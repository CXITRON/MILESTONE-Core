#!/usr/bin/env bash
set -euo pipefail

if (( $# < 1 || $# > 2 )); then
  echo "사용법: $0 VERSION [NOTES]" >&2
  echo "예: $0 2.2.3 'Fix NOW artwork settling and remove album layout'" >&2
  exit 2
fi

version=$1
notes=${2:-"MILESTONE Core v${version}"}
fqbn='esp32:esp32:waveshare_esp32_s3_zero:CDCOnBoot=default,PSRAM=enabled,PartitionScheme=min_spiffs'
# Arduino-ESP32's chip debug report contains __DATE__/__TIME__. Pin GCC's
# reproducible-build clock so rebuilding the same source produces the same BIN
# and an existing release can be verified or repaired byte-for-byte.
release_source_date_epoch=946684800

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
release_dir="$project_dir/release"
# shellcheck source=release-json.sh
source "$script_dir/release-json.sh"

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "오류: VERSION은 2.0.0과 같은 형식이어야 합니다." >&2
  exit 2
fi

source_version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9][0-9.]*\)".*/\1/p' "$project_dir/MILESTONE_Core.ino")
if [[ $source_version != "$version" ]]; then
  echo "오류: 요청 버전($version)과 소스 버전($source_version)이 다릅니다." >&2
  exit 2
fi

echo "호스트 순수 로직 및 프로필 계약 테스트 실행"
bash "$project_dir/tools/test-core.sh"

if ! notes=$(milestone_escape_json_string "$notes"); then
  echo "오류: NOTES에 지원되지 않는 제어 문자가 포함되어 있습니다." >&2
  exit 2
fi

arduino_cli=${ARDUINO_CLI:-}
if [[ -z $arduino_cli ]] && command -v arduino-cli >/dev/null 2>&1; then
  arduino_cli=$(command -v arduino-cli)
fi
if [[ -z $arduino_cli && -x /opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli ]]; then
  arduino_cli=/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli
fi
if [[ -z $arduino_cli || ! -x $arduino_cli ]]; then
  echo "오류: arduino-cli를 찾지 못했습니다. Arduino CLI를 설치하거나 ARDUINO_CLI에 실행 경로를 지정하세요." >&2
  exit 2
fi

mkdir -p -- "$release_dir"
build_root=$(mktemp -d /tmp/milestone-release-build.XXXXXX)
stage_dir=$(mktemp -d "$release_dir/.release-stage.XXXXXX")
cleanup() {
  rm -rf -- "$build_root" "$stage_dir"
}
trap cleanup EXIT

profiles=(core media now)
profile_ids=(1 2 3)
asset_stems=(MILESTONE_Core MILESTONE_Media MILESTONE_Now)
markers=(MILESTONE_PROFILE_CORE MILESTONE_PROFILE_MEDIA MILESTONE_PROFILE_NOW)
media_binary_markers=(/api/media/upload /api/stream/start /media/upload.tmp)
ble_runtime_marker=MILESTONE_BLE_AMS_RUNTIME_V4
artwork_runtime_marker=MILESTONE_NOW_ARTWORK_RUNTIME_V1

