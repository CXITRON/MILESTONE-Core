#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"

required_docs=(AGENTS.md MILESTONE_PROJECT_CONTEXT.md README.md)
for file in "${required_docs[@]}"; do
  [[ -f $file ]] || { echo "Documentation contract: required file missing: $file" >&2; exit 1; }
done

version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9][0-9.]*\)".*/\1/p' MILESTONE_Core.ino | head -n1)
[[ -n $version ]] || { echo 'Documentation contract: FIRMWARE_VERSION not found' >&2; exit 1; }

require_fixed() {
  local file=$1 text=$2 message=$3
  grep -Fq -- "$text" "$file" || { echo "Documentation contract: $message" >&2; exit 1; }
}

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
