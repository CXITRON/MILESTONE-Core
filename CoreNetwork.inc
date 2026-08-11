String randomHex(size_t length) {
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String out;
  out.reserve(length);
  for (size_t i = 0; i < length; ++i) out += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  return out;
}

void stopMdns() {
  if (mdnsActive) {
    MDNS.end();
    mdnsActive = false;
  }
}

void startMdns() {
  if (mdnsActive || WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsActive = true;
  }
}

void ntpTimeAvailable(struct timeval *) {
  ntpSyncEvent = true;
}

void wifiDisconnectedEvent(WiFiEvent_t, WiFiEventInfo_t info) {
  // Arduino-ESP32 documents Serial as thread-safe for network callbacks. Keep
  // the callback diagnostic-only: never mutate the firmware state machine or
  // call WiFi APIs from this separate FreeRTOS task.
  lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
  Serial.printf("[MILESTONE] Wi-Fi STA disconnected reason=%u\n",
                static_cast<unsigned>(lastWifiDisconnectReason));
}

void cancelPortalWifiScan();

void beginNtpRequest() {
  if (WiFi.status() != WL_CONNECTED) return;
  cancelPortalWifiScan();
  // Favor latency and packet reliability during the UDP exchange. Always-
  // connected mode keeps the modem awake afterward; the explicit Wi-Fi-off
  // setting is handled separately once sync/update work is complete.
  WiFi.setSleep(false);
  ntpSyncEvent = false;
  ntpFailed = false;
  ntpRequestActive = true;
  ntpRequestStartedMs = millis();
  ntpDeadlineMs = ntpRequestStartedMs + NTP_TIMEOUT_MS;
  configTzTime(TZ_INFO, "time.google.com", "pool.ntp.org", "time.cloudflare.com");
  logLine("NTP request started");
}

void markNtpSuccess() {
  ntpRequestActive = false;
  internetVerified = true;
  ntpFailed = false;
  freshNtpSinceBoot = true;
  config.lastSync = static_cast<uint64_t>(time(nullptr));
  const uint32_t ntpDurationMs = ntpRequestStartedMs == 0 ? 0 : static_cast<uint32_t>(millis() - ntpRequestStartedMs);
  ntpRequestStartedMs = 0;
  recordDiagnostic(MilestoneDiagnostics::Event::NTP_SUCCESS, 0,
                   ntpDurationMs > MilestoneDiagnostics::VALUE_MAX ? MilestoneDiagnostics::VALUE_MAX : static_cast<int32_t>(ntpDurationMs));
  if (prefs.putULong64("last_sync", config.lastSync) != sizeof(uint64_t)) logLine("NTP timestamp save failed");
  invalidateTimeDisplayCache();
  int days;
  computeDday(days, true);
  logLine("NTP synchronized");
}

void cancelPortalWifiScan() {
  if (!portalWifiScanActive) return;
  WiFi.scanDelete();
  portalWifiScanActive = false;
  portalWifiScanDeadlineMs = 0;
}

void resetEnterpriseStationAuth() {
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#if MILESTONE_HAS_MODERN_EAP
  const esp_err_t result = esp_wifi_sta_enterprise_disable();
#else
  const esp_err_t result = esp_wifi_sta_wpa2_ent_disable();
#endif
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    logLine(String("Enterprise Wi-Fi state reset failed: ") + esp_err_to_name(result));
  }
#endif
}

