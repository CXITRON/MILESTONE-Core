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
version_anchor=$(find_taildrop_version_anchor "$remote_version" v1.8.1 remote)
[[ $version_anchor == "$taildrop_anchor" ]]

# Only v1.8.3 is replayed. The conflicting Taildrop-side v1.8.2 preparation
# commit is skipped because the remote already represents that firmware base.
git -C "$repo" rebase -q --onto remote "$version_anchor" main
git -C "$repo" merge-base --is-ancestor "$remote_head" main
[[ $(source_version_at_ref main) == 1.8.3 ]]
[[ $(git -C "$repo" show main:AGENTS.md) == 'remote release workflow' ]]
[[ $(git -C "$repo" show main:feature.txt) == 'diagnostics hardening' ]]

printf 'Taildrop release reconciliation test passed\n'

# If Taildrop contains additional same-version documentation/tooling commits that
# never reached the remote, prefer the older commit whose complete tree actually
# matches the remote release. This prevents those local-only commits from being
# silently treated as the remote firmware anchor.
repo2="$TEST_DIR/repo-same-version"
git init -q -b main "$repo2"
git -C "$repo2" config user.name CXITRON
git -C "$repo2" config user.email cxitron@proton.me
printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.10.3";' > "$repo2/MILESTONE_Core.ino"
printf '%s\n' 'baseline docs' > "$repo2/README.md"
git -C "$repo2" add MILESTONE_Core.ino README.md
git -C "$repo2" commit -q -m 'release: v1.10.3 baseline'
git -C "$repo2" tag v1.10.3

printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.10.5";' > "$repo2/MILESTONE_Core.ino"
printf '%s\n' 'v1.10.5 release docs' > "$repo2/README.md"
git -C "$repo2" add MILESTONE_Core.ino README.md
git -C "$repo2" commit -q -m 'feat: v1.10.5 release'
matching_release=$(git -C "$repo2" rev-parse HEAD)

git -C "$repo2" branch remote-match "$matching_release"

printf '%s\n' 'local-only docs cleanup' >> "$repo2/README.md"
git -C "$repo2" add README.md
git -C "$repo2" commit -q -m 'docs: local-only cleanup'
local_only_same_version=$(git -C "$repo2" rev-parse HEAD)

printf '%s\n' 'constexpr char FIRMWARE_VERSION[] = "1.10.6";' > "$repo2/MILESTONE_Core.ino"
printf '%s\n' 'stream metadata fix' > "$repo2/fix.txt"
git -C "$repo2" add MILESTONE_Core.ino fix.txt
git -C "$repo2" commit -q -m 'fix: v1.10.6 stream metadata'

PROJECT_DIR=$repo2
version_anchor=$(find_taildrop_version_anchor 1.10.5 v1.10.3 remote-match)
[[ $version_anchor == "$matching_release" ]]
[[ $version_anchor != "$local_only_same_version" ]]

printf 'Taildrop exact-tree anchor selection test passed\n'