echo "고정 빌드 설정: $fqbn"
for index in "${!profiles[@]}"; do
  profile=${profiles[$index]}
  profile_id=${profile_ids[$index]}
  asset_stem=${asset_stems[$index]}
  marker=${markers[$index]}
  build_dir="$build_root/$profile"
  mkdir -p -- "$build_dir"
  reproducible_path_flags="-ffile-prefix-map=$project_dir=/src -fmacro-prefix-map=$project_dir=/src -fdebug-prefix-map=$project_dir=/src -ffile-prefix-map=$build_root=/build -fmacro-prefix-map=$build_root=/build -fdebug-prefix-map=$build_root=/build"

  echo "[$profile] 펌웨어 컴파일"
  TZ=UTC SOURCE_DATE_EPOCH="$release_source_date_epoch" "$arduino_cli" compile \
    --fqbn "$fqbn" \
    --build-path "$build_dir" \
    --build-property "compiler.cpp.extra_flags=-DMILESTONE_BUILD_PROFILE=$profile_id $reproducible_path_flags" \
    --build-property "compiler.c.extra_flags=$reproducible_path_flags" \
    --build-property "compiler.S.extra_flags=$reproducible_path_flags" \
    --warnings all \
    "$project_dir"

  source_bin="$build_dir/MILESTONE_Core.ino.bin"
  if [[ ! -f $source_bin ]]; then
    echo "오류: [$profile] 애플리케이션 BIN을 찾지 못했습니다: $source_bin" >&2
    exit 2
  fi
  first_byte=$(od -An -tx1 -N1 -- "$source_bin" | tr -d '[:space:]')
  if [[ $first_byte != e9 ]]; then
    echo "오류: [$profile] ESP32 애플리케이션 BIN이 아닙니다." >&2
    exit 2
  fi
  if ! strings -a -- "$source_bin" | grep -Fx -- "$version" >/dev/null; then
    echo "오류: [$profile] BIN 내부에서 펌웨어 버전 $version을 확인하지 못했습니다." >&2
    exit 2
  fi
  # Do not use grep -q here while pipefail is enabled. A successful early
  # match closes the pipe, strings receives SIGPIPE, and the pipeline is then
  # reported as failed even though the marker was present.
  if ! strings -a -- "$source_bin" | grep -F -- "$marker" >/dev/null; then
    echo "오류: [$profile] BIN 내부에서 프로필 표식을 확인하지 못했습니다." >&2
    exit 2
  fi
  for media_marker in "${media_binary_markers[@]}"; do
    marker_found=false
    if strings -a -- "$source_bin" | grep -F -- "$media_marker" >/dev/null; then
      marker_found=true
    fi
    if [[ $profile != media && $marker_found == true ]]; then
      echo "오류: [$profile] 미디어/스트리밍 구현이 BIN에 남아 있습니다: $media_marker" >&2
      exit 2
    fi
    if [[ $profile == media && $marker_found == false ]]; then
      echo "오류: [media] 필수 미디어/스트리밍 구현을 BIN에서 찾지 못했습니다: $media_marker" >&2
      exit 2
    fi
  done
  ble_marker_found=false
  if strings -a -- "$source_bin" | grep -F -- "$ble_runtime_marker" >/dev/null; then
    ble_marker_found=true
  fi
  if [[ $profile == now && $ble_marker_found == false ]]; then
    echo "오류: [now] BLE AMS 런타임 구현을 BIN에서 찾지 못했습니다." >&2
    exit 2
  fi
  if [[ $profile != now && $ble_marker_found == true ]]; then
    echo "오류: [$profile] BLE AMS 런타임 구현이 BIN에 포함됐습니다." >&2
    exit 2
  fi
  artwork_marker_found=false
  if strings -a -- "$source_bin" | grep -F -- "$artwork_runtime_marker" >/dev/null; then
    artwork_marker_found=true
  fi
  if [[ $profile == now && $artwork_marker_found == false ]]; then
    echo "오류: [now] 앨범 표지 런타임 구현을 BIN에서 찾지 못했습니다." >&2
    exit 2
  fi
  if [[ $profile != now && $artwork_marker_found == true ]]; then
    echo "오류: [$profile] NOW 앨범 표지 런타임이 BIN에 포함됐습니다." >&2
    exit 2
  fi

  staged_bin="$stage_dir/$asset_stem.bin"
  staged_manifest="$stage_dir/$asset_stem.json"
  cp -- "$source_bin" "$staged_bin"
  size=$(stat -c '%s' -- "$staged_bin")
  sha256=$(sha256sum -- "$staged_bin" | awk '{print $1}')
  printf '{\n  "version": "%s",\n  "profile": "%s",\n  "asset": "%s.bin",\n  "size": %s,\n  "sha256": "%s",\n  "notes": "%s"\n}\n' \
    "$version" "$profile" "$asset_stem" "$size" "$sha256" "$notes" > "$staged_manifest"

  if [[ $(stat -c '%s' -- "$staged_bin") != "$size" ]] ||
     [[ $(sha256sum -- "$staged_bin" | awk '{print $1}') != "$sha256" ]]; then
    echo "오류: [$profile] 스테이징된 릴리스 BIN 검증에 실패했습니다." >&2
    exit 2
  fi
done

# 세 프로필이 모두 검증된 뒤에만 공개 릴리스 산출물을 교체합니다.
for asset_stem in "${asset_stems[@]}"; do
  mv -- "$stage_dir/$asset_stem.bin" "$release_dir/$asset_stem.bin"
  mv -- "$stage_dir/$asset_stem.json" "$release_dir/$asset_stem.json"
done

echo "Release 파일 생성 완료"
for index in "${!profiles[@]}"; do
  profile=${profiles[$index]}
  asset_stem=${asset_stems[$index]}
  size=$(stat -c '%s' -- "$release_dir/$asset_stem.bin")
  sha256=$(sha256sum -- "$release_dir/$asset_stem.bin" | awk '{print $1}')
  echo "  [$profile] $asset_stem.bin / $asset_stem.json"
  echo "           $size bytes / $sha256"
done
echo
echo "GitHub 태그 v${version}의 정식 Release에 위 여섯 파일을 함께 첨부하세요."
