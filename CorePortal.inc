void handleStatus() {
  if (!requireSetupApRequest()) return;
  String wifiLabel;
  if (runtimeState == RuntimeState::WIFI_SLEEP) wifiLabel = "sleep";
  else if (WiFi.status() == WL_CONNECTED) wifiLabel = String(WiFi.SSID()) + " (" + WiFi.RSSI() + " dBm)";
  else wifiLabel = "disconnected";
  String ip = portalActive ? WiFi.softAPIP().toString() : (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "");
  String json = "{";
  json.reserve(768);
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"state\":\"" + String(runtimeStateName(runtimeState)) + "\",";
  json += "\"wifi\":\"" + jsonEscape(wifiLabel) + "\",";
  json += "\"ip\":\"" + jsonEscape(ip) + "\",";
  char uptimeText[24];
  snprintf(uptimeText, sizeof(uptimeText), "%llu", static_cast<unsigned long long>(uptimeSeconds()));
  json += "\"uptime_sec\":" + String(uptimeText) + ",";
  json += "\"cpu_mhz\":" + String(getCpuFrequencyMhz()) + ",";
  if (isnan(chipTemperatureC)) json += "\"temperature_c\":null,";
  else json += "\"temperature_c\":" + String(chipTemperatureC, 1) + ",";
  json += "\"heap_total\":" + String(ESP.getHeapSize()) + ",";
  json += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"heap_min\":" + String(ESP.getMinFreeHeap()) + ",";
  json += "\"heap_largest\":" + String(ESP.getMaxAllocHeap()) + ",";
  json += "\"stack_free\":" + String(uxTaskGetStackHighWaterMark(nullptr)) + ",";
  json += "\"flash_total\":" + String(ESP.getFlashChipSize()) + ",";
  json += "\"sketch_size\":" + String(ESP.getSketchSize()) + ",";
  json += "\"ota_free\":" + String(ESP.getFreeSketchSpace()) + ",";
  json += "\"nvs_ready\":" + String(prefsReady ? "true" : "false") + ",";
  json += "\"nvs_free_entries\":" + String(prefsReady ? prefs.freeEntries() : 0) + ",";
  json += "\"time_valid\":" + String(timeIsValid() ? "true" : "false") + ",";
  json += "\"last_sync\":\"" + jsonEscape(formatEpoch(config.lastSync)) + "\",";
  json += "\"wifi_test\":\"" + String(wifiTestStateName(wifiTestState)) + "\",";
  json += "\"wifi_error\":\"" + jsonEscape(wifiTestError) + "\",";
  json += "\"update_state\":\"" + String(updateStateName(updateState)) + "\",";
  json += "\"latest_firmware\":\"" + jsonEscape(latestFirmwareVersion) + "\",";
  json += "\"update_available\":" + String(updateState == UpdateState::AVAILABLE ? "true" : "false") + ",";
  const bool updateInstallReady = updateState == UpdateState::AVAILABLE && latestFirmwareVersion.length() > 0 &&
                                  latestFirmwareSize > 0 && validSha256(latestFirmwareSha256);
  json += "\"update_install_ready\":" + String(updateInstallReady ? "true" : "false") + ",";
  json += "\"update_install_pending\":" + String(updateInstallRequested ? "true" : "false") + ",";
  const uint32_t updateProgress = latestFirmwareSize > 0
                                    ? static_cast<uint32_t>((updateDownloadedBytes * 100ULL) / latestFirmwareSize) : 0;
  json += "\"update_progress\":" + String(updateProgress) + ",";
  json += "\"update_error\":\"" + jsonEscape(updateError) + "\",";
  json += "\"last_ota_result\":\"" + jsonEscape(lastOtaResult) + "\",";
  json += "\"last_update_check\":\"" + jsonEscape(formatEpoch(lastUpdateCheckEpoch)) + "\"}";
  sendJson(200, json);
}

