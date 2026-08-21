#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"

require_fixed() {
  local file=$1 text=$2 message=$3
  grep -Fq -- "$text" "$file" || { echo "Profile OTA contract: $message" >&2; exit 1; }
}

require_fixed FirmwareProfile.h '#define MILESTONE_FIRMWARE_PROFILE "core"' 'CORE profile identity is missing'
require_fixed FirmwareProfile.h '#define MILESTONE_FIRMWARE_PROFILE "media"' 'MEDIA profile identity is missing'
require_fixed FirmwareProfile.h '#define MILESTONE_FIRMWARE_ASSET "MILESTONE_Core.bin"' 'CORE asset mapping is missing'
require_fixed FirmwareProfile.h '#define MILESTONE_FIRMWARE_ASSET "MILESTONE_Media.bin"' 'MEDIA asset mapping is missing'
require_fixed FirmwareProfile.h '#define MILESTONE_HAS_GENERAL_VIEWS 0' 'MEDIA general-view exclusion is missing'
require_fixed MILESTONE_Core.ino '#include "CoreMediaDisabled.inc"' 'CORE media compile boundary is missing'
require_fixed MILESTONE_Core.ino '#include "CoreMedia.inc"' 'MEDIA implementation include is missing'
require_fixed CoreMedia.cpp '#if MILESTONE_HAS_MEDIA' 'standalone media codec still compiles into CORE'
require_fixed CoreMediaDisabled.inc 'esp_partition_erase_range' 'CORE factory reset no longer clears shared MEDIA storage'
require_fixed CoreUpdate.inc 'profile != requestedUpdateProfile' 'manifest profile is not validated'
require_fixed CoreUpdate.inc 'asset != firmwareAssetForProfile(profile)' 'manifest asset is not validated'
require_fixed CoreUpdate.inc 'latestFirmwareProfile != FIRMWARE_PROFILE' 'same-version profile switching is not enabled'
require_fixed CoreUpdate.inc 'target profile release is older than running firmware' 'cross-profile downgrade guard is missing'
require_fixed CoreUpdate.inc 'prepareRollbackRecord(targetIdentity)' 'rollback target is not profile-qualified'
require_fixed CoreRollback.inc 'otaTargetMatchesCurrent(previousOtaTarget)' 'legacy/profile OTA boot matching is missing'
require_fixed CorePortal.inc 'server.on("/api/profile/switch", HTTP_POST, handleProfileSwitch);' 'profile switch API is not registered'
require_fixed CoreRuntime.inc 'if (MILESTONE_HAS_MEDIA) initializeMediaStorage();' 'CORE media runtime gate is missing'
require_fixed CoreRuntime.inc 'if (MILESTONE_HAS_BLUETOOTH) processBluetoothNowPlaying();' 'MEDIA Bluetooth runtime gate is missing'
require_fixed CoreRuntime.inc 'if (!MILESTONE_HAS_GENERAL_VIEWS)' 'MEDIA-only playback runtime gate is missing'
require_fixed CoreDisplay.inc '#if MILESTONE_HAS_GENERAL_VIEWS' 'general display renderers are not compile-gated'
require_fixed CoreDisplay.inc '[[fallthrough]];' 'intentional MEDIA view fallthrough is not explicit'
require_fixed CoreNetwork.inc '#if MILESTONE_HAS_GENERAL_VIEWS' 'MEDIA still recomputes D-Day after NTP sync'
require_fixed CorePortal.inc 'next.title = config.title;' 'MEDIA does not preserve CORE general-view settings'
require_fixed CorePortal.inc 'CORE 화면 설정은 MEDIA에서 초기화할 수 없습니다.' 'MEDIA settings reset guard is missing'
require_fixed CorePortal.inc 'general_views_supported' 'profile capability is missing from status API'
require_fixed PortalPage.h "'general_dday_card','general_mode_card','general_hour_row','general_seconds_row','settings_reset_area'" 'MEDIA portal does not hide CORE-only settings'
require_fixed tools/make-release.sh 'profiles=(core media)' 'release builder does not build both profiles'
require_fixed tools/make-release.sh 'media_binary_markers=(/api/media/upload /api/stream/start /media/upload.tmp)' 'release builder does not inspect binary profile boundaries'
require_fixed tools/make-release.sh '미디어/스트리밍 구현이 BIN에 남아 있습니다' 'CORE binary exclusion check is missing'
require_fixed tools/make-release.sh 'grep -F -- "$marker" >/dev/null' 'profile marker check can still fail from a pipefail/SIGPIPE false negative'
require_fixed tools/make-release.sh 'ble_runtime_marker=MILESTONE_BLE_AMS_RUNTIME_V2' 'release build does not verify the CORE BLE runtime implementation'
if grep -Fq 'strings -a -- "$source_bin" | grep -Fq' tools/make-release.sh; then
  echo 'Profile OTA contract: profile marker check still uses grep -q under pipefail' >&2
  exit 1
fi
require_fixed tools/milestone-release 'MILESTONE_Media.bin' 'unified release command does not publish MEDIA BIN'
require_fixed tools/milestone-release 'MILESTONE_Media.json' 'unified release command does not publish MEDIA manifest'
require_fixed tools/milestone-release 'handoff_to_incoming_script_if_needed' 'Taildrop does not hand off to the incoming release command before publication'
require_fixed tools/milestone-release 'gh release upload "$TAG" "${asset_paths[@]}"' 'existing incomplete releases cannot be repaired'
require_fixed tools/milestone-release 'verify_published_release_assets' 'published CORE/MEDIA assets are not verified'
if grep -Fq 'ensure_release_not_already_published' tools/milestone-release; then
  echo 'Profile OTA contract: existing releases still exit before asset reconciliation' >&2
  exit 1
fi

bash -n tools/make-release.sh
bash -n tools/milestone-release

echo 'Profile/OTA split contract test passed'
