#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_script="$script_dir/milestone-release"
target_dir="$HOME/.local/bin"
target="$target_dir/milestone-release"

[[ -x $source_script ]] || {
  echo "오류: $source_script 를 찾지 못했거나 실행 권한이 없습니다." >&2
  exit 1
}

mkdir -p -- "$target_dir"
install -m 0755 -- "$source_script" "$target"

echo "설치 완료: $target"

case ":$PATH:" in
  *":$target_dir:"*)
    echo "사용 가능: milestone-release --help"
    ;;
  *)
    echo
    echo "주의: $target_dir 가 현재 PATH에 없습니다."
    echo 'zsh에서는 ~/.zshrc에 다음 줄을 추가한 뒤 새 터미널을 여세요:'
    echo 'export PATH="$HOME/.local/bin:$PATH"'
    ;;
esac
