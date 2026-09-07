import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
portal = (root / "v5/MilestoneV5Main/V5Portal.h").read_text()
runtime = (root / "v5/MilestoneV5Main/V5SyncMedia.h").read_text()
socket = (root / "v5/MilestoneV5Main/V5SyncSocket.h").read_text()
page = (root / "v5/MilestoneV5Main/V5SyncPage.h").read_text()
main = (root / "v5/MilestoneV5Main/MilestoneV5Main.ino").read_text()

for endpoint in (
    '"/sync"',
    '"/api/sync/status"',
    '"/api/sync/upload"',
    '"/api/sync/control"',
    '"/api/sync/remove"',
):
    assert endpoint in portal

assert '"/media/sync/video.tmp"' in runtime
assert '"/media/sync/index.tmp"' in runtime
assert "kControlStaleMs = 2500" in runtime
assert "decodeVideoHeader" in runtime
assert "crc32(scratch, length)" in runtime
assert "videoFrameAtMs" in runtime
assert "SD.rename(kUploadPath, kVideoPath)" in runtime
assert "size > 125" in socket
assert "input[0] & 0x70U" in socket
assert 'memcmp(payload, "MSC1", 4)' in socket
assert "never carry video data" in socket
assert "BROWSER_LIMIT=256*1024*1024" in page
assert "new WebSocket" in page
assert "audio.currentTime" not in page  # timeline is read from the selected video element
assert "video.currentTime*1000" in page
assert "portal.sync.servicePlayback" in main
assert "if (mode || back)" in main and "portal.close();" in main

print("v5 staged sync-media source contract passed")
