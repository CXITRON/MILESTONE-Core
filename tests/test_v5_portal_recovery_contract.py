import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
main = (root / "v5/MilestoneV5Main/MilestoneV5Main.ino").read_text()
portal = (root / "v5/MilestoneV5Main/V5Portal.h").read_text()
radio = (root / "v5/MilestoneV5Main/V5Radio.h").read_text()
zero_network = (root / "v5/MilestoneV5Zero/V5Network.h").read_text()
zero = (root / "v5/MilestoneV5Zero/MilestoneV5Zero.ino").read_text()
page = (root / "PortalPage.h").read_text()

# A stale MEDIA URL must return to the setup root instead of trapping the
# captive browser on a plain 403 page.
sync_route = portal[portal.index('server.on("/sync"'):]
sync_route = sync_route[:sync_route.index('server.on("/status"')]
assert 'profile != MilestoneV5::Profile::kMedia' in sync_route
assert 'server.sendHeader("Location", "/")' in sync_route
assert '"text/plain; charset=utf-8"' in portal

# Wi-Fi completion is explicit. Failure must not collapse back to idle and
# successful saves must refresh the persisted network list in the browser.
assert "uint8_t wifiTestState = 0" in portal
assert 'wifiTestState == 3 ? "failed"' in portal
assert "portal.wifiTestState = ok ? 2 : 3" in radio
assert "portal.wifiTestState = saved ? 2 : 3" in main
assert "wifiPolling=false;await load();" in page

# Keep last valid ZERO telemetry visible while provisioning responses replace
# ordinary status packets, and never overlay status bands on the boot splash.
assert "now - lastValidLinkMs <= MilestoneV5::kLinkStaleMs" in main
assert "if (now - bootStartedMs >= 3000)" in main
assert "kZeroStartReplyTimeoutMs = 15000" in (root / "v5/MilestoneV5Main/V5BundleDownload.h").read_text()
assert "Automatic signed update check queued" in main
assert "WiFi.softAPgetStationNum() > 0 && !useZero" in main
assert "downloadError" in portal and '"error"' in portal
assert "updateCheckInFlight && !bundleDownload.active" in main
assert "bundleDownload.ready || bundleDownload.upToDate" in main
assert "updateResultVisible = true" in main
update_ui = (root / "v5/libraries/MilestoneV5Core/src/MilestoneV5UpdateUi.h").read_text()
assert 'key("OK", action, 108)' in update_ui and 'key("BACK", "취소", 125)' in update_ui
assert 'choices(ready ? "설치" : "다운로드")' in update_ui
confirmation = main[main.index("  if (updateResultVisible &&"):main.index("  if (portal.active) {", main.index("  if (updateResultVisible &&"))]
assert "updateCheckResult == UpdateCheckResult::Available" in confirmation
assert "if (back)" in confirmation and "if (!ok)" in confirmation
assert "!bundleDownload.ready && !bundleDownload.available" in confirmation
assert "temperatureSafe || radio.busy" in confirmation
assert "portal.bundleSource = bundleDownload.directory" in confirmation
assert confirmation.index("portal.bundleRequested = true") < confirmation.index("bundleUpdate.start(portal.bundleSource)")
assert '"available"' in portal and '"current"' in portal and '"idle"' in portal
assert "OTA check complete: current" in main
assert main.count("bundleDownload.active && !bundleDownload.checking() && !stableChannel.busy()") == 2
assert 'hardware.body("업데이트 확인"' not in main
assert 'portal.downloadVersion == "latest" ? "업데이트 확인"' not in main
assert "bundleDownload.active || updateCheckInFlight || portal.downloadRequested ? 4" in main
assert "updateResultVisible = false; // A previous result" in main
assert "Serial0.printf(\"OTA check" in main
download = (root / "v5/MilestoneV5Main/V5BundleDownload.h").read_text()
assert "kLocalPreparationTimeoutMs = 90000" in download
assert "kStageProgressTimeoutMs = 180000" in download
assert "V5DownloadWorker::failureText()" in download

# Stored credentials must not be replayed as a provisioning test on every
# boot. SNTP teardown is legal only after that board has started SNTP/lwIP.
assert "wifiReplicate = store.load(wifi)" not in portal
assert "bool sntpStarted = false" in zero_network
assert "void stopTime()" in zero_network
assert zero_network.count("esp_sntp_stop();") == 1
assert "V5Network::stopTime();" in zero
assert "void stopNtp()" in radio
assert radio.count("esp_sntp_stop();") == 1

print("v5 AP/Wi-Fi recovery source contract passed")
