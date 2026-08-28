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

"$cxx" \
  -std=c++11 \
  -Wall -Wextra -Wpedantic -Werror \
  -I"$project_dir" \
  "$project_dir/CoreDiagnostics.cpp" \
  "$project_dir/tests/test_core_diagnostics.cpp" \
  -o "$build_dir/test_core_diagnostics"

"$build_dir/test_core_diagnostics"

"$cxx" \
  -std=c++11 \
  -Wall -Wextra -Wpedantic -Werror \
  -DMILESTONE_BUILD_PROFILE=2 \
  -I"$project_dir" \
  "$project_dir/CoreMedia.cpp" \
  "$project_dir/tests/test_core_media.cpp" \
  -o "$build_dir/test_core_media"

"$build_dir/test_core_media"

core_portal_html="$build_dir/core-portal.html"
media_portal_html="$build_dir/media-portal.html"
now_portal_html="$build_dir/now-portal.html"
profiles=(core media now)
media_flags=(0 1 0)
general_flags=(1 0 0)
now_flags=(0 0 1)
for index in "${!profiles[@]}"; do
  profile=${profiles[$index]}
  portal_binary="$build_dir/render-portal-$profile"
  "$cxx" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir" -DMILESTONE_HAS_MEDIA=${media_flags[$index]} \
    -DMILESTONE_HAS_GENERAL_VIEWS=${general_flags[$index]} \
    -DMILESTONE_HAS_NOW_VIEW=${now_flags[$index]} \
    "$project_dir/tests/render_portal.cpp" -o "$portal_binary"
  "$portal_binary" > "$build_dir/$profile-portal.html"
done
for marker in 'id="media_preview"' '/api/media/upload' "location.href='/stream'"; do
  for portal_html in "$core_portal_html" "$now_portal_html"; do
    if grep -Fq -- "$marker" "$portal_html"; then
      echo "Profile portal contract: non-MEDIA portal still contains $marker" >&2
      exit 1
    fi
  done
  grep -Fq -- "$marker" "$media_portal_html" || {
    echo "Profile portal contract: MEDIA portal is missing $marker" >&2
    exit 1
  }
done
grep -Fq 'id="general_dday_card"' "$core_portal_html" || {
  echo 'Profile portal contract: CORE portal is missing general view settings' >&2
  exit 1
}
for portal_html in "$media_portal_html" "$now_portal_html"; do
  if grep -Fq 'id="general_dday_card"' "$portal_html"; then
    echo 'Profile portal contract: non-CORE portal still contains visible general view settings' >&2
    exit 1
  fi
done
if command -v node >/dev/null 2>&1; then
  node --test "$project_dir/services/artwork-worker/test/"*.test.js
  for portal_html in "$core_portal_html" "$media_portal_html" "$now_portal_html"; do
    sed -n '/<script>/,/<\/script>/p' "$portal_html" | sed '1s/^.*<script>//; $s/<\/script>.*$//' | node --check -
  done
fi
echo 'Profile-specific portal compilation contract passed'

bash "$project_dir/tests/test_release_manifest.sh"
bash "$project_dir/tests/test_diagnostics_contract.sh"
MILESTONE_RENDERED_MEDIA_PORTAL="$media_portal_html" bash "$project_dir/tests/test_media_contract.sh"
bash "$project_dir/tests/test_radio_bluetooth_contract.sh"
bash "$project_dir/tests/test_profile_ota_contract.sh"
bash "$project_dir/tests/test_network_stability_contract.sh"
bash "$project_dir/tests/test_hardware_contract.sh"
bash "$project_dir/tests/test_docs_contract.sh"
node "$project_dir/tests/test_media_picker.js" "$media_portal_html"
bash "$project_dir/tests/test_release_reconcile.sh"
bash "$project_dir/tests/test_release_assets.sh"
