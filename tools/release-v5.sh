#!/usr/bin/env bash
# Internal backend, sourced only by tools/make-release.sh. Publication remains
# in milestone-release with its existing identity/tag/atomic-push checks.
milestone_build_v5() {
  local version=$1 notes=$2
  local private_key=${MILESTONE_V5_PRIVATE_KEY:-} public_key=${MILESTONE_V5_PUBLIC_KEY:-}
  release_dir=${MILESTONE_V5_OUTPUT_DIR:-$release_dir}
  [[ -f $private_key && -f $public_key ]] || { echo 'v5 release requires MILESTONE_V5_PRIVATE_KEY and MILESTONE_V5_PUBLIC_KEY paths.' >&2; return 2; }
  local source_version
  source_version=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([0-9.]*\)".*/\1/p' "$project_dir/v5/libraries/MilestoneV5Core/src/MilestoneV5Version.h")
  [[ $version == "$source_version" ]] || { echo 'v5 source version mismatch' >&2; return 2; }
  bash "$project_dir/tools/test-core.sh"
  bash "$project_dir/tools/test-v5.sh"
  local cli=${ARDUINO_CLI:-/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli}
  [[ -x $cli ]] || cli=$(command -v arduino-cli)
  local esptool=${ESPTOOL:-/home/citron/.arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool}
  [[ -x $esptool ]] || { echo 'ESPTOOL must name the installed Espressif esptool executable' >&2; return 2; }
  local task_root
  task_root=$(mktemp -d /tmp/milestone-v5-release.XXXXXX)
  # Paths are concrete mktemp results, never project roots.
  # Expand the concrete mktemp path now. The local variable is no longer in
  # scope if set -e reaches the EXIT trap through a failed compiler command.
  trap "rm -rf -- '$task_root'" EXIT
  local stage="$task_root/assets"
  mkdir -p "$stage"
  python3 "$project_dir/tools/v5-release-assets.py" header "$task_root/trust.h" --public-key "$public_key"
  local main_fqbn='esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi'
  local zero_fqbn='esp32:esp32:waveshare_esp32_s3_zero:CDCOnBoot=default,PSRAM=enabled,PartitionScheme=min_spiffs'
  local path_flags="-ffile-prefix-map=$project_dir=/src -fmacro-prefix-map=$project_dir=/src -fdebug-prefix-map=$project_dir=/src -ffile-prefix-map=$task_root=/build -fmacro-prefix-map=$task_root=/build -fdebug-prefix-map=$task_root=/build"
  local role sketch fqbn build
  for role in main zero safe; do
    case $role in main) sketch=MilestoneV5Main;fqbn=$main_fqbn;; zero) sketch=MilestoneV5Zero;fqbn=$zero_fqbn;; safe) sketch=MilestoneV5Safe;fqbn=$main_fqbn;; esac
    build="$task_root/$role"
    TZ=UTC SOURCE_DATE_EPOCH=946684800 "$cli" compile --fqbn "$fqbn" --build-path "$build" \
      --libraries "$project_dir/v5/libraries" \
      --build-property "compiler.cpp.extra_flags=-include $task_root/trust.h $path_flags -Werror" \
      --build-property "compiler.c.extra_flags=$path_flags -Werror" \
      --build-property "compiler.S.extra_flags=$path_flags" \
      --warnings all \
      "$project_dir/v5/$sketch"
    cp "$build/$sketch.ino.bin" "$stage/v5-$role.bin"
  done
  cmp "$project_dir/v5/MilestoneV5Main/partitions.csv" "$project_dir/v5/MilestoneV5Safe/partitions.csv"
  cmp "$task_root/main/MilestoneV5Main.ino.partitions.bin" "$task_root/safe/MilestoneV5Safe.ino.partitions.bin"
  "$esptool" --chip esp32s3 merge-bin --flash-size 16MB -o "$stage/v5-main-initial.bin" \
    0x0 "$task_root/main/MilestoneV5Main.ino.bootloader.bin" \
    0x8000 "$task_root/main/MilestoneV5Main.ino.partitions.bin" \
    0x10000 "$stage/v5-safe.bin" 0x210000 "$stage/v5-main.bin"
  cp "$task_root/zero/MilestoneV5Zero.ino.merged.bin" "$stage/v5-zero-initial.bin"
  python3 "$project_dir/tools/prepare-v5-sd-restore.py" "$stage/v5-main.bin" "$task_root/bundle" \
    --bundle --zero-source "$stage/v5-zero.bin" --version "$version" --private-key "$private_key" --public-key "$public_key"
  cp "$task_root/bundle/bundle.txt" "$stage/v5-bundle.txt"
  cp "$task_root/bundle/bundle.sig" "$stage/v5-bundle.sig"
  for role in main zero; do
    cp "$task_root/bundle/$role/manifest.txt" "$stage/v5-$role-manifest.txt"
    cp "$task_root/bundle/$role/manifest.sig" "$stage/v5-$role-manifest.sig"
  done
  python3 "$project_dir/tools/v5-release-assets.py" catalog "$stage" --version "$version" --private-key "$private_key" --public-key "$public_key"
  mkdir -p "$release_dir"
  local file
  for file in "$stage"/*; do install -m 644 "$file" "$release_dir/${file##*/}"; done
  printf 'v5 build/sign/verification complete: %s (%s)\n' "$version" "$notes"
  # Cleanup before the local variables leave scope.
  rm -rf -- "$task_root"
  trap - EXIT
}
