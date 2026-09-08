#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
source_dir="$project_dir/v5/libraries/MilestoneV5Core/src"
cxx=${CXX:-g++}

command -v "$cxx" >/dev/null 2>&1 || {
  echo "오류: 호스트 C++ 컴파일러를 찾지 못했습니다: $cxx" >&2
  exit 2
}

build_dir=$(mktemp -d /tmp/milestone-v5-tests.XXXXXX)
cleanup() {
  rm -rf -- "$build_dir"
}
trap cleanup EXIT

common_flags=(-std=c++11 -Wall -Wextra -Wpedantic -Werror -I"$source_dir")
if [[ "${V5_SANITIZE:-0}" == 1 ]]; then
  common_flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

"$cxx" "${common_flags[@]}" \
  "$source_dir/MilestoneV5Protocol.cpp" \
  "$source_dir/MilestoneV5Link.cpp" \
  "$source_dir/MilestoneV5Transport.cpp" \
  "$project_dir/tests/test_v5_protocol.cpp" \
  -o "$build_dir/test_v5_protocol"
"$build_dir/test_v5_protocol"

"$cxx" "${common_flags[@]}" \
  "$source_dir/MilestoneV5Runtime.cpp" \
  "$project_dir/tests/test_v5_runtime.cpp" \
  -o "$build_dir/test_v5_runtime"
"$build_dir/test_v5_runtime"

"$cxx" "${common_flags[@]}" \
  "$source_dir/MilestoneV5Runtime.cpp" \
  "$source_dir/MilestoneV5Features.cpp" \
  "$source_dir/MilestoneV5Settings.cpp" \
  "$source_dir/MilestoneV5UpdateRecovery.cpp" \
  "$project_dir/tests/test_v5_features.cpp" \
  -o "$build_dir/test_v5_features"
"$build_dir/test_v5_features"

"$cxx" "${common_flags[@]}" "$source_dir/MilestoneV5Rtc.cpp" \
  "$project_dir/tests/test_v5_rtc.cpp" -o "$build_dir/test_v5_rtc"
"$build_dir/test_v5_rtc"
"$cxx" "${common_flags[@]}" "$source_dir/MilestoneV5Now.cpp" \
  "$project_dir/tests/test_v5_now.cpp" -o "$build_dir/test_v5_now"
"$build_dir/test_v5_now"
"$cxx" "${common_flags[@]}" "$source_dir/MilestoneV5Video.cpp" \
  "$project_dir/tests/test_v5_video.cpp" -o "$build_dir/test_v5_video"
"$build_dir/test_v5_video"
"$cxx" "${common_flags[@]}" "$source_dir/MilestoneV5Protocol.cpp" \
  "$project_dir/tests/test_v5_safety.cpp" -o "$build_dir/test_v5_safety"
"$build_dir/test_v5_safety"
"$cxx" "${common_flags[@]}" "$source_dir/MilestoneV5Manifest.cpp" \
  "$project_dir/tests/test_v5_manifest.cpp" -o "$build_dir/test_v5_manifest"
"$build_dir/test_v5_manifest"
"$cxx" "${common_flags[@]}" "$source_dir/MilestoneV5Manifest.cpp" \
  "$source_dir/MilestoneV5Bundle.cpp" "$source_dir/MilestoneV5Protocol.cpp" \
  "$project_dir/tests/test_v5_bundle.cpp" -o "$build_dir/test_v5_bundle"
"$build_dir/test_v5_bundle"
"$cxx" -I"$project_dir/tests/v5_mocks" "${common_flags[@]}" \
  "$source_dir/MilestoneV5Protocol.cpp" "$source_dir/MilestoneV5Manifest.cpp" \
  "$project_dir/tests/test_v5_ota_receiver.cpp" -o "$build_dir/test_v5_ota_receiver"
"$build_dir/test_v5_ota_receiver"
"$cxx" -I"$project_dir/tests/v5_mocks" "${common_flags[@]}" -std=c++17 \
  "$source_dir/MilestoneV5Protocol.cpp" "$source_dir/MilestoneV5Manifest.cpp" \
  "$source_dir/MilestoneV5Bundle.cpp" "$project_dir/tests/test_v5_bundle_runtime.cpp" \
  -o "$build_dir/test_v5_bundle_runtime"
"$build_dir/test_v5_bundle_runtime" "$build_dir"
PYTHONDONTWRITEBYTECODE=1 python3 "$project_dir/tests/test_v5_sd_prepare.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$project_dir/tests/test_v5_docs.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$project_dir/tests/test_v5_parity_contract.py"
PYTHONDONTWRITEBYTECODE=1 python3 "$project_dir/tests/test_v5_sync_media_contract.py"
(cd "$project_dir" && node tests/test_v5_sync_upload.js)
PYTHONDONTWRITEBYTECODE=1 python3 "$project_dir/tests/test_v5_portal_recovery_contract.py"
for test in storage_runtime download_runtime; do
  "$cxx" -I"$project_dir/tests/v5_mocks" "${common_flags[@]}" -std=c++17 \
    "$source_dir/MilestoneV5Protocol.cpp" "$source_dir/MilestoneV5Manifest.cpp" \
    "$source_dir/MilestoneV5Video.cpp" \
    "$source_dir/MilestoneV5Bundle.cpp" "$project_dir/tests/test_v5_$test.cpp" -o "$build_dir/test_v5_$test"
  "$build_dir/test_v5_$test" "$build_dir"
done
