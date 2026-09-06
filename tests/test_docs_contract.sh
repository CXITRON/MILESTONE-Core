#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"

required_docs=(AGENTS.md MILESTONE_PROJECT_CONTEXT.md README.md)
for file in "${required_docs[@]}"; do
  [[ -f $file ]] || { echo "Documentation contract: required file missing: $file" >&2; exit 1; }
done

require_fixed() {
  local file=$1 text=$2 message=$3
  grep -Fq -- "$text" "$file" || { echo "Documentation contract: $message" >&2; exit 1; }
}

legacy_version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9][0-9.]*\)".*/\1/p' MILESTONE_Core.ino | head -n1)
current_version=$(sed -n 's/^- Current firmware baseline: `\([0-9][0-9.]*\)`$/\1/p' AGENTS.md | head -n1)
[[ -n $legacy_version && -n $current_version ]] || {
  echo 'Documentation contract: firmware/current baseline version not found' >&2
  exit 1
}
version=$legacy_version
if [[ $current_version != "$legacy_version" ]]; then
  version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9][0-9.]*\)".*/\1/p' \
    v5/libraries/MilestoneV5Core/src/MilestoneV5Version.h | head -n1)
  [[ $current_version == "$version" ]] || {
    echo "Documentation contract: current baseline $current_version has no matching source" >&2
    exit 1
  }
  require_fixed AGENTS.md "- Legacy firmware baseline: \`$legacy_version\`" \
    "AGENTS.md does not preserve the legacy $legacy_version baseline"
  require_fixed MILESTONE_PROJECT_CONTEXT.md "> Legacy baseline: MILESTONE Core v$legacy_version" \
    "project context does not preserve the legacy $legacy_version baseline"
fi

require_fixed AGENTS.md "- Current firmware baseline: \`$version\`" "AGENTS.md baseline does not match $version"
require_fixed MILESTONE_PROJECT_CONTEXT.md "> Current baseline: MILESTONE Core v$version" "project context baseline does not match $version"
require_fixed README.md "## v$version 업데이트 안내" "README.md has no v$version update entry"

mapfile -t versions < <(sed -n 's/^## v\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\) 업데이트 안내$/\1/p' README.md)
[[ ${#versions[@]} -gt 0 ]] || { echo 'Documentation contract: README version history is empty' >&2; exit 1; }
[[ ${versions[0]} == "$version" ]] || { echo "Documentation contract: newest README entry is ${versions[0]}, expected $version" >&2; exit 1; }

mapfile -t sorted < <(printf '%s\n' "${versions[@]}" | sort -Vr)
if ! diff -u <(printf '%s\n' "${sorted[@]}") <(printf '%s\n' "${versions[@]}") >/dev/null; then
  echo 'Documentation contract: README version history is not newest-to-oldest semantic-version order' >&2
  diff -u <(printf '%s\n' "${sorted[@]}") <(printf '%s\n' "${versions[@]}") >&2 || true
  exit 1
fi

if [[ $(printf '%s\n' "${versions[@]}" | sort | uniq -d | wc -l) -ne 0 ]]; then
  echo 'Documentation contract: duplicate README version entries found' >&2
  exit 1
fi

echo "Documentation/version contract test passed (v$version)"
