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

require 'FIRMWARE_VERSION\[\] = "1\.10\.1"' MILESTONE_Core.ino 'firmware version is not 1.10.1'
require 'CONFIG_VERSION = 9' MILESTONE_Core.ino 'config schema is not 9'
require 'CUSTOM_MEDIA = 7' MILESTONE_Core.ino 'custom media view must preserve IDs 0-6'
require 'CUSTOM_MEDIA = 8' MILESTONE_Core.ino 'custom media top mode must preserve IDs 0-7'
require 'storedCycleLimit = migrateToV6 \? 0x3F : \(migrateToV9 \? 0x7F : 0xFF\)' CoreConfig.inc 'v8 cycle mask migration must leave media disabled'
require 'config\.cycleOrder\[7\] = 7' CoreConfig.inc 'v8 cycle order migration must append media'

for route in status list upload update order delete clear repair; do
  require "server\.on\(\"/api/media/${route}\"" CorePortal.inc "missing media API route: $route"
done
for route in status start push stop; do
  require "server\.on\(\"/api/stream/${route}\"" CorePortal.inc "missing live stream API route: $route"
done
require 'MEDIA_STREAM_QUEUE_FRAMES = 12' CoreMedia.inc 'live stream queue capacity changed unexpectedly'
require 'MEDIA_STREAM_MAX_BATCH_FRAMES = 4' CoreMedia.inc 'live stream batch bound missing'
require 'MEDIA_STREAM_MAX_FPS = 24' CoreMedia.inc '24 fps source ceiling missing'
require 'MEDIA_STREAM_MAX_RENDER_FPS = 15' CoreMedia.inc '400 kHz OLED streaming safety cap missing'
require 'mediaStreamRenderFps = fps > MEDIA_STREAM_MAX_RENDER_FPS' CoreMedia.inc 'stream renderer must clamp physical OLED FPS'
require 'render_fps' CorePortal.inc 'stream status must expose physical render FPS'
require 'mediaStreamActive \|\| mediaStreamUploadActive.*portalStartedMs = now' CoreRuntime.inc 'active stream must keep setup portal alive'
require 'mediaStreamQueue' CoreMedia.inc 'live stream PSRAM queue missing'
require 'return mediaUploadActive \|\| mediaStreamActive \|\| mediaStreamUploadActive;' CoreMedia.inc 'live stream must block concurrent media mutations'
require 'enqueueMediaStreamFrames' CoreMedia.inc 'live stream queue ingress missing'
require 'renderMediaStreamFrameIfDue' CoreMedia.inc 'live stream renderer missing'
require 'processMediaStreamState' CoreRuntime.inc 'live stream stale/safety processing missing'
require 'id="stream_preview"' PortalPage.h 'live stream OLED preview missing'
require 'id="stream_video" class="stream-source"' PortalPage.h 'stream decoder must stay renderable on mobile browsers'
reject 'id="stream_video"[^>]*hidden' PortalPage.h 'hidden video elements may stall decoding on mobile browsers'
require 'id="stream_file" type="file" accept="video/\*"' PortalPage.h 'live stream local video picker missing'
require 'id="stream_url" type="url"' PortalPage.h 'live stream direct URL input missing'
require '20 fps \(입력 · 최대 15fps 표시\)' PortalPage.h '20 fps source option must disclose OLED cap'
require '24 fps \(입력 · 최대 15fps 표시\)' PortalPage.h '24 fps source option must disclose OLED cap'
require '<option value="10">10 fps</option>' PortalPage.h '10 fps safe streaming default option missing'
reject '<option value="20" selected>' PortalPage.h '20 fps must not remain the default on 400 kHz I2C'
require '기기 표시.*renderFps' PortalPage.h 'portal stats must show actual device render FPS'
require 'function startLiveStream\(' PortalPage.h 'browser live stream start path missing'
require 'function pushStreamBatch\(' PortalPage.h 'browser live stream batch sender missing'
require 'YouTube 페이지 URL' PortalPage.h 'YouTube browser limitation must be explicit'
require 'bool deleteMediaEntry\(uint32_t id, String &detail\)' CoreMedia.inc 'missing detailed single media delete implementation'
python3 - <<'PY_DELETE_ORDER'
from pathlib import Path
s = Path('CoreMedia.inc').read_text()
start = s.index('bool deleteMediaEntry(uint32_t id, String &detail)')
end = s.index('\nbool mediaTransferBusy()', start)
body = s[start:end]
assert body.index('closeMediaPlayback();') < body.index('commitMediaCatalog(candidate)'), 'single media delete must close playback before catalog mutation'
assert 'file-freed:catalog-retry-committed' in body, 'delete must retry catalog commit after freeing media file'
assert 'catalog-committed:file-cleanup-pending' in body, 'catalog deletion must survive best-effort orphan cleanup failure'
assert 'mediaCatalog = candidate;' in body, 'live catalog must hide stale entry when persistence retry still fails'
PY_DELETE_ORDER
require 'media_action_status' PortalPage.h 'visible media action status missing'
require '목록 재확인 중' PortalPage.h 'delete response verification feedback missing'
require '성공 응답 후에도 ID' PortalPage.h 'delete must verify item disappearance after success response'
require "media_list.*addEventListener\('click'" PortalPage.h 'media delete must use delegated click handling'
require "data-media-action" PortalPage.h 'media delete button action marker missing'
require '다시 눌러 삭제' PortalPage.h 'two-tap delete confirmation missing'
reject 'deleteMedia\(id,name\).*confirm' PortalPage.h 'single media delete must not depend on confirm dialog'
require 'MEDIA_UPLOAD_TEMP' CoreMedia.inc 'transactional upload temp file is missing'
require 'MEDIA_INDEX_A' CoreMedia.inc 'A media index is missing'
require 'MEDIA_INDEX_B' CoreMedia.inc 'B media index is missing'
require 'MilestoneMedia::validateFile' CoreMedia.inc 'uploaded media is not fully validated'
require 'clearAllMediaFiles\(\)' CorePortal.inc 'factory reset must clear custom media'
require 'mediaTransferBusy\(\)' CoreRuntime.inc 'OTA scheduler must respect media upload mutual exclusion'
require 'mediaTransferBusy\(\)' CorePortal.inc 'portal update API must respect media upload mutual exclusion'

