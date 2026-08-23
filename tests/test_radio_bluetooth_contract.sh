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

require 'FIRMWARE_VERSION\[\] = "2\.2\.19"' MILESTONE_Core.ino 'firmware version is not 2.2.19'
require 'CONFIG_VERSION = 10' MILESTONE_Core.ino 'configuration schema is not 10'
require 'bool fixedApSecurity = false;' MILESTONE_Core.ino 'fixed AP security must default off'
require 'String fixedApPassword;' MILESTONE_Core.ino 'fixed AP password setting missing'
require 'bool bluetoothNowPlaying = false;' MILESTONE_Core.ino 'Bluetooth Now Playing must default off'
require 'prefs\.putBool\("ap_fixed", config\.fixedApSecurity\)' CoreConfig.inc 'fixed AP enable flag is not persisted'
require 'putStringVerified\("ap_pass", config\.fixedApPassword\)' CoreConfig.inc 'fixed AP password is not persisted transactionally'
require 'prefs\.putBool\("ble_media", config\.bluetoothNowPlaying\)' CoreConfig.inc 'Bluetooth setting is not persisted'
require 'const bool migrateToV10 = version < 10;' CoreConfig.inc 'schema 10 migration gate missing'
require 'prefs\.putUChar\("now_layout"' CoreConfig.inc 'NOW layout is not persisted'
require 'config\.fixedApSecurity = false;' CoreConfig.inc 'older schemas must default fixed AP mode off'
require 'config\.bluetoothNowPlaying = false;' CoreConfig.inc 'older schemas must default Bluetooth off'

require 'apPassword = randomHex\(8\)' CoreNetwork.inc 'legacy random setup-AP password path must remain intact'
require 'config\.fixedApSecurity && apPassword\.length\(\) == 0' CoreNetwork.inc 'explicit open-AP branch missing'
require 'WiFi\.softAP\(AP_SSID\)' CoreNetwork.inc 'open setup AP creation missing'
require 'WiFi\.softAP\(AP_SSID, apPassword\.c_str\(\)\)' CoreNetwork.inc 'password-protected setup AP creation missing'
require 'OPEN NETWORK' CoreDisplay.inc 'OLED must clearly identify an open setup AP'

