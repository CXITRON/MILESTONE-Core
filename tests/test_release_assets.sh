#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR=$(mktemp -d /tmp/milestone-release-assets-test.XXXXXX)
cleanup() {
  rm -rf -- "$TEST_DIR"
}
trap cleanup EXIT

mkdir -p "$TEST_DIR/project/release" "$TEST_DIR/published" "$TEST_DIR/bin"
for asset in MILESTONE_Core.bin MILESTONE_Core.json MILESTONE_Media.bin MILESTONE_Media.json; do
  printf 'verified %s\n' "$asset" > "$TEST_DIR/project/release/$asset"
done
cp "$TEST_DIR/project/release/MILESTONE_Core.bin" "$TEST_DIR/published/"
cp "$TEST_DIR/project/release/MILESTONE_Core.json" "$TEST_DIR/published/"

cat > "$TEST_DIR/bin/gh" <<'MOCK_GH'
#!/usr/bin/env bash
set -euo pipefail

if [[ $1 == release && $2 == view ]]; then
  find "$MOCK_PUBLISHED_DIR" -maxdepth 1 -type f -printf '%f\n' | sort
  exit 0
fi

if [[ $1 == release && $2 == download ]]; then
  pattern=''
  destination=''
  while (( $# > 0 )); do
    case "$1" in
      --pattern)
        pattern=$2
        shift 2
        ;;
      --dir)
        destination=$2
        shift 2
        ;;
      *)
        shift
        ;;
    esac
  done
  [[ -f "$MOCK_PUBLISHED_DIR/$pattern" ]] || exit 1
  cp "$MOCK_PUBLISHED_DIR/$pattern" "$destination/$pattern"
  exit 0
fi

exit 2
MOCK_GH
chmod +x "$TEST_DIR/bin/gh"

sed -n '/^release_asset_paths()/,/^}/p' "$ROOT_DIR/tools/milestone-release" > "$TEST_DIR/helpers.sh"
sed -n '/^validate_existing_release_assets_against_build()/,/^}/p' "$ROOT_DIR/tools/milestone-release" >> "$TEST_DIR/helpers.sh"
sed -n '/^verify_published_release_assets()/,/^}/p' "$ROOT_DIR/tools/milestone-release" >> "$TEST_DIR/helpers.sh"
sed -n '/^handoff_to_incoming_script_if_needed()/,/^}/p' "$ROOT_DIR/tools/milestone-release" >> "$TEST_DIR/helpers.sh"
# shellcheck source=/dev/null
source "$TEST_DIR/helpers.sh"

PATH="$TEST_DIR/bin:$PATH"
export PATH
MOCK_PUBLISHED_DIR="$TEST_DIR/published"
export MOCK_PUBLISHED_DIR
PROJECT_DIR="$TEST_DIR/project"
EXPECTED_REPO='CXITRON/MILESTONE-Core'
TAG='v2.0.0'
RELEASE_EXISTS=1
EXPECTED_RELEASE_ASSETS=(
  MILESTONE_Core.bin
  MILESTONE_Core.json
  MILESTONE_Media.bin
  MILESTONE_Media.json
)
log() { :; }
die() {
  printf '%s\n' "$*" >&2
  exit 1
}

# Existing matching assets are safe, even when MEDIA is the part that is absent.
validate_existing_release_assets_against_build
if (verify_published_release_assets >/dev/null 2>&1); then
  echo 'Release asset test: incomplete release passed verification' >&2
  exit 1
fi

cp "$TEST_DIR/project/release/MILESTONE_Media.bin" "$TEST_DIR/published/"
cp "$TEST_DIR/project/release/MILESTONE_Media.json" "$TEST_DIR/published/"
verify_published_release_assets

printf 'tampered core\n' > "$TEST_DIR/published/MILESTONE_Core.bin"
if (validate_existing_release_assets_against_build >/dev/null 2>&1); then
  echo 'Release asset test: mismatched published asset was accepted' >&2
  exit 1
fi

mkdir -p "$TEST_DIR/incoming/tools"
cat > "$TEST_DIR/incoming/tools/milestone-release" <<'HANDOFF_SCRIPT'
#!/usr/bin/env bash
printf '%s\n' "handoff=${MILESTONE_RELEASE_HANDOFF:-}" "$@" > "$MILESTONE_HANDOFF_CAPTURE"
HANDOFF_SCRIPT
chmod +x "$TEST_DIR/incoming/tools/milestone-release"
PROJECT_DIR="$TEST_DIR/incoming"
ASSUME_YES=1
DRY_RUN=1
VERSION='2.0.0'
NOTES='Split CORE and MEDIA firmware profiles'
MILESTONE_HANDOFF_CAPTURE="$TEST_DIR/handoff-args"
export MILESTONE_HANDOFF_CAPTURE
(handoff_to_incoming_script_if_needed "$TEST_DIR/MILESTONE_Core_2.0.0.zip")
grep -Fxq 'handoff=1' "$MILESTONE_HANDOFF_CAPTURE"
grep -Fxq -- '--yes' "$MILESTONE_HANDOFF_CAPTURE"
grep -Fxq -- '--dry-run' "$MILESTONE_HANDOFF_CAPTURE"
grep -Fxq -- '--zip' "$MILESTONE_HANDOFF_CAPTURE"
grep -Fxq -- 'taildrop' "$MILESTONE_HANDOFF_CAPTURE"
grep -Fxq -- '2.0.0' "$MILESTONE_HANDOFF_CAPTURE"
grep -Fxq -- 'Split CORE and MEDIA firmware profiles' "$MILESTONE_HANDOFF_CAPTURE"

printf 'GitHub release asset reconciliation test passed\n'