void handleGetConfig() {
  if (!requireSetupApRequest()) return;
  String json = "{";
  json.reserve(1400);
  String preferredSsid = config.savedNetworkCount > 0 ? config.savedNetworks[0].ssid : "";
  json += "\"wifi_ssid\":\"" + jsonEscape(preferredSsid) + "\",";
  if (config.savedNetworkCount > 0) {
    const SavedNetwork &preferred = config.savedNetworks[0];
    json += "\"wifi_security\":\"" + String(wifiSecurityName(preferred.security)) + "\",";
  } else {
    json += "\"wifi_security\":\"personal\",";
  }
  // Do not return saved Enterprise credentials to the browser. Empty fields
  // reuse the stored values when the same saved network is tested again.
  json += "\"wifi_username\":\"\",\"wifi_identity\":\"\",";
  json += "\"enterprise_supported\":" + String(enterpriseWifiSupported() ? "true" : "false") + ",";
  json += "\"saved_networks\":[";
  for (uint8_t i = 0; i < config.savedNetworkCount; ++i) {
    if (i) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(config.savedNetworks[i].ssid) + "\",\"security\":\"";
    json += wifiSecurityName(config.savedNetworks[i].security);
    json += "\",\"preferred\":";
    json += i == 0 ? "true}" : "false}";
  }
  json += "],";
  json += "\"mode\":" + String(static_cast<uint8_t>(config.mode)) + ",";
  json += "\"last_view\":" + String(static_cast<uint8_t>(config.lastView)) + ",";
  json += "\"title\":\"" + jsonEscape(config.title) + "\",";
  json += "\"target\":\"" + jsonEscape(config.target) + "\",";
  json += "\"message\":\"" + jsonEscape(config.message) + "\",";
  json += "\"dday_style\":" + String(config.ddayTextStyle ? 1 : 0) + ",";
  json += "\"after_mode\":" + String(config.afterComplete ? 1 : 0) + ",";
  json += "\"msg_align\":" + String(config.messageLeft ? 1 : 0) + ",";
  json += "\"msg_scroll\":" + String(config.messageScroll ? "true" : "false") + ",";
  json += "\"scroll_speed\":" + String(config.scrollSpeed) + ",";
  json += "\"hour24\":" + String(config.hour24 ? 1 : 0) + ",";
  json += "\"show_seconds\":" + String(config.showSeconds ? 1 : 0) + ",";
  json += "\"show_temp\":" + String(config.showChipTemperature ? "true" : "false") + ",";
  json += "\"boot_sync\":" + String(config.bootSync ? "true" : "false") + ",";
  json += "\"ntp_period\":" + String(config.ntpPeriodSec) + ",";
  json += "\"dday_period\":" + String(config.ddayPeriodSec) + ",";
  json += "\"retry_period\":" + String(config.retryPeriodSec) + ",";
  json += "\"wifi_sleep\":" + String(config.wifiSleep ? "true" : "false") + ",";
  json += "\"brightness\":" + String(config.brightness) + ",";
  json += "\"night_level\":" + String(config.nightLevel) + ",";
  json += "\"led_enabled\":" + String(config.ledEnabled ? "true" : "false") + ",";
  json += "\"led_brightness\":" + String(config.ledBrightness) + ",";
  json += "\"led_night_level\":" + String(config.ledNightLevel) + ",";
  json += "\"night_start\":" + String(config.nightStartMin) + ",";
  json += "\"night_end\":" + String(config.nightEndMin) + ",";
  json += "\"burnin\":" + String(config.burninShift ? "true" : "false") + ",";
  json += "\"screen_off\":" + String(config.screenOffMin) + ",";
  json += "\"cycle_mask\":" + String(config.cycleMask) + ",";
  json += "\"cycle_order\":\"" + cycleOrderToString(config) + "\",";
  json += "\"cycle_interval\":" + String(config.cycleIntervalSec) + ",";
  json += "\"time_zone\":\"Asia/Seoul\"}";
  sendJson(200, json);
}