require 'id="media_preview"' PortalPage.h 'OLED media preview is missing'
require 'decodeGif\(' PortalPage.h 'browser GIF decoder is missing'
require 'id="media_image_file" type="file" accept="image/\*"' PortalPage.h 'dedicated photo chooser is missing'
require 'id="media_video_file" type="file" accept="video/\*"' PortalPage.h 'dedicated native video chooser is missing'
require 'id="media_any_file" type="file"' PortalPage.h 'unrestricted Files-provider fallback is missing'
reject 'id="media_any_file"[^>]*accept=' PortalPage.h 'Files-provider fallback must not apply a native accept filter'
require 'id="media_drop"' PortalPage.h 'drag-and-drop media fallback is missing'
require 'id="media_trace"' PortalPage.h 'copyable media chooser diagnostics are missing'
require 'function mediaKind\(' PortalPage.h 'MIME/extension media detection is missing'
require 'function selectMediaFile\(' PortalPage.h 'selected-file state handling is missing'
require "addEventListener\('input',handleMediaInput\)" PortalPage.h 'file input event handling is missing'
require "addEventListener\('change',handleMediaInput\)" PortalPage.h 'file change event handling is missing'
require 'function pollMediaPick\(' PortalPage.h 'file handoff event-loss fallback is missing'
require '180초 동안 파일이 브라우저에 전달되지 않았습니다' PortalPage.h 'file-provider timeout diagnosis is missing'
require 'provider-no-file' PortalPage.h 'empty cancel must be diagnosed as a provider no-file result'
require '모바일 데이터 또는 인터넷' PortalPage.h 'iCloud/mobile-data recovery guidance is missing'
require "phase:'파일 선택 중'" PortalPage.h 'explicit media phase tracking is missing'
reject 'post-cancel-watch' PortalPage.h 'empty cancel must not leave the user waiting after the provider returned files=0'
require 'function probeMediaFile\(' PortalPage.h 'selected File readability probe is missing'
require 'function handleMediaDrop\(' PortalPage.h 'drag-and-drop file handling is missing'
require 'function openVideoFile\(' PortalPage.h 'bounded video initialization is missing'
require 'function waitForVideo\(' PortalPage.h 'video load timeout handling is missing'
require 'buildMsm\(' PortalPage.h 'MSM1 browser encoder is missing'
require 'x\+\(y>>3\)\*128' PortalPage.h 'U8g2 page-major packing contract is missing'
require 'const MEDIA_MAX_FRAMES=1024' PortalPage.h 'browser converter must use the MSM1 1024-frame ceiling'
require 'MEDIA_MAX_FRAMES/fps' PortalPage.h 'video duration must be bounded by the 1024-frame ceiling'
reject 'Math\.min\(300' PortalPage.h 'legacy 300-frame conversion ceiling must be removed'
reject 'frames\.length<300' PortalPage.h 'legacy 300-frame GIF decode ceiling must be removed'
reject 'id="media_duration"[^>]*max="40"' PortalPage.h 'legacy 40-second media duration ceiling must be removed'
reject 'Math\.min\(40,Math\.max\(1,Number\(val\('media_duration'\)' PortalPage.h 'legacy 40-second JS clamp must be removed'

if command -v node >/dev/null 2>&1; then
  awk '/<\/main><script>/{p=1; sub(/^.*<\/main><script>/,"")} p{if(/<\/script><\/body>/){sub(/<\/script><\/body>.*$/,""); print; exit} print}' \
    "$project_dir/PortalPage.h" | node --check -
fi

echo "Custom media portal/API contract test passed"
