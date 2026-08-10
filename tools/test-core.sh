#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
cxx=${CXX:-g++}

if ! command -v "$cxx" >/dev/null 2>&1; then
  echo "오류: 호스트 C++ 컴파일러를 찾지 못했습니다: $cxx" >&2
  exit 2
fi

build_dir=$(mktemp -d /tmp/milestone-core-tests.XXXXXX)
cleanup() {
  rm -rf -- "$build_dir"
}
trap cleanup EXIT

"$cxx" \
  -std=c++11 \
  -Wall -Wextra -Wpedantic -Werror \
  -I"$project_dir" \
  "$project_dir/CoreLogic.cpp" \
  "$project_dir/tests/test_core_logic.cpp" \
  -o "$build_dir/test_core_logic"

"$build_dir/test_core_logic"
bash "$project_dir/tests/test_release_manifest.sh"
