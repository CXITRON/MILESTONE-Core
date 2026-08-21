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

require 'FIRMWARE_VERSION\[\] = "1\.11\.2"' MILESTONE_Core.ino 'firmware version is not 1.11.2'
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
require 'id="wifi_scan_btn"' PortalPage.h 'portal scan button needs an explicit busy state'
require '설정 AP는 그대로 유지됩니다' PortalPage.h 'empty scan result must explain that setup AP remains available'
require 'lastError' PortalPage.h 'browser scan polling must tolerate transient AP packet loss'

echo "Network stability contract test passed"