require '89D3502B|0x2b, 0x50, 0xd3, 0x89' CoreBluetooth.inc 'Apple Media Service UUID missing'
require '2F7CABCE|0xce, 0xab, 0x7c, 0x2f' CoreBluetooth.inc 'AMS Entity Update UUID missing'
require 'C6B2F38C|0x8c, 0xf3, 0xb2, 0xc6' CoreBluetooth.inc 'AMS Entity Attribute UUID missing'
require 'BLE_HS_ADV_TYPE_SVC_DATA_UUID128|0x15' CoreBluetooth.inc 'AMS service solicitation advertising is missing'
require 'setShortName\("MILESTON"\)' CoreBluetooth.inc 'passive BLE scans need a name in the primary advertisement'
require 'scanResponse\.setName\(BLE_DEVICE_NAME\)' CoreBluetooth.inc 'active BLE scans need the complete MILESTONE name'
require 'MILESTONE_BLE_AMS_RUNTIME_V5' CoreBluetooth.inc 'compiled BLE runtime marker missing'
require 'requires an Arduino-ESP32 build with NimBLE enabled' MILESTONE_Core.ino 'NOW must fail compilation instead of shipping a BLE stub'
require 'bluetoothNowPlayingVisible\(\)' CoreBluetooth.inc 'Bluetooth music overlay visibility gate missing'
python3 - <<'PY_AMS_PARSE'
from pathlib import Path
s = Path('CoreBluetooth.inc').read_text()
start = s.index('void applyAmsNotification(')
end = s.index('\nvoid processBluetoothNowPlaying()', start)
body = s[start:end]
assert body.count('value += static_cast<char>(packet.data[i])') == 1, 'AMS notification payload must be appended exactly once'
PY_AMS_PARSE
require 'initializeBluetoothNowPlaying\(\)' CoreBluetooth.inc 'Bluetooth initialization missing'
require 'setScanResponse\(true\)' CoreBluetooth.inc 'complete MILESTONE name scan response must be explicitly enabled'
require 'setAdvertisementType\(BLE_GAP_CONN_MODE_UND\)' CoreBluetooth.inc 'AMS advertising must be explicitly connectable'
require 'setScanFilter\(false, false\)' CoreBluetooth.inc 'AMS advertising must accept iPhone scan and connection requests'
require 'BLE_ADVERTISING_RETRY_MS' CoreBluetooth.inc 'Bluetooth advertising retry cadence missing'
require 'bluetoothNowPlayingAdvertising\(\)' CoreBluetooth.inc 'actual Bluetooth advertising state missing'
require 'shutdownBluetoothNowPlaying\(\)' CoreBluetooth.inc 'Bluetooth shutdown missing'
require 'suspendBluetoothNowPlaying\(\)' CoreBluetooth.inc 'Bluetooth stream suspension missing'
require 'resumeBluetoothNowPlaying\(\)' CoreBluetooth.inc 'Bluetooth stream resume missing'
require 'isolateBluetoothForFirmwareOperation\(\)' CoreBluetooth.inc 'firmware HTTPS Bluetooth isolation missing'
require 'restoreBluetoothAfterFirmwareOperation\(\)' CoreBluetooth.inc 'firmware HTTPS Bluetooth restoration missing'
require 'if \(bluetoothFirmwareOperationIsolated\) return;' CoreBluetooth.inc 'Bluetooth service must stay stopped during firmware HTTPS'
require 'const bool bluetoothIsolated = isolateBluetoothForFirmwareOperation\(\)' CoreUpdate.inc 'OTA install must capture Bluetooth isolation result'
require 'const bool bluetoothIsolated = isolateBluetoothForFirmwareOperation\(\)' CoreRuntime.inc 'manifest checks must capture Bluetooth isolation result'
require 'if \(!bluetoothIsolated\)' CoreUpdate.inc 'OTA install must reject incomplete Bluetooth isolation'
require 'if \(!bluetoothIsolated\)' CoreRuntime.inc 'manifest checks must wait for safe Bluetooth isolation'
require 'bluetoothDisconnectedEvent && bluetoothEventConnHandle == connHandle' CoreBluetooth.inc 'BLE shutdown must wait for the matching GAP disconnect confirmation'
require 'getPeerDevices\(false\)' CoreBluetooth.inc 'OTA isolation must inspect NimBLE server peers when app state has not caught up'
require 'getConnectedCount\(\) == 0' CoreBluetooth.inc 'OTA isolation must verify the live NimBLE connection count'
require 'bluetoothNowPlayingHasLiveConnection\(\)' CoreBluetooth.inc 'automatic OTA checks need the NimBLE live-link state'
require 'bluetoothServer->getConnectedCount\(\) > 0' CoreBluetooth.inc 'automatic OTA checks must inspect the NimBLE peer count'
require 'clearBluetoothPendingEvents\(\)' CoreBluetooth.inc 'stale GAP and AMS events must be cleared after OTA isolation'
require 'BLE_DISCONNECT_TIMEOUT_MS' CoreBluetooth.inc 'BLE disconnect confirmation must be bounded'
require 'BLE_SECURITY_TIMEOUT_MS = 15000UL' CoreBluetooth.inc 'BLE security wait must be bounded'
require 'ble_gap_conn_find\(bluetoothNowPlaying\.connHandle, &connection\)' CoreBluetooth.inc 'live encrypted connection state must be polled'
require 'connection\.sec_state\.encrypted' CoreBluetooth.inc 'security recovery must verify actual link encryption'
require 'shouldIgnoreLateBluetoothEncryptionFailure' CoreBluetooth.inc 'a delayed security timeout must not tear down an already encrypted link'
require 'BLE_HS_ETIMEOUT' CoreBluetooth.inc 'the delayed security exception must remain limited to timeout events'
require 'BLE late encryption timeout ignored after link confirmation' CoreBluetooth.inc 'ignored delayed security timeouts must remain observable'
require 'security-timeout' CoreBluetooth.inc 'stuck security must expose a concrete timeout error'
require 'disconnect\(timedOutHandle, BLE_ERR_REM_USER_CONN_TERM\)' CoreBluetooth.inc 'stuck security must disconnect for a clean retry'
require 'bluetooth_security_elapsed_ms' CorePortal.inc 'portal status must expose security wait duration'
require 'bluetooth_security_elapsed_ms.*초' PortalPage.h 'portal must render the current security wait duration'
require 'stack deinit refused' CoreBluetooth.inc 'BLE shutdown timeout must refuse unsafe stack deinit'
require 'BLE quiesced without deinit for firmware HTTPS' CoreBluetooth.inc 'firmware HTTPS must retain the initialized BLE stack'
python3 - <<'PY_BLE_OTA_QUIESCE'
from pathlib import Path
import re
s = Path('CoreBluetooth.inc').read_text()
start = s.index('bool isolateBluetoothForFirmwareOperation()')
end = s.index('\nvoid restoreBluetoothAfterFirmwareOperation()', start)
body = s[start:end]
assert 'waitForBluetoothDisconnectBeforeShutdown()' in body
assert 'bluetoothAdvertising->stop()' in body
assert not re.search(r'^\s*BLEDevice::deinit', body, re.M), 'OTA isolation must not delete live NimBLE objects'
PY_BLE_OTA_QUIESCE
require 'FIRMWARE_OPERATION_RTC_MAGIC' CoreUpdate.inc 'firmware operations need an RTC reset breadcrumb'
require 'manifest-tls' CoreUpdate.inc 'manifest reset stage is not diagnosable'
require 'firmware_interrupted_stage' CorePortal.inc 'portal must expose interrupted firmware operation stage'
python3 - <<'PY_BLE_SHUTDOWN'
from pathlib import Path
s = Path('CoreBluetooth.inc').read_text()
start = s.index('bool shutdownBluetoothNowPlaying()')
end = s.index('\nbool isolateBluetoothForFirmwareOperation()', start)
body = s[start:end]
wait = body.index('waitForBluetoothDisconnectBeforeShutdown()')
deinit = body.index('BLEDevice::deinit(false)')
assert wait < deinit, 'BLEDevice deinit must happen only after disconnect confirmation'
PY_BLE_SHUTDOWN
require 'restoreBluetoothAfterFirmwareOperation\(\);' CoreUpdate.inc 'firmware failure paths must restore Bluetooth'
require 'restoreBluetoothAfterFirmwareOperation\(\);' CoreRuntime.inc 'successful manifest checks must restore Bluetooth'
require 'suspendBluetoothNowPlaying\(\);' CoreRuntime.inc 'STREAM_MODE entry must suspend Bluetooth'
require 'resumeBluetoothNowPlaying\(\);' CoreRuntime.inc 'STREAM_MODE exit must resume Bluetooth'
require 'processBluetoothNowPlaying\(\);' CoreRuntime.inc 'main loop must service Bluetooth metadata'
require 'if \(bluetoothNowPlayingConfigured\(\)\)' CoreRuntime.inc 'Bluetooth stack must be gated by the active profile policy'
require 'MILESTONE_BLUETOOTH_ALWAYS_ON \|\| config\.bluetoothNowPlaying' CoreBluetooth.inc 'NOW must force Bluetooth on without mutating saved CORE settings'
require '#define MILESTONE_HAS_BLUETOOTH 0' FirmwareProfile.h 'CORE and MEDIA must exclude Bluetooth'
require '#define MILESTONE_PROFILE_NOW 3' FirmwareProfile.h 'NOW profile identifier missing'

