#!/usr/bin/env bash
# Optional pixel QA with the same installed U8g2 fonts used by the firmware.
# Usage: bash tools/test-v5-update-ui-fonts.sh U8g2/src/clib [existing-output-dir]
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
font_dir=${1:?Pass the installed U8g2/src/clib directory}
build_dir=$(mktemp -d /tmp/milestone-update-ui.XXXXXX)
trap 'rm -rf -- "$build_dir"' EXIT
for unit in u8g2_font u8g2_fonts u8g2_setup u8g2_hvline u8g2_ll_hvline u8g2_intersection u8x8_8x8; do
  "${CC:-cc}" -O2 -ffunction-sections -fdata-sections -I"$font_dir" \
    -c "$font_dir/$unit.c" -o "$build_dir/$unit.o"
done
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -DV5_UPDATE_UI_REAL_FONTS \
  -ffunction-sections -fdata-sections -I"$font_dir" -I"$font_dir/.." \
  -I"$project_dir/v5/libraries/MilestoneV5Core/src" \
  "$project_dir/tests/test_v5_update_ui.cpp" "$build_dir/"*.o \
  -Wl,--gc-sections -o "$build_dir/test"
if [[ $# -ge 2 ]]; then "$build_dir/test" "$2"; else "$build_dir/test"; fi