void handleWifiScan() {
  if (!requireSetupApRequest()) return;
  if (savedWifiScanActive) {
    sendJson(409, "{\"error\":\"저장된 Wi-Fi 자동 검색이 진행 중입니다. 잠시 후 다시 시도하세요.\"}");
    return;
  }
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    if (!portalWifiScanActive) {
      portalWifiScanActive = true;
      portalWifiScanDeadlineMs = millis() + WIFI_SCAN_TIMEOUT_MS;
    } else if (deadlineReached(millis(), portalWifiScanDeadlineMs)) {
      cancelPortalWifiScan();
      sendJson(504, "{\"error\":\"Wi-Fi 검색 시간이 초과되었습니다. 다시 시도하세요.\"}");
      return;
    }
    sendJson(202, "{\"state\":\"scanning\"}");
    return;
  }
  if (count == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    int started = WiFi.scanNetworks(true, true, false, 300, 0);
    if (started == WIFI_SCAN_FAILED) {
      sendJson(500, "{\"error\":\"Wi-Fi 검색을 시작하지 못했습니다.\"}");
      return;
    }
    portalWifiScanActive = true;
    portalWifiScanDeadlineMs = millis() + WIFI_SCAN_TIMEOUT_MS;
    sendJson(202, "{\"state\":\"scanning\"}");
    return;
  }
  portalWifiScanActive = false;
  portalWifiScanDeadlineMs = 0;
  String json = "{\"networks\":[";
  json.reserve(2048);
  bool first = true;
  const int resultCount = min(count, 40);
  for (int i = 0; i < resultCount; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    if (!first) json += ',';
    first = false;
    const wifi_auth_mode_t auth = static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i));
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + WiFi.RSSI(i);
    json += ",\"open\":" + String(auth == WIFI_AUTH_OPEN ? "true" : "false");
    const bool enterpriseCaRequired = wifiAuthRequiresEnterpriseCa(auth);
    json += ",\"enterprise\":" + String(wifiAuthIsEnterprise(auth) && !enterpriseCaRequired ? "true" : "false");
    json += ",\"enterprise_ca_required\":" + String(enterpriseCaRequired ? "true" : "false") + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  sendJson(200, json);
}

