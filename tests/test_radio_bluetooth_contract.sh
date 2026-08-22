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

require 'FIRMWARE_VERSION\[\] = "2\.0\.6"' MILESTONE_Core.ino 'firmware version is not 2.0.6'
require 'CONFIG_VERSION = 10' MILESTONE_Core.ino 'configuration schema is not 10'
require 'bool fixedApSecurity = false;' MILESTONE_Core.ino 'fixed AP security must default off'
require 'String fixedApPassword;' MILESTONE_Core.ino 'fixed AP password setting missing'
require 'bool bluetoothNowPlaying = false;' MILESTONE_Core.ino 'Bluetooth Now Playing must default off'
require 'prefs\.putBool\("ap_fixed", config\.fixedApSecurity\)' CoreConfig.inc 'fixed AP enable flag is not persisted'
require 'putStringVerified\("ap_pass", config\.fixedApPassword\)' CoreConfig.inc 'fixed AP password is not persisted transactionally'
require 'prefs\.putBool\("ble_media", config\.bluetoothNowPlaying\)' CoreConfig.inc 'Bluetooth setting is not persisted'
require 'const bool migrateToV10 = version < 10;' CoreConfig.inc 'schema 10 migration gate missing'
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
require 'MILESTONE_BLE_AMS_RUNTIME_V2' CoreBluetooth.inc 'compiled BLE runtime marker missing'
require 'requires an Arduino-ESP32 build with NimBLE enabled' MILESTONE_Core.ino 'CORE must fail compilation instead of shipping a BLE stub'
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
require 'isolateBluetoothForFirmwareOperation\(\);' CoreUpdate.inc 'OTA install must isolate Bluetooth before resource checks'
require 'isolateBluetoothForFirmwareOperation\(\);' CoreRuntime.inc 'manifest checks must isolate Bluetooth before HTTPS'
require 'restoreBluetoothAfterFirmwareOperation\(\);' CoreUpdate.inc 'firmware failure paths must restore Bluetooth'
require 'restoreBluetoothAfterFirmwareOperation\(\);' CoreRuntime.inc 'successful manifest checks must restore Bluetooth'
require 'suspendBluetoothNowPlaying\(\);' CoreRuntime.inc 'STREAM_MODE entry must suspend Bluetooth'
require 'resumeBluetoothNowPlaying\(\);' CoreRuntime.inc 'STREAM_MODE exit must resume Bluetooth'
require 'processBluetoothNowPlaying\(\);' CoreRuntime.inc 'main loop must service Bluetooth metadata'
require 'if \(config\.bluetoothNowPlaying\)' CoreRuntime.inc 'Bluetooth stack must be gated by saved setting'

require 'void drawBluetoothNowPlayingScreen\(\)' CoreDisplay.inc 'Now Playing OLED renderer missing'
require 'if \(bluetoothNowPlayingVisible\(\)\)' CoreDisplay.inc 'Now Playing must be an overlay rather than a ninth persistent view'
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
