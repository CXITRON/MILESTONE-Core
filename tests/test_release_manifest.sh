#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
# shellcheck source=../tools/release-json.sh
source "$project_dir/tools/release-json.sh"

original=$'quote" slash/ backslash\\ newline\n carriage\r tab\t back\b form\f end'
expected='quote\" slash/ backslash\\ newline\n carriage\r tab\t back\b form\f end'
escaped=$(milestone_escape_json_string "$original")

if [[ $escaped != "$expected" ]]; then
  echo "FAIL: release manifest JSON escaping mismatch" >&2
  printf 'expected: %q\nactual:   %q\n' "$expected" "$escaped" >&2
  exit 1
fi

if milestone_escape_json_string $'unsupported\x1bcontrol' >/dev/null; then
  echo "FAIL: unsupported JSON control character was accepted" >&2
  exit 1
fi

printf 'Release manifest escape contract test passed\n'
