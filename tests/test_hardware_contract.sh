#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"

require() {
  local pattern=$1 file=$2 message=$3
  grep -Eq -- "$pattern" "$file" || { echo "Hardware contract: $message" >&2; exit 1; }
}

reject() {
  local pattern=$1 file=$2 message=$3
  if grep -Eq -- "$pattern" "$file"; then
    echo "Hardware contract: $message" >&2
    exit 1
  fi
}

require 'PIN_TFT_MOSI = 8' MILESTONE_Core.ino 'TFT MOSI must remain GPIO8'
require 'PIN_TFT_SCK = 9' MILESTONE_Core.ino 'TFT SCK must remain GPIO9'
require 'PIN_TFT_CS = 10' MILESTONE_Core.ino 'TFT CS must be GPIO10'
require 'PIN_TFT_RESET = 5' MILESTONE_Core.ino 'TFT reset must be GPIO5'
require 'PIN_TFT_DC = 6' MILESTONE_Core.ino 'TFT DC must be GPIO6'
require 'PIN_BUTTON_PREVIOUS = 1' MILESTONE_Core.ino 'previous button must be GPIO1'
require 'PIN_BUTTON_NEXT = 2' MILESTONE_Core.ino 'next button must be GPIO2'
require 'PIN_BUTTON_CONFIRM = 11' MILESTONE_Core.ino 'confirm button must be GPIO11'
require 'MilestoneTftDisplay display' MILESTONE_Core.ino 'firmware must use the TFT backend'
reject '#include <Wire\.h>' MILESTONE_Core.ino 'legacy I2C display transport must stay removed'
reject 'SH1107_PIMORONI_128X128_F_HW_I2C' MILESTONE_Core.ino 'legacy SH1107 hardware object must stay removed'
require 'const uint8_t madctl\[\] = \{0x00\}' CoreTftDisplay.cpp 'verified upright RGB orientation is missing'
require 'FRAME_Y = \(TFT_HEIGHT - FRAME_HEIGHT\) / 2' CoreTftDisplay.h '128x128 frame must remain vertically centered'
require 'setProfileLabel\(FIRMWARE_PROFILE_LABEL\)' CoreDisplay.inc 'top profile label is missing'
require 'setAddressWindow\(0, FRAME_Y \+ FRAME_HEIGHT, TFT_WIDTH, 1\)' CoreTftDisplay.cpp 'bottom separator is missing'
require 'updateDisplayArea' CoreTftDisplay.cpp 'TFT dirty-area update path is missing'
require 'pinMode\(PIN_BUTTON_CONFIRM, INPUT_PULLUP\)' CoreRuntime.inc 'confirm pull-up initialization is missing'
require 'digitalRead\(PIN_BOOT\) == LOW \|\| digitalRead\(PIN_BUTTON_CONFIRM\) == LOW' CoreRuntime.inc 'confirm must mirror onboard BOOT'
require 'selectPreviousMediaItem' CoreMedia.inc 'MEDIA previous navigation is missing'
require 'retreatNowLayout' CoreRuntime.inc 'NOW previous navigation is missing'
require 'openProfileSelector\(\)' CoreRuntime.inc 'confirm-hold profile selector is missing'
require 'requestButtonProfileSwitch\(\)' CoreRuntime.inc 'button profile switch must enter OTA verification'
require 'profile == "restart"' CoreRuntime.inc 'profile menu restart action is missing'
require 'current profile selected; profile menu closed' CoreRuntime.inc 'selecting the active profile must close the menu'
require 'heldMs < 3000' CoreRuntime.inc 'confirm 1-3 second profile gesture is missing'
require 'deviceInfoPage.*DEVICE_INFO_PAGE_COUNT' CoreRuntime.inc 'confirm-driven device info paging is missing'
reject 'DEVICE_INFO_PAGE_MS' MILESTONE_Core.ino 'device info must not auto-advance on a timer'
require 'drawDeviceInfoHeader\("PSRAM"' CoreDisplay.inc 'PSRAM device-information page is missing'
require 'colorTitle' MILESTONE_Core.ino 'persistent CORE title color is missing'
require 'type="color"' PortalPage.h 'CORE portal color controls are missing'
require 'color_title_value' PortalPage.h 'CORE portal must show the selected color value'
require 'labelColor' CoreTftDisplay.cpp 'profile-colored top label is missing'
require 'initializeToneTables' CoreTftDisplay.cpp 'global low-cost TFT tone adjustment is missing'
require 'wifiIdleWaiting' CoreDisplay.inc 'Wi-Fi retry waiting state needs the hollow-circle status icon'
require 'if \(!Serial\) return' MILESTONE_Core.ino 'USB logging must skip writes without a host'
require 'Serial\.availableForWrite\(\)' MILESTONE_Core.ino 'USB logging must remain non-blocking'
require 'Serial\.setTxTimeoutMs\(0\)' CoreRuntime.inc 'USB CDC writes must have a zero blocking timeout'
reject 'setContrast|updateContrast' 'CoreTftDisplay.cpp' 'TFT brightness runtime must stay removed'
reject 'id="brightness"|id="night_level"' PortalPage.h 'nonfunctional TFT brightness controls must stay removed'

echo 'ST7735 TFT and three-button hardware contract test passed'