require 'void drawBluetoothNowPlayingScreen\(\)' CoreDisplay.inc 'Now Playing OLED renderer missing'
if [[ $(grep -Fc 'drawStatusIcon(112, 0);' "$project_dir/CoreDisplay.inc") -lt 2 ]]; then
  echo 'FAIL: NOW playback and waiting screens must both acknowledge the visible update indicator' >&2
  exit 1
fi
require 'if \(bluetoothNowPlayingVisible\(\)\)' CoreDisplay.inc 'Now Playing must be an overlay rather than a ninth persistent view'
require 'u8g2_font_unifont_t_japanese2' CoreDisplay.inc 'NOW metadata Japanese font routing missing'
require 'utf8ContainsHangul' CoreDisplay.inc 'NOW metadata Korean font routing missing'
require 'drawUTF8X2' CoreDisplay.inc 'NOW title must render larger than the artist'
require 'bluetoothNowPlayingAlbum\(\)' CoreArtwork.inc 'artwork lookup still needs AMS album metadata'
require 'AMS_TRACK_ALBUM' CoreBluetooth.inc 'album layout needs AMS album metadata subscription'
require 'AMS_CONN_INTERVAL_MIN = 32' CoreBluetooth.inc 'AMS connection interval is not coexistence-safe during artwork HTTPS'
require 'AMS_CONN_INTERVAL_MAX = 64' CoreBluetooth.inc 'AMS maximum connection interval is too sparse during artwork HTTPS'
require 'AMS_CONN_LATENCY = 0' CoreBluetooth.inc 'AMS peripheral latency can starve BLE during artwork HTTPS'
require 'AMS_CONN_TIMEOUT = 1200' CoreBluetooth.inc 'AMS supervision timeout is too short for Wi-Fi TLS coexistence'
require 'event->disconnect\.reason' CoreBluetooth.inc 'BLE disconnect reason is not captured for coexistence diagnosis'
require 'bluetooth_disconnect_reason' CorePortal.inc 'portal must expose the last BLE disconnect reason'
require 'MILESTONE_NOW_ARTWORK_RUNTIME_V1' CoreArtwork.inc 'NOW artwork runtime marker missing'
require 'xTaskCreate\(nowArtworkWorker' CoreArtwork.inc 'artwork lookup must stay off the cooperative main loop'
require 'NOW_ART_TRACK_SETTLE_MS' CoreArtwork.inc 'rapid track changes need a settle/debounce window'
require 'NOW_ART_MAX_LOOKUP_BYTES = 48UL \* 1024UL' CoreArtwork.inc 'MusicBrainz response must have a hard streamed byte limit'
require 'NOW_ART_MAX_CANDIDATES = 3' CoreArtwork.inc 'MusicBrainz/Cover Art fallback must remain bounded'
require 'limit=3&query=release%3A' CoreArtwork.inc 'album lookup must collect bounded alternate release groups'
require 'limit=3&query=recording%3A' CoreArtwork.inc 'title and artist fallback lookup is missing'
require 'itunes\.apple\.com/search' CoreArtwork.inc 'localized Apple Music artwork lookup is missing'
require 'nowArtworkReadAppleUrl' CoreArtwork.inc 'Apple artwork JSON must be parsed as a bounded stream'
require 'feedAppleArtworkUrlParser' CoreArtwork.inc 'host-tested Apple artwork URL parser is not shared with firmware'
reject 'appleHttp\.getString\(\)' CoreArtwork.inc 'Apple Search JSON must not be copied into an unbounded internal-heap String'
require 'Apple search TLS client is now destroyed before the image TLS starts' CoreArtwork.inc 'Apple search and image TLS lifetimes overlap internal RAM'
require 'if \(!success\) \{' CoreArtwork.inc 'MusicBrainz query allocations must remain lazy after Apple success'
require 'artwork_apple_code' CorePortal.inc 'NOW portal must expose Apple Search diagnosis separately'
require 'NOW_ART_LOOKUP_ATTEMPTS = 5' CoreArtwork.inc 'transient artwork transport failures need the observed fourth-attempt recovery window'
require 'NOW_ART_APPLE_ATTEMPTS = 1' CoreArtwork.inc 'Apple transport failure must fall through before monopolizing the shared budget'
require 'NOW_ART_TLS_HANDSHAKE_TIMEOUT_SEC = 3UL' CoreArtwork.inc 'artwork TLS handshake can overrun a short-track budget'
require 'esp_coex_preference_set\(ESP_COEX_PREFER_WIFI\)' CoreArtwork.inc 'artwork TLS needs temporary Wi-Fi coexistence priority'
require 'esp_coex_preference_set\(ESP_COEX_PREFER_BALANCE\)' CoreArtwork.inc 'artwork worker must restore balanced radio coexistence'
require 'NOW_ART_LOOKUP_TIMEOUT_MS = 7000UL' CoreArtwork.inc 'NOW artwork read timeout must remain bounded for short tracks'
require 'NOW_ART_LOOKUP_RETRY_MS = 1200UL' CoreArtwork.inc 'MusicBrainz retry must respect per-client request spacing'
require 'NOW_ART_TRANSPORT_BACKOFF_MS = 250UL' CoreArtwork.inc 'artwork transport recovery starts too slowly for normal track lengths'
require 'NOW_ART_WORKER_BUDGET_MS = 15000UL' CoreArtwork.inc 'one track artwork lookup needs a short shared wall-time budget'
require 'nowArtworkWorkerBudgetExpired' CoreArtwork.inc 'artwork provider retries must share the worker budget'
require 'NowArtworkWorkerResult::LOOKUP_TIMEOUT' CoreArtwork.inc 'artwork budget exhaustion must remain distinguishable from cancellation'
require 'NOW artwork loaded elapsed_ms=' CoreArtwork.inc 'successful artwork latency must remain observable'
require 'total_elapsed_ms=' CoreArtwork.inc 'track-change-to-artwork latency must remain observable'
require 'elapsed_ms=' CoreArtwork.inc 'failed artwork latency must remain observable'
require 'nowArtworkReadJpegWithRetries' CoreArtwork.inc 'Apple and Cover Art image transports need bounded retry recovery'
require 'NOW artwork download retry after code=' CoreArtwork.inc 'image transport retries must remain observable'
require 'NOW Apple artwork retry after code=' CoreArtwork.inc 'Apple Search transport retries must remain observable'
require 'shouldRetryTransientHttpFailure' CoreArtwork.inc 'artwork retries must distinguish transient transport failures from definitive responses'
require 'nowArtworkWaitForTransportRetry' CoreArtwork.inc 'artwork retries must be cancellable during backoff'
require '!elapsed\(millis\(\), lastLookupRequestMs, NOW_ART_LOOKUP_RETRY_MS\)' CoreArtwork.inc 'album-to-recording fallback is not rate paced'
require 'nowArtworkReadReleaseGroups\(HTTPClient &http' CoreArtwork.inc 'MusicBrainz XML candidates must be parsed as a bounded stream'
require 'feedMusicBrainzReleaseGroupParser' CoreArtwork.inc 'MusicBrainz release-group parsing must ignore XML attribute order'
require 'nowArtworkWorkerGeneration' CoreArtwork.inc 'rapid track changes need cancellable artwork generations'
require 'temperature-paused' CoreArtwork.inc 'artwork must pause under NOW thermal pressure'
require 'bluetoothInitialNetworkGate = provisioned' CoreRuntime.inc 'initial BLE advertising must wait for the first saved Wi-Fi attempt'
require 'initial Wi-Fi attempt settled; enabling BLE Now Playing' CoreBluetooth.inc 'initial BLE/Wi-Fi coexistence gate must be observable'
reject 'const String xml = http\.getString\(\)' CoreArtwork.inc 'MusicBrainz XML must not be copied into an unbounded internal-heap String'
require 'NOW_ART_WORKER_STACK_BYTES = 14UL \* 1024UL' CoreArtwork.inc 'artwork worker needs explicit TLS/JPEG stack headroom'
require 'uxTaskGetStackHighWaterMark' CoreArtwork.inc 'artwork task stack headroom must be observable'
require 'network-failed' CoreArtwork.inc 'artwork network failure must not be mislabeled as not-found'
require 'lookup-timeout' CoreArtwork.inc 'slow MusicBrainz responses need a distinct visible state'
require 'artwork_http_code' CorePortal.inc 'NOW portal must expose artwork HTTP/transport diagnosis'
require 'u8g2_font_4x6_tf' CoreDisplay.inc 'long artwork errors need a fitting fallback font'
require 'textX = x \+ \(static_cast<int>\(size\) - width\) / 2' CoreDisplay.inc 'artwork status text must be centered inside its actual placeholder origin'
require 'no-match' CoreArtwork.inc 'a real MusicBrainz miss needs a distinct state'
require 'art-not-found' CoreArtwork.inc 'all candidate covers missing needs a distinct state'
require 'artwork_download_code' CorePortal.inc 'NOW portal must expose Cover Art Archive diagnosis'
require 'artwork_candidates' CorePortal.inc 'NOW portal must expose the number of cover candidates'
require 'consumeNowArtworkResetBreadcrumb\(\)' CoreRuntime.inc 'boot must report an artwork-stage reset breadcrumb'
require 'artwork_interrupted_stage' CorePortal.inc 'portal must expose interrupted artwork stage'
require 'THERMAL_WARNING_C = MILESTONE_HAS_NOW_VIEW \? 75\.0f : 70\.0f' MILESTONE_Core.ino 'NOW thermal warning policy missing'
require 'THERMAL_THROTTLE_C = MILESTONE_HAS_NOW_VIEW \? 85\.0f : 80\.0f' MILESTONE_Core.ino 'NOW thermal throttle policy missing'
require 'THERMAL_CRITICAL_C = MILESTONE_HAS_NOW_VIEW \? 95\.0f : 90\.0f' MILESTONE_Core.ino 'NOW thermal protection policy missing'
require 'NOW_ART_CACHE_SLOTS = 6' CoreArtwork.inc 'recent artwork cache is missing'
require 'NOW_ART_BLE_MIN_FREE_HEAP = 80UL \* 1024UL' CoreArtwork.inc 'BLE artwork lookup lacks protected internal-RAM headroom'
require 'NOW_ART_BLE_MIN_LARGEST_BLOCK = 32UL \* 1024UL' CoreArtwork.inc 'BLE artwork lookup lacks contiguous internal-RAM headroom'
require 'MusicBrainz|musicbrainz\.org' CoreArtwork.inc 'direct MusicBrainz lookup missing'
require 'coverartarchive\.org' CoreArtwork.inc 'direct Cover Art Archive lookup missing'
require 'esp_jpeg_decode' CoreArtwork.inc 'on-device JPEG decoding missing'
require 'advanceNowLayout\(\)' CoreRuntime.inc 'BOOT NOW layout cycling missing'
require 'server\.on\("/api/now-config"' CorePortal.inc 'NOW layout portal API missing'
require 'storedNowLayout != static_cast<uint8_t>\(NowLayout::TITLE_ARTIST_ALBUM\)' CoreConfig.inc 'removed album-name layout must normalize on load'
require 'static constexpr NowLayout layouts\[\]' CoreRuntime.inc 'NOW BOOT cycling needs an explicit supported-layout list'
require 'layout == static_cast<int>\(NowLayout::TITLE_ARTIST_ALBUM\)' CorePortal.inc 'portal API must reject the removed album-name layout'
reject '<option value="2">곡명 \+ 아티스트 \+ 앨범명</option>' PortalPage.h 'removed album-name layout remains selectable'
require 'portal-paused' CoreArtwork.inc 'artwork must explain when setup work pauses lookup'
require 'portalStationStable' CoreArtwork.inc 'stable STA must permit artwork while the setup AP remains open'
require 'constexpr uint8_t VIEW_COUNT = 8;' MILESTONE_Core.ino 'existing eight-view cycle contract must remain unchanged'

