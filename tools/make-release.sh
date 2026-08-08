#!/usr/bin/env bash
set -euo pipefail

if (( $# < 2 || $# > 3 )); then
  echo "사용법: $0 VERSION COMPILED_BIN [NOTES]" >&2
  echo "예시: $0 1.5.2 MILESTONE_Core.ino.bin 'OTA 안정성 및 복구 개선'" >&2
  exit 2
fi

version=$1
source_bin=$2
notes=${3:-"MILESTONE Core v${version}"}

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "오류: VERSION은 1.5.2와 같은 형식이어야 합니다." >&2
  exit 2
fi
if [[ ! -f $source_bin ]]; then
  echo "오류: BIN 파일을 찾을 수 없습니다: $source_bin" >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
source_version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9][0-9.]*\)".*/\1/p' "$project_dir/MILESTONE_Core.ino" | head -n 1)
if [[ $source_version != "$version" ]]; then
  echo "오류: 요청 버전($version)과 소스 버전($source_version)이 다릅니다." >&2
  exit 2
fi
newer_source=$(find "$project_dir" -maxdepth 1 -type f \
  \( -name '*.ino' -o -name '*.h' -o -name '*.hpp' \) -newer "$source_bin" -print -quit)
if [[ -n $newer_source ]]; then
  echo "오류: BIN보다 새로운 소스 파일이 있습니다: $newer_source" >&2
  echo "Arduino IDE에서 컴파일된 바이너리를 다시 내보내세요." >&2
  exit 2
fi
first_byte=$(od -An -tx1 -N1 -- "$source_bin" | tr -d '[:space:]')
if [[ $first_byte != e9 ]]; then
  echo "오류: ESP32 애플리케이션 BIN이 아닙니다(시작 바이트가 0xE9가 아님)." >&2
  exit 2
fi
if ! strings -a -- "$source_bin" | grep -Fx -- "$version" >/dev/null; then
  echo "오류: BIN 내부에서 펌웨어 버전 $version을 확인하지 못했습니다." >&2
  echo "이전 빌드의 BIN이 아닌지 확인하고 다시 내보내세요." >&2
  exit 2
fi
release_dir="$project_dir/release"
release_bin="$release_dir/MILESTONE_Core.bin"
manifest="$release_dir/MILESTONE_Core.json"

mkdir -p -- "$release_dir"
cp -- "$source_bin" "$release_bin"

size=$(stat -c '%s' -- "$release_bin")
sha256=$(sha256sum -- "$release_bin" | awk '{print $1}')
notes=${notes//\\/\\\\}
notes=${notes//\"/\\\"}
notes=${notes//$'\n'/\\n}
notes=${notes//$'\r'/\\r}
notes=${notes//$'\t'/\\t}
notes=${notes//$'\b'/\\b}
notes=${notes//$'\f'/\\f}
if printf '%s' "$notes" | LC_ALL=C grep -q '[[:cntrl:]]'; then
  echo "오류: NOTES에 지원되지 않는 제어 문자가 포함되어 있습니다." >&2
  exit 2
fi

manifest_temp="$manifest.tmp.$$"
trap 'rm -f -- "$manifest_temp"' EXIT
printf '{\n  "version": "%s",\n  "size": %s,\n  "sha256": "%s",\n  "notes": "%s"\n}\n' \
  "$version" "$size" "$sha256" "$notes" > "$manifest_temp"
mv -- "$manifest_temp" "$manifest"
trap - EXIT

echo "Release 파일 생성 완료"
echo "  BIN:      $release_bin"
echo "  Manifest: $manifest"
echo "  Size:     $size bytes"
echo "  SHA-256:  $sha256"
echo
echo "GitHub 태그 v${version}의 정식 Release에 위 두 파일을 첨부하세요."