void beginStationConnection(const String &ssid, const String &password, WifiSecurityType security,
                            const String &username, const String &identity, bool preserveAp) {
  cancelPortalWifiScan();
  stopMdns();
  internetVerified = false;
  ntpFailed = false;
  WiFi.mode(preserveAp ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(false);  // The firmware selects among saved networks itself.
  // Keep the radio fully awake from cold driver start through DHCP, NTP, and
  // the optional update check. AP+STA testing already behaved this way in
  // practice; applying it to STA-only boot removes the cold-start difference.
  WiFi.setSleep(false);
  delay(WIFI_DRIVER_SETTLE_MS);
  WiFi.disconnect(false, false);
  delay(50);
  resetEnterpriseStationAuth();
  wl_status_t beginResult = WL_CONNECT_FAILED;
  if (security == WifiSecurityType::ENTERPRISE_PEAP) {
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    const String effectiveIdentity = identity.length() ? identity : username;
    beginResult = WiFi.begin(ssid.c_str(), WPA2_AUTH_PEAP, effectiveIdentity.c_str(), username.c_str(),
                             password.c_str(), nullptr, nullptr, nullptr, -1, 0, nullptr, true);
    logLine(String("Enterprise Wi-Fi PEAP connection started: ") + ssid);
#else
    logLine("Enterprise Wi-Fi requested but this ESP32 build has Enterprise support disabled");
#endif
  } else {
    beginResult = WiFi.begin(ssid.c_str(), password.c_str());
  }
  wifiNetworkReadySinceMs = 0;
  lastWifiDisconnectReason = 0;
  wifiConnectionStartedMs = millis();
  wifiDeadlineMs = beginResult == WL_CONNECT_FAILED ? wifiConnectionStartedMs : wifiConnectionStartedMs + WIFI_CONNECT_TIMEOUT_MS;
  if (wifiTestState == WifiTestState::IDLE) setRuntimeState(RuntimeState::CONNECTING);
  logLine(String("Wi-Fi connection started: security=") + wifiSecurityName(security));
}

void beginSavedWifiConnection(uint8_t index, bool preserveAp) {
  if (index >= config.savedNetworkCount) return;
  activeWifiIndex = index;
  beginStationConnection(config.savedNetworks[index].ssid,
                         config.savedNetworks[index].password,
                         config.savedNetworks[index].security,
                         config.savedNetworks[index].username,
                         config.savedNetworks[index].identity,
                         preserveAp);
}

void startSavedWifiSequence(bool preserveAp) {
  savedWifiScanActive = false;
  savedWifiScanCompleted = false;
  savedWifiPreserveAp = preserveAp;
  wifiCandidateCount = 0;
  wifiCandidatePosition = 0;
  activeWifiIndex = NO_WIFI_INDEX;
  if (config.savedNetworkCount == 0) {
    setRuntimeState(RuntimeState::UNPROVISIONED);
    return;
  }
  // Index 0 is the most recently successful network.
  wifiCandidateOrder[0] = 0;
  wifiCandidateCount = 1;
  beginSavedWifiConnection(0, preserveAp);
}

bool startSavedWifiScan() {
  WiFi.scanDelete();
  int started = WiFi.scanNetworks(true, true, false, 300, 0);
  if (started == WIFI_SCAN_FAILED) return false;
  savedWifiScanActive = true;
  savedWifiScanCompleted = true;
  savedWifiScanDeadlineMs = millis() + WIFI_SCAN_TIMEOUT_MS;
  logLine("scanning for other saved Wi-Fi networks");
  return true;
}

bool finishSavedWifiScanAndConnect() {
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING && !deadlineReached(millis(), savedWifiScanDeadlineMs)) return true;
  savedWifiScanActive = false;
  if (count < 0) {
    WiFi.scanDelete();
    return false;
  }

  int32_t bestRssi[MAX_SAVED_NETWORKS];
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; ++i) bestRssi[i] = -1000;
  for (int scanIndex = 0; scanIndex < count; ++scanIndex) {
    int savedIndex = findSavedNetwork(WiFi.SSID(scanIndex));
    if (savedIndex <= 0) continue;  // The most recent network was already tried.
    bestRssi[savedIndex] = max(bestRssi[savedIndex], WiFi.RSSI(scanIndex));
  }
  WiFi.scanDelete();

  wifiCandidateCount = 1;
  bool selected[MAX_SAVED_NETWORKS] = {};
  selected[0] = true;
  while (wifiCandidateCount < config.savedNetworkCount) {
    int bestIndex = -1;
    int32_t strongest = -1001;
    for (uint8_t i = 1; i < config.savedNetworkCount; ++i) {
      if (!selected[i] && bestRssi[i] > strongest) {
        strongest = bestRssi[i];
        bestIndex = i;
      }
    }
    if (bestIndex < 0 || strongest == -1000) break;
    selected[bestIndex] = true;
    wifiCandidateOrder[wifiCandidateCount++] = static_cast<uint8_t>(bestIndex);
  }

  // Hidden networks cannot always be identified by SSID in scan results.
  // Append every unseen saved entry after the RSSI-ranked candidates so each
  // network still receives a direct connection attempt.
  for (uint8_t i = 1; i < config.savedNetworkCount; ++i) {
    if (!selected[i]) wifiCandidateOrder[wifiCandidateCount++] = i;
  }

  wifiCandidatePosition = 1;
  if (wifiCandidatePosition >= wifiCandidateCount) return false;
  beginSavedWifiConnection(wifiCandidateOrder[wifiCandidatePosition], savedWifiPreserveAp);
  return true;
}