require 'server\.on\("/api/radio-config", HTTP_GET, handleGetRadioConfig\)' CorePortal.inc 'radio config GET route missing'
require 'server\.on\("/api/radio-config", HTTP_POST, handlePostRadioConfig\)' CorePortal.inc 'radio config POST route missing'
require 'ap_password_set' CorePortal.inc 'radio config must expose only password presence'
python3 - <<'PY_RADIO_SECRET'
from pathlib import Path
s = Path('CorePortal.inc').read_text()
start = s.index('void handleGetRadioConfig()')
end = s.index('\nvoid handlePostRadioConfig()', start)
body = s[start:end]
assert 'ap_password_set' in body
assert 'fixedApPassword.length()' in body
assert 'jsonEscape(config.fixedApPassword)' not in body
assert '+ config.fixedApPassword' not in body
PY_RADIO_SECRET
require 'config\.fixedApPassword = fixedAp \? password : "";' CorePortal.inc 'turning fixed AP off must clear the dormant saved password'
require 'hasApUpdate' CorePortal.inc 'AP settings must be independently updateable'
require 'hasBluetoothUpdate' CorePortal.inc 'Bluetooth settings must be independently updateable'
require 'bluetooth_ams_ready' CorePortal.inc 'status API must expose AMS readiness'
require 'bluetooth_advertising' CorePortal.inc 'status API must expose actual advertising state'
require 'bluetooth_stage' CorePortal.inc 'status API must expose the Bluetooth runtime stage'
require 'bluetooth_error_code' CorePortal.inc 'status API must expose the Bluetooth stack error code'
require 'bluetooth_address' CorePortal.inc 'status API must expose the advertising address for scanner verification'

