#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / "v5/MilestoneV5Main/MilestoneV5Main.ino").read_text()
portal = (ROOT / "v5/MilestoneV5Main/V5Portal.h").read_text()
page = (ROOT / "PortalPage.h").read_text()
zero = (ROOT / "v5/MilestoneV5Zero/MilestoneV5Zero.ino").read_text()
media = (ROOT / "v5/MilestoneV5Main/V5LegacyMedia.h").read_text()

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

print("v5 legacy parity source contract passed")
