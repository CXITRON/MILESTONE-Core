#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR=$(mktemp -d /tmp/milestone-reconcile-test.XXXXXX)
cleanup() {
  rm -rf -- "$TEST_DIR"
}
trap cleanup EXIT

# Load only the two history-inspection helpers. Running milestone-release itself
# would intentionally proceed into GitHub authentication and the firmware build.
sed -n '/^source_version_at_ref()/,/^}/p' "$ROOT_DIR/tools/milestone-release" > "$TEST_DIR/helpers.sh"
sed -n '/^find_taildrop_version_anchor()/,/^}/p' "$ROOT_DIR/tools/milestone-release" >> "$TEST_DIR/helpers.sh"
# shellcheck source=/dev/null
source "$TEST_DIR/helpers.sh"

repo="$TEST_DIR/repo"
git init -q -b main "$repo"
git -C "$repo" config user.name CXITRON
git -C "$repo" config user.email cxitron@proton.me

printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.8.1";' > "$repo/MILESTONE_Core.ino"
git -C "$repo" add MILESTONE_Core.ino
git -C "$repo" commit -q -m 'release: v1.8.1 baseline'
git -C "$repo" tag v1.8.1

git -C "$repo" switch -q -c remote
printf '%s\n' 'remote release workflow' > "$repo/AGENTS.md"
printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.8.2";' > "$repo/MILESTONE_Core.ino"
git -C "$repo" add AGENTS.md MILESTONE_Core.ino
git -C "$repo" commit -q -m 'release: remote v1.8.2'
remote_head=$(git -C "$repo" rev-parse HEAD)

git -C "$repo" switch -q main
printf '%s\n' 'taildrop release workflow with different history' > "$repo/AGENTS.md"
printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.8.2";' > "$repo/MILESTONE_Core.ino"
git -C "$repo" add AGENTS.md MILESTONE_Core.ino
git -C "$repo" commit -q -m 'release: taildrop v1.8.2'
taildrop_anchor=$(git -C "$repo" rev-parse HEAD)

printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.8.3";' > "$repo/MILESTONE_Core.ino"
printf '%s\n' 'diagnostics hardening' > "$repo/feature.txt"
git -C "$repo" add MILESTONE_Core.ino feature.txt
git -C "$repo" commit -q -m 'fix: prepare v1.8.3'

PROJECT_DIR=$repo
remote_version=$(source_version_at_ref remote)
[[ $remote_version == 1.8.2 ]]
version_anchor=$(find_taildrop_version_anchor "$remote_version" v1.8.1)
[[ $version_anchor == "$taildrop_anchor" ]]

# Only v1.8.3 is replayed. The conflicting Taildrop-side v1.8.2 preparation
# commit is skipped because the remote already represents that firmware base.
git -C "$repo" rebase -q --onto remote "$version_anchor" main
git -C "$repo" merge-base --is-ancestor "$remote_head" main
[[ $(source_version_at_ref main) == 1.8.3 ]]
[[ $(git -C "$repo" show main:AGENTS.md) == 'remote release workflow' ]]
[[ $(git -C "$repo" show main:feature.txt) == 'diagnostics hardening' ]]

printf 'Taildrop release reconciliation test passed\n'
