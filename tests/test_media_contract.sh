#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

require() {
  local pattern=$1 file=$2 message=$3
  if ! grep -Eq -- "$pattern" "$project_dir/$file"; then
    echo "FAIL: $message" >&2
    exit 1
  fi
}

reject() {
  local pattern=$1 file=$2 message=$3
  if grep -Eq -- "$pattern" "$project_dir/$file"; then
    echo "FAIL: $message" >&2
    exit 1
  fi
}

require 'FIRMWARE_VERSION\[\] = "1\.9\.2"' MILESTONE_Core.ino 'firmware version is not 1.9.2'
require 'CONFIG_VERSION = 9' MILESTONE_Core.ino 'config schema is not 9'
require 'CUSTOM_MEDIA = 7' MILESTONE_Core.ino 'custom media view must preserve IDs 0-6'
require 'CUSTOM_MEDIA = 8' MILESTONE_Core.ino 'custom media top mode must preserve IDs 0-7'
require 'storedCycleLimit = migrateToV6 \? 0x3F : \(migrateToV9 \? 0x7F : 0xFF\)' CoreConfig.inc 'v8 cycle mask migration must leave media disabled'
require 'config\.cycleOrder\[7\] = 7' CoreConfig.inc 'v8 cycle order migration must append media'

for route in status list upload update order delete clear repair; do
  require "server\.on\(\"/api/media/${route}\"" CorePortal.inc "missing media API route: $route"
done
require 'MEDIA_UPLOAD_TEMP' CoreMedia.inc 'transactional upload temp file is missing'
require 'MEDIA_INDEX_A' CoreMedia.inc 'A media index is missing'
require 'MEDIA_INDEX_B' CoreMedia.inc 'B media index is missing'
require 'MilestoneMedia::validateFile' CoreMedia.inc 'uploaded media is not fully validated'
require 'clearAllMediaFiles\(\)' CorePortal.inc 'factory reset must clear custom media'
require 'mediaTransferBusy\(\)' CoreRuntime.inc 'OTA scheduler must respect media upload mutual exclusion'
require 'mediaTransferBusy\(\)' CorePortal.inc 'portal update API must respect media upload mutual exclusion'

require 'id="media_preview"' PortalPage.h 'OLED media preview is missing'
require 'decodeGif\(' PortalPage.h 'browser GIF decoder is missing'
require 'id="media_file" type="file"' PortalPage.h 'unfiltered media file chooser is missing'
reject 'id="media_file"[^>]*accept=' PortalPage.h 'media chooser must not apply a native accept filter'
require 'id="media_drop"' PortalPage.h 'drag-and-drop media fallback is missing'
require 'function mediaKind\(' PortalPage.h 'MIME/extension media detection is missing'
require 'function selectMediaFile\(' PortalPage.h 'selected-file state handling is missing'
require 'function handleMediaDrop\(' PortalPage.h 'drag-and-drop file handling is missing'
require 'function openVideoFile\(' PortalPage.h 'bounded video initialization is missing'
require 'function waitForVideo\(' PortalPage.h 'video load timeout handling is missing'
require 'buildMsm\(' PortalPage.h 'MSM1 browser encoder is missing'
require 'x\+\(y>>3\)\*128' PortalPage.h 'U8g2 page-major packing contract is missing'

if command -v node >/dev/null 2>&1; then
  awk '/<\/main><script>/{p=1; sub(/^.*<\/main><script>/,"")} p{if(/<\/script><\/body>/){sub(/<\/script><\/body>.*$/,""); print; exit} print}' \
    "$project_dir/PortalPage.h" | node --check -
fi

echo "Custom media portal/API contract test passed"
