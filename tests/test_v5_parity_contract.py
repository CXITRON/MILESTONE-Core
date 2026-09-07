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
assert "WIFI_AUTH_WPA2_ENTERPRISE" in portal
assert "WIFI_AUTH_WPA3_ENTERPRISE" in portal
assert "enterprise_ca_required" in portal
assert "MILESTONE_PORTAL_HTML" in portal
assert "location.href='/artwork'" in page
assert "MilestoneV5LegacyMedia" in media
assert "kCapabilityInternetHttp" in zero
assert "kCapabilityCompanionOta" in zero
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
assert "txLeaseId = wireLeaseSequence" in main
assert "decoded.fields.leaseId != txLeaseId" in main
assert "taskFrame ? decoded.fields.leaseId != 0" in zero
assert "d.fields.leaseId, ++txSequence" in zero
assert "result == ESP_ERR_TIMEOUT" in zero
assert "transactionQueued = false;" in zero
assert "ZeroPins::kLinkReady), 0" in zero

print("v5 legacy parity source contract passed")
