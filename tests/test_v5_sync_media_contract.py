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
assert "length == 9 || length == 10" in socket
assert "controlSequence" in socket and "accepted ? 1 : 0" in socket
assert "never carry video data" in socket
assert "async function convertAndStore()" in page
assert "batchBytes>=256*1024" in page
assert "duration>21600" in page
assert "count>432000" in page
assert "fps=Math.min(20" in page
assert "total=0&offset=${offset}&final=${final?1:0}&stream=1" in page
assert 'server.arg("total")' in portal
assert 'server.arg("offset")' in portal
assert 'server.arg("stream") == "1"' in portal
assert "sync.beginUpload(expected, streaming)" in portal
assert "uploadOpenEnded" in runtime
assert "SD.usedBytes()" not in runtime
assert "new WebSocket" in page
assert "audio.currentTime" not in page  # timeline is read from the selected video element
assert "video.currentTime*1000" in page
assert "WebSocket ACK 시간 초과" in page
assert "pendingControls" in page
assert "await api('/api/sync/control'" in page
assert "portal.sync.servicePlayback" in main
assert "display.flushRegion(16, 128);" in runtime
assert "invalidateDisplayedFrame" in runtime
assert "The synchronized player owns all 128 body rows" in main
assert main.count("portal.sync.invalidateDisplayedFrame();") == 1
assert "void renderUpdateCheckResult() {\n  // Restore even a paused frame" in main
assert "const bool updateOwnsBody = updateResultVisible" in main
assert main.count("if (!updateOwnsBody &&") == 3
assert "Serial0.availableForWrite() >= length" in main
assert "!mediaTimingCritical)\n    artwork.maintain" in main
assert "!sync.occupied() && now - openedMs" in portal
assert "renderedFrames" in runtime and "controlCount" in runtime
assert "Serial0.printf(" in main and '"SYNC state=%s' in main
assert "void flushRegion(int y, int height)" in (root / "v5/MilestoneV5Main/V5Tft.h").read_text()
assert "if (mode || back)" in main and "portal.close();" in main

print("v5 staged sync-media source contract passed")
