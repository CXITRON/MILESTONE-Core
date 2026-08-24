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

require 'FIRMWARE_VERSION\[\] = "2\.2\.23"' MILESTONE_Core.ino 'firmware version is not 2.2.23'
require 'if \(oledReady && !portalActive && !updateCheckIndicatorRendered\) return;' CoreRuntime.inc 'portal manifest checks can deadlock waiting for the non-portal U icon'
require 'UPDATE_PORTAL_HTTP_CONNECT_TIMEOUT_MS' CoreUpdate.inc 'portal manifest checks need a bounded connect timeout'
require 'UPDATE_PORTAL_TLS_HANDSHAKE_TIMEOUT_SEC' CoreUpdate.inc 'portal manifest checks need a bounded TLS timeout'
require 'UPDATE_CHECK_MAX_ATTEMPTS = 5' MILESTONE_Core.ino 'transient GitHub transport failures need hardware-verified retry headroom'
require 'UPDATE_BLUETOOTH_DEFER_MS = 5UL \* 60UL \* 1000UL' MILESTONE_Core.ino 'automatic NOW update checks need a bounded connected-session defer interval'
require 'UPDATE_RELEASE_API_URL' CoreUpdate.inc 'update checks must use the direct GitHub Release API path'
require 'application/vnd\.github\+json' CoreUpdate.inc 'GitHub Release API media type missing'
require 'parseJsonStringField\(assetFields, "digest", digest\)' CoreUpdate.inc 'GitHub asset SHA-256 digest is not validated'
require 'latestFirmwareDownloadUrl = candidate\.assetApiUrl' CoreUpdate.inc 'OTA install does not retain the verified GitHub asset API URL'
require 'firmware HTTPS attempt' CoreUpdate.inc 'OTA BIN connection lacks bounded transport retries'
require 'const uint32_t portalNow = millis\(\);' CoreRuntime.inc 'portal timeout must re-sample time after request handlers'
require 'elapsed\(portalNow, portalStartedMs, AP_TIMEOUT_MS\)' CoreRuntime.inc 'portal timeout still compares an older loop timestamp with refreshed activity'
python3 - <<'PY_PORTAL_UPDATE_ORDER'
from pathlib import Path
s = Path('CoreRuntime.inc').read_text()
start = s.index('void processFirmwareUpdate()')
end = s.index('\n\nvoid enterThermalSafeMode', start)
body = s[start:end]
gate = body.index('if (oledReady && !portalActive && !updateCheckIndicatorRendered) return;')
isolate = body.index('isolateBluetoothForFirmwareOperation()', gate)
attempt = body.index('++updateCheckAttempt', isolate)
assert gate < isolate < attempt, 'BLE isolation must start only after the portal-safe display gate'
PY_PORTAL_UPDATE_ORDER
python3 - <<'PY_PORTAL_TIMEOUT_ORDER'
from pathlib import Path
s = Path('CoreRuntime.inc').read_text()
start = s.index('void processNetwork()')
end = s.index('\n\nvoid processFirmwareUpdate()', start)
body = s[start:end]
handled = body.index('server.handleClient();')
resampled = body.index('const uint32_t portalNow = millis();', handled)
checked = body.index('elapsed(portalNow, portalStartedMs, AP_TIMEOUT_MS)', resampled)
assert handled < resampled < checked, 'portal activity time must be sampled after request handlers and before idle timeout'
PY_PORTAL_TIMEOUT_ORDER
require '#include <esp_wifi\.h>' MILESTONE_Core.ino 'low-level Wi-Fi scan control header missing'
require 'WIFI_PRIMARY_CONNECT_TIMEOUT_MS = 12UL \* 1000UL' MILESTONE_Core.ino 'primary saved-network timeout was not shortened'
require 'WIFI_PORTAL_TEST_CONNECT_TIMEOUT_MS = 15UL \* 1000UL' MILESTONE_Core.ino 'portal test timeout bound missing'
require 'PORTAL_WIFI_SCAN_DWELL_MS = 120UL' MILESTONE_Core.ino 'short portal scan dwell missing'
require 'SAVED_WIFI_SCAN_DWELL_MS = 120UL' MILESTONE_Core.ino 'saved-network scan dwell missing'
require 'esp_wifi_scan_stop\(\)' CoreNetwork.inc 'active scans are not explicitly stoppable'
require 'prepareStablePortalRadio\(\)' CoreNetwork.inc 'setup AP does not cancel background station work'
require 'WiFi\.setSleep\(false\);' CoreNetwork.inc 'setup AP must keep Wi-Fi power save disabled'
require 'scanNetworks\(true, false, false, dwellMs, 0\)' CoreNetwork.inc 'saved-network scan must use bounded dwell and skip hidden probe results'
require 'Do not blindly direct-connect every saved SSID' CoreNetwork.inc 'absent saved networks must not be exhaustively retried'
require 'normal retry backoff' CoreRuntime.inc 'no-network condition must use normal retry backoff'
require 'parkDisconnectedWifiUntilRetry\(\)' CoreNetwork.inc 'disconnected Wi-Fi radio parking helper missing'
require 'parkDisconnectedWifiUntilRetry\(\);' CoreRuntime.inc 'confirmed no-network state must park the radio until retry'
require 'stabilizePortalAfterFailedWifiTest\(\)' CorePortal.inc 'failed Wi-Fi tests must return to a stable AP state'
reject 'restoreConfiguredWifiAfterFailedTest\(\)' CorePortal.inc 'failed tests must not immediately launch another STA connection under the AP'
require 'Wi-Fi 연결 시험이 진행 중입니다\. 완료 후 검색하세요' CorePortal.inc 'manual scan must be blocked during Wi-Fi test'
require 'PORTAL_WIFI_SCAN_DWELL_MS' CorePortal.inc 'portal scan must use short AP-friendly dwell'
require 'portalStartedMs = millis\(\);' CoreNetwork.inc 'authenticated portal activity must refresh idle timeout'
require 'setupApRadioReady\(\)' CoreNetwork.inc 'setup AP radio health check missing'
require 'recoverSetupApRadioIfNeeded\(now\);' CoreRuntime.inc 'setup AP radio watchdog is not serviced'
require 'setup AP remains active' CorePortal.inc 'successful Wi-Fi test must keep the setup AP active'
require 'MILESTONE 설정 AP는 계속 유지됩니다' PortalPage.h 'portal must confirm that AP remains after provisioning'
require 'bool beginNtpRequest\(\)' CoreNetwork.inc 'NTP start must report whether the network was ready'
require 'stopNtpService\(\)' CoreNetwork.inc 'bounded NTP cleanup helper missing'
require 'NTP_SERVER_ATTEMPT_MS = 7UL \* 1000UL' MILESTONE_Core.ino 'NTP per-server failover bound missing'
require 'advanceNtpServerIfDue\(now\)' CoreRuntime.inc 'runtime does not advance an unresponsive NTP provider'
require 'configTzTime\(TZ_INFO, NTP_SERVERS\[ntpServerIndex\], nullptr, nullptr\)' CoreNetwork.inc 'NTP must isolate each provider from lwIP fixed failover delays'
require 'portalFirmwareWait' CoreRuntime.inc 'portal update/profile checks can remain queued across repeated NTP timeouts'
require 'failPortalFirmwareNetworkWait' CoreRuntime.inc 'portal firmware actions can remain queued after Wi-Fi connection failure'
require 'updateInstallAfterNetworkReady' CoreRuntime.inc 'confirmed install is not resumed after automatic Wi-Fi/NTP recovery'
require 'update_check_pending' CorePortal.inc 'portal cannot poll automatic Wi-Fi/NTP/update progress'
python3 - <<'PY_PORTAL_PROFILE_RECONNECT_ORDER'
from pathlib import Path
s = Path('CorePortal.inc').read_text()
update_start = s.index('void handleUpdateCheck()')
start = s.index('void handleProfileSwitch()')
end = s.index('\n\nvoid handleUpdateInstall()', start)
body = s[start:end]
update_body = s[update_start:start]
assert '인터넷에 연결된 Wi-Fi가 필요합니다' not in update_body
assert '인터넷에 연결된 Wi-Fi가 필요합니다' not in body
queued = body.index('requestFirmwareUpdateCheck(UpdateCheckReason::MANUAL, profile);')
connect = body.index('startSavedWifiSequence(true);', queued)
assert queued < connect, 'target profile must be preserved before automatic Wi-Fi reconnect starts'
PY_PORTAL_PROFILE_RECONNECT_ORDER
require 'time_sync_pending' CorePortal.inc 'manual time-sync progress state missing from status API'
require 'timeSyncPolling' PortalPage.h 'manual time-sync UI must poll through completion'
require 'pendingUpdateCheckReason != UpdateCheckReason::MANUAL' CoreRuntime.inc 'automatic update checks must defer while setup portal is active'
require 'if \(mediaUploadActive \|\| firmwareUpdateBusy\(\) \|\| ntpRequestActive \|\|' CorePortal.inc 'stream start must reject active time synchronization'
reject 'portalClosingAfterSuccess = true' CorePortal.inc 'successful Wi-Fi test must not arm automatic AP shutdown'
reject 'PORTAL_SUCCESS_HOLD_MS' MILESTONE_Core.ino 'obsolete post-success AP shutdown delay remains'
require 'id="wifi_scan_btn"' PortalPage.h 'portal scan button needs an explicit busy state'
require '설정 AP는 그대로 유지됩니다' PortalPage.h 'empty scan result must explain that setup AP remains available'
require 'lastError' PortalPage.h 'browser scan polling must tolerate transient AP packet loss'
require 'server\.on\("/api/portal/close", HTTP_POST, handlePortalClose\)' CorePortal.inc 'explicit setup portal close API missing'
require 'PORTAL_CLOSE_RESPONSE_HOLD_MS = 750UL' MILESTONE_Core.ino 'portal close must leave time for its HTTP response'
require 'portalCloseRequested && deadlineReached\(portalNow, portalCloseNotBeforeMs\)' CoreRuntime.inc 'portal close request is not processed after HTTP service'
require 'portalManifestWorker' CoreRuntime.inc 'portal manifest HTTPS must not block captive HTTP/DNS service'
require 'BoundedStringWriter writer\(body, UPDATE_MANIFEST_MAX_BYTES\)' CoreUpdate.inc 'unknown-length release responses need a pre-allocation body limit'
reject 'body = http\.getString\(\)' CoreUpdate.inc 'release body must not be fully allocated before its size limit is enforced'
require 'AbortController' PortalPage.h 'portal API requests need bounded browser recovery'
require 'onclick="closePortal\(\)"' PortalPage.h 'setup portal needs an explicit close button'
python3 - <<'PY_BLUETOOTH_UPDATE_DEFER'
from pathlib import Path
s = Path('CoreRuntime.inc').read_text()
start = s.index('void processFirmwareUpdate()')
end = s.index('\n\nvoid enterThermalSafeMode', start)
body = s[start:end]
live_gate = body.index('reason != UpdateCheckReason::MANUAL && bluetoothNowPlayingHasLiveConnection()')
checking = body.index('setUpdateState(UpdateState::CHECKING)', live_gate)
assert live_gate < checking, 'automatic Bluetooth defer must happen before CHECKING LED state'
failure = body.index('if (!bluetoothIsolated)', checking)
manual_exit = body.index('failFirmwareCheck("Bluetooth 연결을 종료하지 못해 업데이트 확인을 중단했습니다.", true)', failure)
assert 'updateCheckNotBeforeMs = millis() + 500UL' not in body[failure:manual_exit + 200]
assert failure < manual_exit, 'manual BLE refusal must leave CHECKING through a bounded failure path'
PY_BLUETOOTH_UPDATE_DEFER

echo "Network stability contract test passed"
