#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / "v5/MilestoneV5Main/MilestoneV5Main.ino").read_text()
portal = (ROOT / "v5/MilestoneV5Main/V5Portal.h").read_text()
page = (ROOT / "PortalPage.h").read_text()
zero = (ROOT / "v5/MilestoneV5Zero/MilestoneV5Zero.ino").read_text()
media = (ROOT / "v5/MilestoneV5Main/V5LegacyMedia.h").read_text()
core_views = (ROOT / "v5/MilestoneV5Main/V5CoreViews.h").read_text()
hardware = (ROOT / "v5/MilestoneV5Main/V5Hardware.h").read_text()
artwork_portal = (ROOT / "v5/MilestoneV5Main/V5ArtworkPortal.h").read_text()

for marker in (
    '"CYTRON//MILESTONE"', '"MILESTONE NOW"', '"BLE ADVERTISING"',
    '"NOW PLAYING"', '"NO MEDIA"', '"OPEN SETUP TO ADD"',
    '"PROFILE / POWER"', '"MILESTONE-D1-SETUP"', '"BACK: CLOSE"',
):
    assert marker in main, f"v5 legacy display marker missing: {marker}"

for route in (
    "/api/config", "/api/radio-config", "/api/now-config",
    "/api/wifi/scan", "/api/wifi/test", "/api/wifi/delete",
    "/api/time/sync", "/api/media/status", "/api/media/list",
    "/api/media/upload", "/api/media/update", "/api/media/order",
    "/api/media/delete", "/api/media/clear", "/api/media/repair",
    "/api/update/check", "/api/update/install", "/api/settings/reset",
    "/api/reset", "/api/portal/close",
):
    assert route in portal, f"v5 legacy portal route missing: {route}"

assert '#define MILESTONE_HAS_STREAM 0' in portal
for forbidden in ('server.on("/stream"', 'server.on("/api/stream/'):
    assert forbidden not in portal, f"streaming route returned: {forbidden}"

assert "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" in portal
close_start = portal.index("  void close()")
close_end = portal.index("  void service()", close_start)
close_body = portal[close_start:close_end]
assert "WiFi.scanDelete()" in close_body
assert "wifiScanRunning = false" in close_body
assert "WIFI_AUTH_WPA2_ENTERPRISE" in portal
assert "WIFI_AUTH_WPA3_ENTERPRISE" in portal
assert "enterprise_ca_required" in portal
assert "MILESTONE_PORTAL_HTML" in portal
assert "location.href='/artwork'" in page
assert "MilestoneV5LegacyMedia" in media
assert "kCapabilityInternetHttp" in zero
assert "kCapabilityCompanionOta" in zero
assert "kStatusBleAdvertising" in zero
assert "kStatusBleReady" in zero
assert "kStatusBleError" in zero
assert "static constexpr uint8_t kInfoPageCount = 9" in core_views
for heading in ('"TIME / RTC"', '"ENVIRONMENT"', '"MAIN / ZERO"',
                '"FIRMWARE / SAFE"'):
    assert heading in core_views, f"v5 device-info page missing: {heading}"
assert 'display.print("AP")' in hardware
assert 'display.drawCircle(122, 6, 4, 0x2F2D)' in hardware
assert "['delete','이미지만 삭제']" in artwork_portal
assert 'op == "delete"' in artwork_portal
assert "SET_LOOP_TASK_STACK_SIZE(32 * 1024);" in main
assert "kEnableZeroLink" not in main
assert "linkSpi.begin(" in main
assert "exchangeHeartbeat(now);" in main
assert '"BLUETOOTH ERROR"' in main
assert '"BLUETOOTH STARTING"' in main
assert "txLeaseId = wireLeaseSequence" in main
assert "decoded.fields.leaseId != txLeaseId" in main
assert "taskFrame ? decoded.fields.leaseId != 0" in zero
assert "d.fields.leaseId, ++txSequence" in zero
assert "result == ESP_ERR_TIMEOUT" in zero
assert "transactionQueued = false;" in zero
assert "ZeroPins::kLinkReady), 0" in zero
assert "legacyAutoText(selected, 68, color);" in main
assert "coreViews.view != 6" in main
assert "glyphs.drawHLine(0, y, 128);" in hardware
assert "canvas.drawHLine(0, y, 128);" in core_views
assert 'centered(h, "시간 미확정", 66' in core_views
assert 'onclick="openSyncMedia()"' in page
assert "async function openSyncMedia()" in page
assert "s.profile!=='media'" in page
assert "function armWifiDelete(ssid,button)" in page
assert "다시 눌러 삭제" in page
assert "confirm(`저장된 Wi-Fi" not in page
assert "MEDIA_MAX_FRAMES=4096" in page
assert "MEDIA_FILE_LIMIT=4*1024*1024" in page
assert '"media_limit_bytes\\\":268435456' in portal
assert "SD.totalBytes()" not in core_views
assert "SD.usedBytes()" not in core_views
assert "WiFi.status()" not in core_views
assert "centered(h, clock, 72, u8g2_font_6x10_tf" in core_views
assert 'legacyAutoText("BACK: 재생 종료", 70);' in main
assert 'legacyText("PREV", 104' in main
assert 'legacyText("NEXT", 104' in main

print("v5 legacy parity source contract passed")
