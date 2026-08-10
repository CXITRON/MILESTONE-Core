#!/usr/bin/env bash
set -euo pipefail

if (( $# < 1 || $# > 2 )); then
  echo "사용법: $0 VERSION [NOTES]" >&2
  echo "예: $0 1.6.1 'Harden Wi-Fi storage and Enterprise compatibility'" >&2
  exit 2
fi

version=$1
notes=${2:-"MILESTONE Core v${version}"}
fqbn='esp32:esp32:waveshare_esp32_s3_zero:CDCOnBoot=default,PSRAM=enabled,PartitionScheme=min_spiffs'

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
release_dir="$project_dir/release"

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "오류: VERSION은 1.6.0과 같은 형식이어야 합니다." >&2
  exit 2
fi

source_version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9][0-9.]*\)".*/\1/p' "$project_dir/MILESTONE_Core.ino")
if [[ $source_version != "$version" ]]; then
  echo "오류: 요청 버전($version)과 소스 버전($source_version)이 다릅니다." >&2
  exit 2
fi

notes=${notes//\\/\\\\}
notes=${notes//\"/\\\"}
notes=${notes//$'\n'/\\n}
notes=${notes//$'\r'/\\r}
notes=${notes//$'\t'/\\t}
notes=${notes//$'\b'/\\b}
notes=${notes//$'\f'/\\f}
if printf '%s' "$notes" | LC_ALL=C grep '[[:cntrl:]]' >/dev/null; then
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
build_dir=$(mktemp -d /tmp/milestone-release-build.XXXXXX)
stage_dir=$(mktemp -d "$release_dir/.release-stage.XXXXXX")
cleanup() {
  rm -rf -- "$build_dir" "$stage_dir"
}
trap cleanup EXIT

echo "고정 빌드 설정: $fqbn"
"$arduino_cli" compile --fqbn "$fqbn" --build-path "$build_dir" --warnings all "$project_dir"

source_bin="$build_dir/MILESTONE_Core.ino.bin"
if [[ ! -f $source_bin ]]; then
  echo "오류: 컴파일은 끝났지만 애플리케이션 BIN을 찾지 못했습니다: $source_bin" >&2
  exit 2
fi
first_byte=$(od -An -tx1 -N1 -- "$source_bin" | tr -d '[:space:]')
if [[ $first_byte != e9 ]]; then
  echo "오류: ESP32 애플리케이션 BIN이 아닙니다(시작 바이트가 0xE9가 아님)." >&2
  exit 2
fi
if ! strings -a -- "$source_bin" | grep -Fx -- "$version" >/dev/null; then
  echo "오류: BIN 내부에서 펌웨어 버전 $version을 확인하지 못했습니다." >&2
  exit 2
fi

staged_bin="$stage_dir/MILESTONE_Core.bin"
staged_manifest="$stage_dir/MILESTONE_Core.json"
cp -- "$source_bin" "$staged_bin"
size=$(stat -c '%s' -- "$staged_bin")
sha256=$(sha256sum -- "$staged_bin" | awk '{print $1}')
printf '{\n  "version": "%s",\n  "size": %s,\n  "sha256": "%s",\n  "notes": "%s"\n}\n' \
  "$version" "$size" "$sha256" "$notes" > "$staged_manifest"

if [[ $(stat -c '%s' -- "$staged_bin") != "$size" ]] ||
   [[ $(sha256sum -- "$staged_bin" | awk '{print $1}') != "$sha256" ]]; then
  echo "오류: 스테이징된 릴리스 BIN 검증에 실패했습니다." >&2
  exit 2
fi

# All validation and compilation has succeeded. Replace public artifacts only now.
mv -- "$staged_bin" "$release_dir/MILESTONE_Core.bin"
mv -- "$staged_manifest" "$release_dir/MILESTONE_Core.json"

echo "Release 파일 생성 완료"
echo "  BIN:      $release_dir/MILESTONE_Core.bin"
echo "  Manifest: $release_dir/MILESTONE_Core.json"
echo "  Size:     $size bytes"
echo "  SHA-256:  $sha256"
echo
echo "GitHub 태그 v${version}의 정식 Release에 위 두 파일을 첨부하세요."
