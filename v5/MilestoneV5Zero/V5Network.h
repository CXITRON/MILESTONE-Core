#pragma once
#include <MilestoneV5Legacy.h>
#include <MilestoneV5SystemSettings.h>
#include <MilestoneV5WifiStore.h>
#include <WiFi.h>
#include <atomic>
#include <esp_sntp.h>
namespace V5Network {
MilestoneV5::WifiCredentials credentials;
MilestoneV5::WifiStore store;
MilestoneV5::SystemSettings settings;
bool configured = false, connecting = false, syncing = false, started = false;
bool reconnectRequested = false;
uint32_t attemptMs = 0, retryMs = 0, syncMs = 0;
uint32_t lastTimeSyncMs = 0;
bool testing = false, testStarted = false, testFinished = false;
uint8_t testResult = 1, networkIndex = 0;
uint32_t testAt = 0, connectedAt = 0;
MilestoneV5::WifiCredentials candidate;
std::atomic<bool> timeReceived{false};
void synchronized(struct timeval *) { timeReceived.store(true); }
void startTime(uint32_t now) {
  timeReceived.store(false);
  esp_sntp_set_time_sync_notification_cb(synchronized);
  configTime(0, 0, "time.cloudflare.com", "time.google.com", "pool.ntp.org");
  syncMs = now;
  syncing = true;
}
void begin() {
  MilestoneV5::importLegacyNetworks();
  settings.begin();
  if (!settings.loaded) {
    uint8_t legacy[MilestoneV5::kLegacySnapshotBytes];
    if (MilestoneV5::legacySnapshot(legacy))
      settings.importLegacy(legacy);
  }
  configured = store.load(credentials);
  retryMs = millis();
}
uint8_t provision(const uint8_t *p, size_t size) {
  MilestoneV5::WifiCredentials next;
  if (!p || !MilestoneV5::decodeWifi(p, size, next))
    return 1;
  uint8_t a[MilestoneV5::kWifiWireBytes], b[MilestoneV5::kWifiWireBytes];
  MilestoneV5::encodeWifi(next, a);
  MilestoneV5::encodeWifi(candidate, b);
  if ((testing || testFinished) && !memcmp(a, b, sizeof(a)))
    return testing ? 2 : testResult;
  if (testing)
    return 1;
  candidate = next;
  testing = true;
  testStarted = testFinished = false;
  testAt = millis();
  connectedAt = 0;
  return 2;
}
void service(uint32_t now, bool bleBusy, bool forceTime = false) {
  if (testing) {
    if (!testStarted) {
      if (bleBusy) {
        if (now - testAt > 15000) {
          testing = false;
          testFinished = true;
          testResult = 1;
        }
        return;
      }
      esp_sntp_stop();
      WiFi.disconnect(false, false);
      WiFi.mode(WIFI_STA);
      connecting = syncing = false;
      testStarted = MilestoneV5::connectWifi(candidate);
      testAt = now;
      if (!testStarted) {
        testing = false;
        testFinished = true;
        testResult = 1;
      }
      return;
    }
    bool connected =
        WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
    if (connected && !connectedAt)
      connectedAt = now;
    if (!connected)
      connectedAt = 0;
    if ((connectedAt && now - connectedAt >= 2000) || now - testAt >= 15000) {
      testResult =
          connectedAt && now - connectedAt >= 2000 && store.save(candidate) ? 0
                                                                            : 1;
      testing = false;
      testFinished = true;
      configured = store.load(credentials);
      networkIndex = 0;
      if (testResult) {
        WiFi.disconnect(false, false);
        retryMs = now + 1000;
      } else {
        timeReceived.store(false);
        esp_sntp_set_time_sync_notification_cb(synchronized);
        configTime(0, 0, "time.cloudflare.com", "time.google.com",
                   "pool.ntp.org");
        syncMs = now;
        syncing = true;
      }
    }
    return;
  }
  if (!configured)
    return;
  if (reconnectRequested && !bleBusy) {
    WiFi.disconnect(false, false);
    connecting = false;
    syncing = false;
    reconnectRequested = false;
    esp_sntp_stop();
  }
  if (connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      connecting = false;
      if (settings.bootSync || forceTime)
        startTime(now);
    } else if (now - attemptMs >= 12000) {
      connecting = false;
      WiFi.disconnect(false, false);
      if (store.load(credentials, ++networkIndex))
        retryMs = now + 1000;
      else {
        networkIndex = 0;
        configured = store.load(credentials);
        retryMs = now + settings.retrySeconds * 1000;
        WiFi.mode(WIFI_OFF);
      }
    }
  }
  if (syncing && (timeReceived.load() || now - syncMs >= 21000)) {
    if (timeReceived.load())
      lastTimeSyncMs = now;
    esp_sntp_stop();
    syncing = false;
  }
  if (!connecting && !syncing && WiFi.status() == WL_CONNECTED &&
      ((forceTime && time(nullptr) < 1704067200) ||
       (settings.ntpSeconds &&
        now - lastTimeSyncMs >= settings.ntpSeconds * 1000)) &&
      now - syncMs >= 60000)
    startTime(now);
  if (!connecting && !syncing && WiFi.status() != WL_CONNECTED && !bleBusy &&
      int32_t(now - retryMs) >= 0) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(settings.wifiSleep);
    MilestoneV5::connectWifi(credentials);
    connecting = true;
    started = true;
    attemptMs = now;
  }
}
} // namespace V5Network
