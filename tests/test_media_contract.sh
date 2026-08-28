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

require 'CONFIG_VERSION = 12' MILESTONE_Core.ino 'config schema is not 12'
require 'CUSTOM_MEDIA = 7' MILESTONE_Core.ino 'custom media view must preserve IDs 0-6'
require 'CUSTOM_MEDIA = 8' MILESTONE_Core.ino 'custom media top mode must preserve IDs 0-7'
require 'storedCycleLimit = migrateToV6 \? 0x3F : \(migrateToV9 \? 0x7F : 0xFF\)' CoreConfig.inc 'v8 cycle mask migration must leave media disabled'
require 'config\.cycleOrder\[7\] = 7' CoreConfig.inc 'v8 cycle order migration must append media'

for route in status list upload update order delete clear repair; do
  require "server\.on\(\"/api/media/${route}\"" CorePortal.inc "missing media API route: $route"
done
for route in status start push finish stop; do
  require "server\.on\(\"/api/stream/${route}\"" CorePortal.inc "missing live stream API route: $route"
done
require 'server\.on\("/stream", HTTP_GET, handleStreamPortal\)' CorePortal.inc 'dedicated stream page route missing'
require 'MILESTONE_STREAM_HTML' CorePortal.inc 'dedicated stream page response missing'
require 'server\.uri\(\) == "/"' CorePortal.inc 'general portal root must stay lightweight during STREAM_MODE'
require 'MEDIA_STREAM_QUEUE_FRAMES = 96' CoreMedia.inc 'color-capable PSRAM stream queue missing'
require 'MEDIA_STREAM_MAX_PUSH_FRAMES = 12' CoreMedia.inc 'amortized color stream push missing'
require 'MEDIA_STREAM_PREBUFFER_FRAMES = 24' CoreMedia.inc 'low-latency color stream prebuffer missing'
require 'MEDIA_STREAM_REBUFFER_FRAMES = 12' CoreMedia.inc 'color stream rebuffer threshold missing'
require 'mediaStreamSourceEnded' CoreMedia.inc 'stream source-end state missing'
require 'source-complete' CoreMedia.inc 'device must stop only after the final queued frame drains'
require 'mediaStreamQueueCount <= MEDIA_STREAM_REBUFFER_FRAMES' CoreMedia.inc 'deep healthy stream buffers must not trigger the stale sender timeout'
require 'mediaStreamPhase == MediaStreamPhase::BUFFERING && mediaStreamQueueCount > 0' CoreMedia.inc 'EOF must drain short sources below the normal prebuffer threshold'
require 'MEDIA_STREAM_MAX_FPS = 24' CoreMedia.inc '24 fps experimental ceiling missing'
require 'MEDIA_STREAM_WARNING_C = 75\.0f' CoreMedia.inc 'stream warning threshold missing'
require 'MEDIA_STREAM_STOP_C = 80\.0f' CoreMedia.inc '80 C stream stop threshold missing'
require 'MEDIA_STREAM_EMERGENCY_C = 85\.0f' CoreMedia.inc '85 C emergency threshold missing'
require 'psramFound\(\)' CoreMedia.inc 'stream buffer must require PSRAM'
require 'MALLOC_CAP_SPIRAM' CoreMedia.inc 'stream queue must allocate from PSRAM'
require 'releaseMediaPlaybackBuffersForStream\(\)' CoreMedia.inc 'stored-media playback buffers must be released for streaming'
require 'HTTPRaw &raw = server\.raw\(\)' CorePortal.inc 'raw request streaming callback missing'
require 'RAW_START' CorePortal.inc 'raw stream start handling missing'
require 'RAW_WRITE' CorePortal.inc 'raw stream chunk handling missing'
require 'RAW_END' CorePortal.inc 'raw stream completion handling missing'
require 'server\.collectHeaders\(headerKeys, 5\)' CorePortal.inc 'stream metadata headers must be explicitly collected'
require 'X-MILESTONE-Session' CorePortal.inc 'raw stream session header handling missing'
require 'X-MILESTONE-Seq' CorePortal.inc 'raw stream sequence header handling missing'
require 'X-MILESTONE-Count' CorePortal.inc 'stream frame-count header handling missing'
require 'X-MILESTONE-Bytes' CorePortal.inc 'encoded stream byte-count header handling missing'
require 'parseUintHeader\(' CorePortal.inc 'raw stream metadata must not depend on WebServer query parsing'
require 'invalid-stream-metadata' CorePortal.inc 'raw stream metadata parse failures must be diagnosable'
require 'invalid-frame-count' CorePortal.inc 'raw stream frame-count failures must be diagnosable'
require 'application/octet-stream' CorePortal.inc 'binary stream ACK/content type missing'
require 'mediaStreamRawDuplicate' CoreMedia.inc 'idempotent stream retry state missing'
require 'MEDIA_STREAM_PACKET_MAX_BYTES' CoreMedia.inc 'bounded encoded stream staging area missing'
require 'MilestoneMedia::decodeFrameSized' CoreMedia.inc 'live stream must decode mono or color MSM frame records before queueing'
require 'mediaStreamDecodeFrame' CoreMedia.inc 'live delta stream reference frame missing'
require 'seq == mediaStreamLastPushSeq' CoreMedia.inc 'duplicate stream sequence retry handling missing'
reject 'mediaStreamUploadBuffer' CoreMedia.inc 'legacy stream staging buffer must be removed'
require 'heap_caps_malloc\(UPDATE_DOWNLOAD_BUFFER_BYTES, MALLOC_CAP_INTERNAL' CoreUpdate.inc 'OTA buffer must be allocated only during OTA'
reject 'uint8_t updateDownloadBuffer\[UPDATE_DOWNLOAD_BUFFER_BYTES\]' MILESTONE_Core.ino 'OTA transfer buffer must not occupy permanent SRAM during streaming'
reject 'MEDIA_STREAM_MAX_RENDER_FPS' CoreMedia.inc 'legacy fixed 15fps render cap must be removed'
reject 'mediaStreamRenderFps' CoreMedia.inc 'legacy render-fps clamp state must be removed'
require 'updateDisplayArea\(' CoreMedia.inc 'SH1107 partial full-buffer update path missing'
require 'renderMediaStreamFrameIfDue' CoreMedia.inc 'live stream renderer missing'
require 'MEDIA_STREAM_NETWORK_SLICE_MS = 12' CoreMedia.inc 'OLED renderer must reserve network service time'
require 'mediaStreamNextRenderMs' CoreMedia.inc 'source timeline and OLED service clock must be separated'
require 'rowFirstTile\[16\]' CoreMedia.inc 'stream partial refresh must track horizontal dirty tile spans'
require '\+\+mediaStreamFpsWindowFrames' CoreMedia.inc 'actual stream FPS counter must advance on displayed frames'
require 'MEDIA_STREAM_MIN_FREE_HEAP' CoreMedia.inc 'stream heap guard missing'
require 'MEDIA_STREAM_MIN_LARGEST_HEAP_BLOCK' CoreMedia.inc 'stream contiguous-heap guard missing'
require 'MEDIA_STREAM_MIN_STACK_FREE' CoreMedia.inc 'stream stack guard missing'
require 'prefs\.putBool\("stream_open", true\)' CoreMedia.inc 'stream crash marker must be armed at stream start'
require 'previous reset interrupted live stream' CoreRuntime.inc 'interrupted stream reset must be diagnosed on next boot'
require 'void enterMediaStreamPerformanceMode\(\)' CoreRuntime.inc 'stream performance-mode entry missing'
require 'void leaveMediaStreamPerformanceMode\(\)' CoreRuntime.inc 'stream performance-mode exit missing'
require 'void processMediaStreamMode\(\)' CoreRuntime.inc 'isolated stream loop missing'
require 'processMediaStreamNetworkOnly\(\)' CoreRuntime.inc 'stream-only network servicing missing'
require 'refreshChipTemperature\(true\)' CoreRuntime.inc 'forced low-rate stream thermal sampling missing'
require 'server\.enableDelay\(false\)' CoreRuntime.inc 'stream network loop tuning missing'
require 'esp_sntp_stop\(\)' CoreNetwork.inc 'bounded NTP service stop helper missing'
require 'stopNtpService\(\)' CoreRuntime.inc 'STREAM_MODE must leave SNTP in a clean stopped state'
require 'portalStartedMs = millis\(\);' CoreRuntime.inc 'stream mode must pin setup portal activity'
require 'bool rejectPortalWorkDuringStream\(\)' CorePortal.inc 'general portal work must be isolated from STREAM_MODE'
require 'stream_active.*true' CorePortal.inc 'status endpoint must use a tiny response during STREAM_MODE'
require 'if \(rejectPortalWorkDuringStream\(\)\) return;' CorePortal.inc 'heavy portal handlers must fail fast during STREAM_MODE'
require 'if \(mediaStreamActive\) \{' CoreRuntime.inc 'stream mode early branch missing'
python3 - <<'PY_STREAM_LOOP'
from pathlib import Path
s = Path('CoreRuntime.inc').read_text()
start = s.index('void loopFirmware()')
body = s[start:]
first_stream = body.index('if (mediaStreamActive)')
network = body.index('processNetwork();')
second_stream = body.index('if (mediaStreamActive)', first_stream + 1)
background = [body.index(name) for name in ['processFirmwareUpdate();','processDiagnostics();','processCycle();','processDisplay();','processLed();']]
assert first_stream < network, 'active stream must bypass normal network/background loop immediately'
assert network < second_stream < min(background), 'stream started by HTTP request must bypass every normal background subsystem'
PY_STREAM_LOOP
require 'location\.href=.*/stream' PortalPage.h 'main portal must link to dedicated stream page'
reject 'id="stream_video"' PortalPage.h 'legacy embedded live stream decoder must be removed from main portal'
reject 'function startLiveStream\(' PortalPage.h 'legacy embedded stream sender must be removed from main portal'
require 'id="file" type="file" accept="video/\*"' StreamPage.h 'dedicated stream local video picker missing'
require 'id="url" type="url"' StreamPage.h 'direct media URL field missing'
require '<option value="20" selected>20 fps</option>' StreamPage.h '20 fps high-performance target must be the stream-page default'
require '<option value="24">24 fps · Experimental</option>' StreamPage.h '24 fps experimental option missing'
require 'let FRAME_BYTES=2048' StreamPage.h 'dynamic mono/color stream frame size contract missing'
require 'CHUNK_FRAMES=128' StreamPage.h 'browser-side chunked preconversion store missing'
require 'INITIAL_FILL_FRAMES=24' StreamPage.h 'browser color initial fill missing'
require 'LOW_WATERMARK=48' StreamPage.h 'stream sender proactive watermark missing'
require 'REFILL_TARGET=88' StreamPage.h 'stream sender refill target missing'
require 'REFILL_FLOOR=72' StreamPage.h 'stream sender refill floor missing'
require 'QUEUE_SAFETY_FRAMES=4' StreamPage.h 'stream sender must reserve ACK transit safety frames'
require 'HTTP RTT' StreamPage.h 'stream status must expose request RTT for supply diagnostics'
require 'MAX_PUSH_FRAMES=12' StreamPage.h 'browser color raw push bound missing'
require 'PUSH_TIMEOUT_MS=10000' StreamPage.h 'stream POST needs a bounded transport timeout distinct from device stale detection'
require 'new AbortController\(\)' StreamPage.h 'stream POST timeout must abort a stalled fetch'
reject 'observedDrainFps\(\)' StreamPage.h 'displayed FPS must not be mistaken for source-timeline queue consumption'
require '최근 송신' StreamPage.h 'stream sender must show a recent rate instead of a lifetime average'
require 'async function convertAll\(' StreamPage.h 'full browser preconversion path missing'
require 'MAX_PRECONVERT_BYTES=48\*1024\*1024' StreamPage.h 'live preconversion needs a browser memory ceiling'
require 'Math\.ceil\(video\.duration\*fps\)' StreamPage.h 'full-video frame count must derive from duration and fps'
require "'Content-Type':'application/octet-stream'" StreamPage.h 'stream page must send raw binary bodies'
require "'X-MILESTONE-Session':String\(state\.session\)" StreamPage.h 'stream page must send session metadata as a collected header'
require "'X-MILESTONE-Seq':String\(seq\)" StreamPage.h 'stream page must send sequence metadata as a collected header'
require "'X-MILESTONE-Count':String\(count\)" StreamPage.h 'stream page must send frame count as a collected header'
require "'X-MILESTONE-Bytes':String\(body\.byteLength\)" StreamPage.h 'stream page must declare encoded payload length'
require 'function deltaRle\(' StreamPage.h 'live XOR-RLE encoder missing'
require 'function encodeBatch\(' StreamPage.h 'live encoded frame-record batching missing'
require 'RAW 대비' StreamPage.h 'stream UI must expose payload compression ratio'
reject '/api/stream/push\?session=' StreamPage.h 'raw stream metadata must not rely on query arguments in Arduino-ESP32 WebServer 3.3.11'
reject 'FormData' StreamPage.h 'stream page must not use multipart/FormData'
require 'function parseAck\(' StreamPage.h 'binary stream ACK parser missing'
require 'async function pushRaw\(' StreamPage.h 'paced raw stream sender missing'
require 'finishAndDrain' StreamPage.h 'browser must let the device drain its real queue instead of guessing the tail delay'
require 'function estimatedQueue\(' StreamPage.h 'watermark sender must estimate queue drain between ACKs'
require 'queue>=REFILL_FLOOR' StreamPage.h 'watermark refill loop missing'
reject 'LEAD_FRAMES' StreamPage.h 'legacy clock-lead pacing must stay removed'
reject 'STEADY_PUSH_FRAMES' StreamPage.h 'legacy two-frame ACK-locked pacing must stay removed'
require 'MEDIA_STREAM_PREBUFFER_FRAMES = 24' CoreMedia.inc 'device must accumulate a low-latency color queue before playback'
require 'MEDIA_STREAM_REBUFFER_FRAMES = 12' CoreMedia.inc 'device must rebuild the color queue after an underrun'
require "waitForVideo\(video,\['seeked','loadeddata'\]" StreamPage.h 'stream preconversion must tolerate first-frame loadeddata/seeked behavior'
require 'lastError' StreamPage.h 'stream sender must retry transport exceptions with the same sequence/body'
require '!mediaStreamRawActive' CoreMedia.inc 'an in-flight large raw batch must not trip the stale-sender guard'
require 'MEDIA_STREAM_STALE_MS = 15000UL' CoreMedia.inc 'device stale timeout must not race the browser POST timeout'
require 'renderMediaStreamFrameIfDue\(millis\(\)\)' CorePortal.inc 'raw request service must preserve OLED deadlines during a large refill'
require 'scheduledRenderMs \+ mediaStreamRenderIntervalMs' CoreMedia.inc 'OLED pacing must advance from its prior deadline instead of accumulating loop jitter'
require 'STREAM_MODE stop: reason=' CoreMedia.inc 'stream stop diagnostics must expose the terminal reason and counters'
require 'MEDIA_STREAM_MAX_PUSH_FRAMES = 12' CoreMedia.inc 'device raw push bound must match the color browser batch'
require 'COLOR_FRAME_BYTES' CoreMedia.h 'stored media color frame contract missing'
require 'loadRgb332' CoreMedia.inc 'stored/live RGB332 output path missing'
require 'media_monochrome' PortalPage.h 'portal media monochrome/color control missing'
require 'drawMediaStreamStartingScreen\(\)' CoreRuntime.inc 'STREAM_MODE entry must replace a stale portal/T frame immediately'
require 'wifiTestState == WifiTestState::CONNECTING' CorePortal.inc 'stream start must block only an active Wi-Fi connection test'
reject 'wifiTestState != WifiTestState::IDLE' CorePortal.inc 'completed Wi-Fi SUCCESS/FAILED states must not block streaming'
require 'pauseNtpForMediaStream\(\)' CorePortal.inc 'stream start must safely pause an active NTP transaction'
require 'resumeDeferredNtpAfterMediaStream\(\)' CoreRuntime.inc 'stream exit must resume a time synchronization paused for playback'
require 'NTP paused for STREAM_MODE' CorePortal.inc 'deferred NTP transition needs an observable pause event'
require 'NTP resumed after STREAM_MODE' CorePortal.inc 'deferred NTP transition needs an observable resume event'
require 'mediaUpdateIndicatorDrawn' CoreMedia.inc 'MEDIA playback must acknowledge and later clear the visible update marker'
require 'stale-stream-session' CorePortal.inc 'stale stream pages must not stop a newer stream session'
require '이미 다른 스트리밍 세션' CorePortal.inc 'a second stream start must not silently replace the active session'
require 'YouTube 페이지 URL' StreamPage.h 'YouTube browser limitation must be explicit'
if command -v node >/dev/null 2>&1; then
  node "$project_dir/tests/test_stream_page.js" "$project_dir/StreamPage.h"
fi
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
require 'shouldRotateStoredItem' CoreRuntime.inc 'stored animations must rotate only at a complete playback boundary'
require 'mediaEnabledItemCount\(\) > 1U' CoreMedia.inc 'a single stored animation must loop without being reopened by the catalog timer'
require 'markMediaPlaybackFailure' CoreMedia.inc 'mid-playback frame failures must be diagnosed and recovered'
require 'MEDIA_PLAYBACK_RECOVERY_MS = 5000UL' CoreMedia.inc 'stored playback retry must use a bounded backoff'
require 'reset_reason' CorePortal.inc 'portal status must expose the hardware reset reason'
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
  portal_script_source=${MILESTONE_RENDERED_MEDIA_PORTAL:-$project_dir/PortalPage.h}
  awk '/<\/main><script>/{p=1; sub(/^.*<\/main><script>/,"")} p{if(/<\/script><\/body>/){sub(/<\/script><\/body>.*$/,""); print; exit} print}' \
    "$portal_script_source" | node --check -
fi


echo "Custom media portal/API contract test passed"