bool tryNextSavedWifiCandidate() {
  if (!savedWifiScanCompleted) return startSavedWifiScan();
  ++wifiCandidatePosition;
  if (wifiCandidatePosition >= wifiCandidateCount) return false;
  beginSavedWifiConnection(wifiCandidateOrder[wifiCandidatePosition], savedWifiPreserveAp);
  return true;
}

void scheduleRetry(bool quick = false) {
  if (quick) {
    nextRetryMs = millis() + WIFI_QUICK_RETRY_MS;
    return;
  }
  uint32_t retrySeconds = config.retryPeriodSec < 30 ? 30 : config.retryPeriodSec;
  nextRetryMs = millis() + retrySeconds * 1000UL;
}

void stopPortal();
void registerPortalRoutes();
void selectFirstEnabledCycleView();
void sendJson(int status, const String &json);
void enterWifiSleep();
void requestFirmwareUpdateCheck(UpdateCheckReason reason);
void processFirmwareUpdate();
bool stationNetworkReady();

void startPortal() {
  if (thermalSafeMode) return;
  if (portalActive) {
    portalStartedMs = millis();
    return;
  }
  apPassword = randomHex(8);
  sessionToken = randomHex(20);
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(AP_SSID, apPassword.c_str())) {
    logLine("SoftAP start failed");
    return;
  }
  dnsServer.start(53, "*", WiFi.softAPIP());
  registerPortalRoutes();
  server.begin();
  portalActive = true;
  portalClosingAfterSuccess = false;
  portalStartedMs = millis();
  wakeDisplay();
  setRuntimeState(RuntimeState::SETUP_AP);
  logLine(String("setup AP started at ") + WiFi.softAPIP().toString());
}

void stopPortal() {
  if (!portalActive) return;
  cancelPortalWifiScan();
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  portalActive = false;
  portalClosingAfterSuccess = false;
  wifiTestState = WifiTestState::IDLE;
  wifiTestError = "";
  pendingSsid = "";
  pendingPass = "";
  pendingWifiSecurity = WifiSecurityType::PERSONAL;
  pendingWifiUsername = "";
  pendingWifiIdentity = "";
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    startMdns();
    if (internetVerified && timeIsValid() && !ntpFailed) {
      setRuntimeState(RuntimeState::RUNNING_ONLINE);
    } else {
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      scheduleRetry();
    }
  } else if (config.savedNetworkCount > 0) {
    startSavedWifiSequence(false);
  } else {
    WiFi.mode(WIFI_OFF);
    setRuntimeState(RuntimeState::UNPROVISIONED);
  }
  wakeDisplay();
  logLine("setup AP stopped");
}

bool requestFromSetupAp() {
  return portalActive && server.client().localIP() == WiFi.softAPIP();
}

bool requireSetupApRequest(bool jsonResponse = true) {
  if (requestFromSetupAp()) return true;
  if (jsonResponse) sendJson(403, "{\"error\":\"설정 Wi-Fi에서만 접근할 수 있습니다.\"}");
  else server.send(403, "text/plain; charset=UTF-8", "설정 Wi-Fi에서만 접근할 수 있습니다.");
  return false;
}

bool validToken() {
  if (!server.hasHeader("Cookie")) return false;
  const String expected = String("MILESTONE_TOKEN=") + sessionToken;
  const String cookies = server.header("Cookie");
  int start = 0;
  while (start <= static_cast<int>(cookies.length())) {
    int end = cookies.indexOf(';', start);
    if (end < 0) end = cookies.length();
    String cookie = cookies.substring(start, end);
    cookie.trim();
    if (cookie == expected) return true;
    if (end == static_cast<int>(cookies.length())) break;
    start = end + 1;
  }
  return false;
}

void sendJson(int status, const String &json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json; charset=UTF-8", json);
}

bool authorizePortalRequest() {
  if (!requireSetupApRequest()) return false;
  if (validToken()) return true;
  sendJson(403, "{\"error\":\"유효하지 않은 세션 토큰입니다.\"}");
  return false;
}