require 'id="fixed_ap"' PortalPage.h 'fixed AP toggle missing from portal'
require 'id="ap_password" type="password"' PortalPage.h 'fixed AP password input missing from portal'
require '비밀번호 없이 설정 AP를 사용하시겠습니까' PortalPage.h 'open AP confirmation warning missing'
require '누구나 MILESTONE Setup 네트워크에 접속' PortalPage.h 'persistent open AP security warning missing'
require 'saveApSecurity\(\)' PortalPage.h 'AP security must have an independent save action'
require 'saveBluetoothSetting\(\)' PortalPage.h 'Bluetooth must have an independent save action'
require 'id="bluetooth_now_playing"' PortalPage.h 'Bluetooth Now Playing toggle missing'
require '설정 &gt; Bluetooth 목록에 나타나지 않을 수 있습니다' PortalPage.h 'iOS BLE discovery guidance missing'
require '저장된 비밀번호는|현재 비밀번호는 숨김' PortalPage.h 'portal must explain that stored AP password is not disclosed'

# The browser must not submit AP fields when only the Bluetooth control is saved.
python3 - <<'PY_RADIO_UI'
from pathlib import Path
s = Path('PortalPage.h').read_text()
start = s.index('async function saveBluetoothSetting()')
end = s.index('\n\nasync function load()', start)
body = s[start:end]
assert 'bluetooth_now_playing' in body
assert 'fixed_ap:' not in body and 'ap_password:' not in body, 'Bluetooth-only save must not overwrite AP credentials'
PY_RADIO_UI

echo "Radio/Bluetooth configuration contract test passed"