bool parseUintArg(const char *name, uint32_t &value) {
  if (!server.hasArg(name)) return false;
  const String text = server.arg(name);
  if (text.length() == 0) return false;
  uint64_t parsed = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    if (!isDigit(text[i])) return false;
    parsed = parsed * 10ULL + static_cast<uint8_t>(text[i] - '0');
    if (parsed > UINT32_MAX) return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseBoolArg(const char *name, bool &value) {
  if (!server.hasArg(name)) return false;
  const String text = server.arg(name);
  if (text == "0") {
    value = false;
    return true;
  }
  if (text == "1") {
    value = true;
    return true;
  }
  return false;
}

void handlePostConfig() {
  if (!authorizePortalRequest()) return;
  if (!server.hasArg("title") || !server.hasArg("target") || !server.hasArg("message") ||
      !server.hasArg("cycle_order")) {
    return sendJson(400, "{\"error\":\"필수 설정 항목이 누락되었습니다.\"}");
  }
  Config next = config;
  next.title = server.arg("title");
  next.target = server.arg("target");
  next.message = server.arg("message");
  uint32_t mode, cycleMask, cycleInterval, scrollSpeed, brightness, nightLevel;
  uint32_t ledBrightness, ledNightLevel, nightStart, nightEnd, screenOff;
  uint32_t ntpPeriod, ddayPeriod, retryPeriod;
  bool ddayTextStyle, afterComplete, messageLeft, messageScroll, hour24, showSeconds;
  bool showChipTemperature, bootSync, wifiSleep, ledEnabled, burninShift;
  if (!parseUintArg("mode", mode) || !parseUintArg("cycle_mask", cycleMask) ||
      !parseUintArg("cycle_interval", cycleInterval) || !parseUintArg("scroll_speed", scrollSpeed) ||
      !parseUintArg("brightness", brightness) || !parseUintArg("night_level", nightLevel) ||
      !parseUintArg("led_brightness", ledBrightness) || !parseUintArg("led_night_level", ledNightLevel) ||
      !parseUintArg("night_start", nightStart) || !parseUintArg("night_end", nightEnd) ||
      !parseUintArg("screen_off", screenOff) || !parseUintArg("ntp_period", ntpPeriod) ||
      !parseUintArg("dday_period", ddayPeriod) || !parseUintArg("retry_period", retryPeriod) ||
      !parseBoolArg("dday_style", ddayTextStyle) || !parseBoolArg("after_mode", afterComplete) ||
      !parseBoolArg("msg_align", messageLeft) || !parseBoolArg("msg_scroll", messageScroll) ||
      !parseBoolArg("hour24", hour24) || !parseBoolArg("show_seconds", showSeconds) ||
      !parseBoolArg("show_temp", showChipTemperature) || !parseBoolArg("boot_sync", bootSync) ||
      !parseBoolArg("wifi_sleep", wifiSleep) || !parseBoolArg("led_enabled", ledEnabled) ||
      !parseBoolArg("burnin", burninShift)) {
    return sendJson(400, "{\"error\":\"설정 값이 누락되었거나 숫자·선택 형식이 잘못되었습니다.\"}");
  }
  int year, month, day;
  if (utf8Codepoints(next.title) > 24 || next.title.length() > 96 || next.title.length() == 0) return sendJson(400, "{\"error\":\"목표 이름은 1~24자여야 합니다.\"}");
  if (utf8Codepoints(next.message) > 60 || next.message.length() > 240) return sendJson(400, "{\"error\":\"문구는 60자 이하여야 합니다.\"}");
  if (!parseDate(next.target, year, month, day)) return sendJson(400, "{\"error\":\"목표 날짜 형식이 잘못되었습니다.\"}");
  if (mode > 7) return sendJson(400, "{\"error\":\"표시 모드가 잘못되었습니다.\"}");
  if (cycleMask < 1 || cycleMask > 127) return sendJson(400, "{\"error\":\"순환 화면을 하나 이상 선택하세요.\"}");
  if (!(cycleInterval == 0 || (cycleInterval >= 3 && cycleInterval <= 60))) return sendJson(400, "{\"error\":\"자동 전환은 0초 또는 3~60초여야 합니다.\"}");
  if (scrollSpeed < 5 || scrollSpeed > 80 || brightness < 1 || brightness > 255 ||
      nightLevel < 1 || nightLevel > 255 || ledBrightness < 1 || ledBrightness > 64 ||
      ledNightLevel < 1 || ledNightLevel > 32 || nightStart > 1439 || nightEnd > 1439 ||
      screenOff > 1440) {
    return sendJson(400, "{\"error\":\"밝기·시간·스크롤 설정 범위를 확인하세요.\"}");
  }
  uint8_t parsedOrder[VIEW_COUNT];
  if (!parseCycleOrderCount(server.arg("cycle_order"), parsedOrder, VIEW_COUNT)) return sendJson(400, "{\"error\":\"순환 순서는 0~6을 중복 없이 입력하세요.\"}");
  if (!allowedValue(ntpPeriod, ALLOWED_NTP_PERIODS) ||
      !allowedValue(ddayPeriod, ALLOWED_DDAY_PERIODS) ||
      !allowedValue(retryPeriod, ALLOWED_RETRY_PERIODS)) {
    return sendJson(400, "{\"error\":\"동기화·디데이·재시도 주기가 잘못되었습니다.\"}");
  }
  next.mode = static_cast<TopMode>(mode);
  next.ddayTextStyle = ddayTextStyle;
  next.afterComplete = afterComplete;
  next.messageLeft = messageLeft;
  next.messageScroll = messageScroll;
  next.scrollSpeed = static_cast<uint8_t>(scrollSpeed);
  next.hour24 = hour24;
  next.showSeconds = showSeconds;
  next.showChipTemperature = showChipTemperature;
  next.bootSync = bootSync;
  next.ntpPeriodSec = ntpPeriod;
  next.ddayPeriodSec = ddayPeriod;
  next.retryPeriodSec = retryPeriod;
  next.wifiSleep = wifiSleep;
  next.brightness = static_cast<uint8_t>(brightness);
  next.nightLevel = static_cast<uint8_t>(nightLevel);
  next.ledEnabled = ledEnabled;
  next.ledBrightness = static_cast<uint8_t>(ledBrightness);
  next.ledNightLevel = static_cast<uint8_t>(ledNightLevel);
  next.nightStartMin = static_cast<uint16_t>(nightStart);
  next.nightEndMin = static_cast<uint16_t>(nightEnd);
  next.burninShift = burninShift;
  next.screenOffMin = static_cast<uint16_t>(screenOff);
  next.cycleMask = static_cast<uint8_t>(cycleMask);
  memcpy(next.cycleOrder, parsedOrder, sizeof(parsedOrder));
  next.cycleIntervalSec = static_cast<uint8_t>(cycleInterval);
  if (next.mode != TopMode::SELECTED_CYCLE) next.lastView = topModeView(next.mode);
  const Config previous = config;
  config = next;
  if (!saveGeneralConfig()) {
    config = previous;
    if (!saveGeneralConfig()) logLine("configuration rollback could not be persisted");
    return sendJson(500, "{\"error\":\"설정을 저장하지 못했습니다. 저장공간 상태를 확인하세요.\"}");
  }
  currentCycleIndex = config.cycleIndex;
  if (config.mode == TopMode::SELECTED_CYCLE) {
    selectFirstEnabledCycleView();
  } else {
    currentView = topModeView(config.mode);
  }
  if (currentView == View::DEVICE_INFO) deviceInfoStartedMs = millis();
  lastTemperatureReadMs = 0;
  invalidateTimeDisplayCache();
  appliedLedColor = UINT32_MAX;
  scrollStartedMs = millis();
  lastCycleMs = millis();
  wakeDisplay();
  updateContrast();
  sendJson(200, "{\"ok\":true}");
}

void handleWifiTest() {
  if (!authorizePortalRequest()) return;
  String ssid = server.arg("ssid");
  String password = server.arg("pass");
  String securityText = server.arg("security");
  String username = server.arg("username");
  String identity = server.arg("identity");
  const WifiSecurityType security = securityText == "enterprise_peap"
                                      ? WifiSecurityType::ENTERPRISE_PEAP
                                      : WifiSecurityType::PERSONAL;
  if (ssid.length() == 0 || ssid.length() > 32) return sendJson(400, "{\"error\":\"SSID를 확인하세요.\"}");
  if (securityText != "personal" && securityText != "enterprise_peap") {
    return sendJson(400, "{\"error\":\"Wi-Fi 보안 방식이 잘못되었습니다.\"}");
  }
  int savedIndex = findSavedNetwork(ssid);
  if (savedIndex >= 0 && config.savedNetworks[savedIndex].security == security) {
    if (security == WifiSecurityType::ENTERPRISE_PEAP) {
      const bool reuseStoredCredentials = password.length() == 0 && username.length() == 0 && identity.length() == 0;
      if (reuseStoredCredentials) {
        password = config.savedNetworks[savedIndex].password;
        username = config.savedNetworks[savedIndex].username;
        identity = config.savedNetworks[savedIndex].identity;
      } else {
        if (password.length() == 0) password = config.savedNetworks[savedIndex].password;
        if (username.length() == 0) username = config.savedNetworks[savedIndex].username;
      }
    } else if (password.length() == 0) {
      password = config.savedNetworks[savedIndex].password;
    }
  }
  if (security == WifiSecurityType::PERSONAL) {
    if (password.length() > 63) return sendJson(400, "{\"error\":\"Wi-Fi 비밀번호가 너무 깁니다.\"}");
    username = "";
    identity = "";
  } else {
    if (!enterpriseWifiSupported()) {
      return sendJson(501, "{\"error\":\"이 ESP32 빌드에서는 Enterprise Wi-Fi를 지원하지 않습니다.\"}");
    }
    if (username.length() == 0 || username.length() > 64) {
      return sendJson(400, "{\"error\":\"Enterprise 사용자명은 1~64자여야 합니다.\"}");
    }
    if (password.length() == 0 || password.length() > 64) {
      return sendJson(400, "{\"error\":\"Enterprise 비밀번호는 1~64자여야 합니다.\"}");
    }
    if (identity.length() > 64) {
      return sendJson(400, "{\"error\":\"Enterprise identity는 64자 이하여야 합니다.\"}");
    }
  }
  pendingSsid = ssid;
  pendingPass = password;
  pendingWifiSecurity = security;
  pendingWifiUsername = username;
  pendingWifiIdentity = identity;
  wifiTestError = "";
  wifiTestState = WifiTestState::CONNECTING;
  beginStationConnection(pendingSsid, pendingPass, pendingWifiSecurity,
                         pendingWifiUsername, pendingWifiIdentity, true);
  sendJson(202, "{\"ok\":true,\"state\":\"connecting\"}");
}

void handleTimeSync() {
  if (!authorizePortalRequest()) return;
  if (wifiTestState == WifiTestState::CONNECTING || wifiTestState == WifiTestState::TIME_SYNCING) {
    return sendJson(409, "{\"error\":\"Wi-Fi 시험이 진행 중입니다. 완료 후 다시 시도하세요.\"}");
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (config.savedNetworkCount == 0) return sendJson(409, "{\"error\":\"먼저 Wi-Fi에 연결하세요.\"}");
    wifiTestState = WifiTestState::IDLE;
    wifiTestError = "";
    startSavedWifiSequence(true);
    sendJson(202, "{\"ok\":true,\"state\":\"connecting\"}");
    return;
  }
  beginNtpRequest();
  setRuntimeState(RuntimeState::TIME_SYNCING);
  sendJson(202, "{\"ok\":true}");
}

void restartAfterFactoryReset() {
  delay(250);
  WiFi.disconnect(true, true);
  delay(100);
  ESP.restart();
}

void handleFactoryReset() {
  if (!authorizePortalRequest()) return;
  if (!prefsReady) return sendJson(503, "{\"error\":\"NVS 저장공간을 사용할 수 없어 공장 초기화를 실행할 수 없습니다.\"}");
  if (server.arg("confirm") != "RESET") return sendJson(400, "{\"error\":\"초기화 확인 문자열이 올바르지 않습니다.\"}");
  if (!prefs.clear()) return sendJson(500, "{\"error\":\"설정 저장공간을 초기화하지 못했습니다.\"}");
  sendJson(200, "{\"ok\":true,\"restarting\":true}");
  restartAfterFactoryReset();
}

void handleSettingsReset() {
  if (!authorizePortalRequest()) return;
  if (server.arg("confirm") != "DEFAULTS") return sendJson(400, "{\"error\":\"설정 초기화 확인 문자열이 올바르지 않습니다.\"}");

  const Config previous = config;
  const uint8_t networkCount = previous.savedNetworkCount;
  config = Config();
  config.savedNetworkCount = networkCount;
  for (uint8_t i = 0; i < networkCount; ++i) config.savedNetworks[i] = previous.savedNetworks[i];
  config.lastSync = previous.lastSync;
  config.lastDday = previous.lastDday;
  if (!saveGeneralConfig()) {
    config = previous;
    if (!saveGeneralConfig()) logLine("settings reset rollback could not be persisted");
    return sendJson(500, "{\"error\":\"기본 설정을 저장하지 못했습니다.\"}");
  }

  currentView = View::DDAY_TIME;
  currentCycleIndex = 0;
  scrollStartedMs = millis();
  lastCycleMs = millis();
  lastInteractionMs = millis();
  lastTemperatureReadMs = 0;
  invalidateTimeDisplayCache();
  appliedLedColor = UINT32_MAX;
  wakeDisplay();
  updateContrast();
  logLine("display, time, and LED settings restored to defaults; saved Wi-Fi retained");
  sendJson(200, "{\"ok\":true,\"wifi_retained\":true}");
}

void handleDeleteSavedWifi() {
  if (!authorizePortalRequest()) return;
  String ssid = server.arg("ssid");
  int index = findSavedNetwork(ssid);
  if (index < 0) return sendJson(404, "{\"error\":\"저장된 Wi-Fi를 찾을 수 없습니다.\"}");
  String connectedSsid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  const bool removedCurrentNetwork = connectedSsid == ssid;
  const Config previous = config;
  removeSavedNetworkAt(static_cast<uint8_t>(index));
  if (!saveWifiOrRestore(previous, activeWifiIndex, "saved Wi-Fi deletion rollback could not be persisted")) {
    return sendJson(500, "{\"error\":\"Wi-Fi 삭제 내용을 저장하지 못했습니다.\"}");
  }
  if (removedCurrentNetwork) {
    WiFi.scanDelete();
    WiFi.disconnect(false, true);
    activeWifiIndex = NO_WIFI_INDEX;
    if (config.savedNetworkCount > 0) startSavedWifiSequence(true);
    else setRuntimeState(RuntimeState::SETUP_AP);
  } else {
    int connectedIndex = findSavedNetwork(connectedSsid);
    activeWifiIndex = connectedIndex >= 0 ? static_cast<uint8_t>(connectedIndex) : NO_WIFI_INDEX;
  }
  logLine(String("saved Wi-Fi removed: ") + ssid);
  sendJson(200, String("{\"ok\":true,\"saved_count\":") + String(config.savedNetworkCount) + "}");
}

void handleUpdateCheck() {
  if (!authorizePortalRequest()) return;
  if (firmwareUpdateBusy() || updateState == UpdateState::CURRENT) {
    return sendJson(409, "{\"error\":\"업데이트 작업이 이미 진행 중입니다.\"}");
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    return sendJson(409, "{\"error\":\"온도 보호 상태에서는 업데이트를 확인할 수 없습니다.\"}");
  }
  if (!stationNetworkReady()) {
    return sendJson(409, "{\"error\":\"인터넷에 연결된 Wi-Fi가 필요합니다.\"}");
  }
  if (!timeIsValid() || !freshNtpSinceBoot) {
    updateCheckAfterNetworkReady = true;
    beginNtpRequest();
    setRuntimeState(RuntimeState::TIME_SYNCING);
    return sendJson(202, "{\"ok\":true,\"state\":\"time_syncing\"}");
  }
  updateCheckNotBeforeMs = millis() + UPDATE_NETWORK_SETTLE_MS;
  requestFirmwareUpdateCheck(UpdateCheckReason::MANUAL);
  sendJson(202, "{\"ok\":true,\"state\":\"queued\"}");
}

void handleUpdateInstall() {
  if (!authorizePortalRequest()) return;
  if (updateInstallRequested || firmwareTransferActive()) {
    return sendJson(409, "{\"error\":\"업데이트 설치가 이미 진행 중입니다.\"}");
  }
  if (updateState != UpdateState::AVAILABLE || latestFirmwareVersion.length() == 0 ||
      latestFirmwareSize == 0 || !validSha256(latestFirmwareSha256)) {
    return sendJson(409, "{\"error\":\"설치할 새 펌웨어가 확인되지 않았습니다.\"}");
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    return sendJson(409, "{\"error\":\"온도 보호 상태에서는 업데이트할 수 없습니다.\"}");
  }
  if (!stationNetworkReady()) {
    if (config.savedNetworkCount == 0) {
      return sendJson(409, "{\"error\":\"인터넷에 연결된 Wi-Fi가 필요합니다.\"}");
    }
    updateCheckAfterNetworkReady = true;
    startSavedWifiSequence(true);
    return sendJson(202, "{\"ok\":true,\"state\":\"reconnecting\"}");
  }
  updateInstallRequested = true;
  updateInstallNotBeforeMs = millis() + UPDATE_INSTALL_RESPONSE_HOLD_MS;
  logLine(String("portal firmware install request accepted; target=") + latestFirmwareVersion +
          ", start_after_ms=" + String(UPDATE_INSTALL_RESPONSE_HOLD_MS));
  sendJson(202, "{\"ok\":true,\"state\":\"queued\"}");
}

void handlePortalRoot() {
  if (!requireSetupApRequest(false)) return;
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Set-Cookie", String("MILESTONE_TOKEN=") + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
  server.send_P(200, "text/html; charset=UTF-8", MILESTONE_PORTAL_HTML);
}

void registerPortalRoutes() {
  static bool registered = false;
  if (registered) return;
  registered = true;
  static const char *headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);
  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/wifi/test", HTTP_POST, handleWifiTest);
  server.on("/api/wifi/delete", HTTP_POST, handleDeleteSavedWifi);
  server.on("/api/time/sync", HTTP_POST, handleTimeSync);
  server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
  server.on("/api/settings/reset", HTTP_POST, handleSettingsReset);
  server.on("/api/reset", HTTP_POST, handleFactoryReset);
  server.on("/generate_204", HTTP_GET, handlePortalRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
  server.on("/ncsi.txt", HTTP_GET, handlePortalRoot);
  server.onNotFound([]() {
    if (!requestFromSetupAp()) {
      server.send(404, "text/plain", "Not found");
      return;
    }
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  });
}

void restoreConfiguredWifiAfterFailedTest() {
  if (config.savedNetworkCount == 0) return;
  WiFi.disconnect(false, false);
  beginSavedWifiConnection(0, true);
  logLine("restoring the previously saved Wi-Fi");
}

void failWifiTest(const String &reason);

void finishWifiTestSuccess() {
  const Config previous = config;
  // Persist the non-Wi-Fi schema/defaults first so a brand-new device has a
  // valid cfg_ver even if the subsequent Wi-Fi bank transaction fails.
  if (!saveGeneralConfig()) {
    failWifiTest("Wi-Fi 확인은 성공했지만 일반 설정 저장에 실패했습니다.");
    return;
  }
  if (!upsertSavedNetwork(pendingSsid, pendingPass, pendingWifiSecurity,
                          pendingWifiUsername, pendingWifiIdentity)) {
    config = previous;
    failWifiTest("Wi-Fi 확인은 성공했지만 Wi-Fi 설정 저장에 실패했습니다.");
    return;
  }
  pendingSsid = "";
  pendingPass = "";
  pendingWifiSecurity = WifiSecurityType::PERSONAL;
  pendingWifiUsername = "";
  pendingWifiIdentity = "";
  wifiTestError = "";
  wifiTestState = WifiTestState::SUCCESS;
  markNtpSuccess();
  setRuntimeState(RuntimeState::RUNNING_ONLINE);
  startMdns();
  portalClosingAfterSuccess = true;
  portalSuccessMs = millis();
  logLine("tested Wi-Fi saved; setup portal will close");
}

void failWifiTest(const String &reason) {
  ntpRequestActive = false;
  internetVerified = false;
  wifiTestState = WifiTestState::FAILED;
  wifiTestError = reason;
  pendingSsid = "";
  pendingPass = "";
  pendingWifiSecurity = WifiSecurityType::PERSONAL;
  pendingWifiUsername = "";
  pendingWifiIdentity = "";
  setRuntimeState(RuntimeState::SETUP_AP);
  restoreConfiguredWifiAfterFailedTest();
  logLine(String("Wi-Fi test failed: ") + reason);
}

