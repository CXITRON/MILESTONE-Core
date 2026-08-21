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
for profile_id in 0 1; do
  portal_binary="$build_dir/render-portal-$profile_id"
  general_views=$((1 - profile_id))
  "$cxx" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir" -DMILESTONE_HAS_MEDIA=$profile_id \
    -DMILESTONE_HAS_GENERAL_VIEWS=$general_views \
    "$project_dir/tests/render_portal.cpp" -o "$portal_binary"
  if [[ $profile_id == 0 ]]; then
    "$portal_binary" > "$core_portal_html"
  else
    "$portal_binary" > "$media_portal_html"
  fi
done
for marker in 'id="media_preview"' '/api/media/upload' "location.href='/stream'"; do
  if grep -Fq -- "$marker" "$core_portal_html"; then
    echo "Profile portal contract: CORE portal still contains $marker" >&2
    exit 1
  fi
  grep -Fq -- "$marker" "$media_portal_html" || {
    echo "Profile portal contract: MEDIA portal is missing $marker" >&2
    exit 1
  }
done
grep -Fq 'id="general_dday_card"' "$core_portal_html" || {
  echo 'Profile portal contract: CORE portal is missing general view settings' >&2
  exit 1
}
if grep -Fq 'id="general_dday_card"' "$media_portal_html"; then
  echo 'Profile portal contract: MEDIA portal still contains visible general view settings' >&2
  exit 1
fi
if command -v node >/dev/null 2>&1; then
  for portal_html in "$core_portal_html" "$media_portal_html"; do
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
bash "$project_dir/tests/test_docs_contract.sh"
node "$project_dir/tests/test_media_picker.js" "$media_portal_html"
bash "$project_dir/tests/test_release_reconcile.sh"
