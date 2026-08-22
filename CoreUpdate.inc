String formatEpoch(uint64_t epoch) {
  if (epoch < static_cast<uint64_t>(MIN_VALID_EPOCH)) return "";
  time_t value = static_cast<time_t>(epoch);
  tm local{};
  localtime_r(&value, &local);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S KST", &local);
  return buffer;
}

int findJsonValue(const String &json, const char *key) {
  String marker = String('"') + key + '"';
  int position = json.indexOf(marker);
  if (position < 0) return -1;
  position = json.indexOf(':', position + marker.length());
  if (position < 0) return -1;
  ++position;
  while (position < static_cast<int>(json.length()) && isspace(static_cast<unsigned char>(json[position]))) ++position;
  return position;
}

bool parseJsonStringField(const String &json, const char *key, String &value) {
  int position = findJsonValue(json, key);
  if (position < 0 || position >= static_cast<int>(json.length()) || json[position] != '"') return false;
  ++position;
  value = "";
  while (position < static_cast<int>(json.length())) {
    char c = json[position++];
    if (c == '"') return true;
    if (c == '\\') {
      if (position >= static_cast<int>(json.length())) return false;
      c = json[position++];
      char decoded = 0;
      if (!MilestoneCoreLogic::decodeJsonEscape(c, decoded)) return false;
      value += decoded;
    } else {
      value += c;
    }
    if (value.length() > 256) return false;
  }
  return false;
}

bool parseJsonUintField(const String &json, const char *key, uint32_t &value) {
  int position = findJsonValue(json, key);
  if (position < 0 || position >= static_cast<int>(json.length()) || !isDigit(json[position])) return false;
  uint64_t parsed = 0;
  while (position < static_cast<int>(json.length()) && isDigit(json[position])) {
    parsed = parsed * 10ULL + static_cast<uint8_t>(json[position++] - '0');
    if (parsed > UINT32_MAX) return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseSemanticVersion(const String &text, uint16_t parts[3]) {
  return MilestoneCoreLogic::parseSemanticVersion(text.c_str(), parts);
}

int compareSemanticVersions(const String &left, const String &right) {
  return MilestoneCoreLogic::compareSemanticVersions(left.c_str(), right.c_str());
}

bool validSha256(const String &value) {
  return MilestoneCoreLogic::validSha256(value.c_str());
}

String sha256Hex(const uint8_t digest[32]) {
  static const char hex[] = "0123456789abcdef";
  char result[65];
  for (uint8_t i = 0; i < 32; ++i) {
    result[i * 2] = hex[digest[i] >> 4];
    result[i * 2 + 1] = hex[digest[i] & 0x0F];
  }
  result[64] = '\0';
  return String(result);
}

void setUpdateState(UpdateState state) {
  updateState = state;
  updateStateStartedMs = millis();
  updateCurrentVisibleMs = 0;
  if (state != UpdateState::CHECKING) updateCheckIndicatorRendered = false;
  wakeDisplay();
  logLine(String("update state -> ") + updateStateName(state));
}

bool clearOtaAttemptRecord() {
  const bool stageCleared = removePreferenceVerified("ota_stage");
  const bool targetCleared = removePreferenceVerified("ota_target");
  return stageCleared && targetCleared;
}

bool recordOtaStage(const char *stage) {
  if (!putStringVerified("ota_stage", stage)) return false;
  logLine(String("OTA stage -> ") + stage);
  return true;
}

void restoreNetworkAfterFirmwareOperation() {
  if (WiFi.status() != WL_CONNECTED) return;
  // During NTP/update traffic the radio is intentionally kept fully awake.
  // Keep it awake after the operation as well. The user-facing "Wi-Fi sleep"
  // setting means WIFI_OFF between sync windows, not ESP modem power-save.
  // Re-enabling modem sleep here made the normal always-connected mode behave
  // differently from setup/AP testing and could reintroduce latency/dropouts.
  // Full WIFI_OFF sleep, when configured, is handled separately by
  // enterWifiSleep() after the update state machine has finished.
  WiFi.setSleep(false);
  startMdns();
}

MilestoneDiagnostics::OtaFailure classifyOtaFailure(const String &reason) {
  String normalized = reason;
  normalized.toLowerCase();
  if (normalized.indexOf("metadata") >= 0 || normalized.indexOf("release") >= 0) return MilestoneDiagnostics::OtaFailure::METADATA;
  if (normalized.indexOf("temperature") >= 0) return MilestoneDiagnostics::OtaFailure::TEMPERATURE;
  if (normalized.indexOf("ram") >= 0 || normalized.indexOf("memory") >= 0) return MilestoneDiagnostics::OtaFailure::MEMORY;
  if (normalized.indexOf("partition") >= 0) return MilestoneDiagnostics::OtaFailure::PARTITION;
  if (normalized.indexOf("sha-256") >= 0 || normalized.indexOf("sha256") >= 0) return MilestoneDiagnostics::OtaFailure::SHA256;
  if (normalized.indexOf("write") >= 0) return MilestoneDiagnostics::OtaFailure::WRITE;
  if (normalized.indexOf("finalize") >= 0 || normalized.indexOf("ota end") >= 0) return MilestoneDiagnostics::OtaFailure::FINALIZE;
  if (normalized.indexOf("rollback") >= 0) return MilestoneDiagnostics::OtaFailure::ROLLBACK;
  if (normalized.indexOf("state") >= 0 || normalized.indexOf("save") >= 0 || normalized.indexOf("progress") >= 0) return MilestoneDiagnostics::OtaFailure::STATE;
  if (normalized.indexOf("download") >= 0 || normalized.indexOf("length") >= 0 || normalized.indexOf("size header") >= 0) return MilestoneDiagnostics::OtaFailure::DOWNLOAD;
  if (normalized.indexOf("http") >= 0 || normalized.indexOf("https") >= 0 || normalized.indexOf("tls") >= 0 || normalized.indexOf("request") >= 0) return MilestoneDiagnostics::OtaFailure::HTTP;
  if (normalized.indexOf("wi-fi") >= 0 || normalized.indexOf("network") >= 0) return MilestoneDiagnostics::OtaFailure::NETWORK;
  return MilestoneDiagnostics::OtaFailure::UNKNOWN;
}

void failFirmwareCheck(const String &reason, bool transient) {
  updateError = reason;
  updateInstallRequested = false;
  updateInstallNotBeforeMs = 0;
  updatePromptVisible = false;
  nextUpdateRetryMs = millis() + (transient ? UPDATE_CHECK_TRANSIENT_RETRY_MS : UPDATE_FAILURE_RETRY_MS);
  if (!putStringVerified("ota_check", "failed")) logLine("OTA check failure metadata save failed");
  recordDiagnostic(MilestoneDiagnostics::Event::UPDATE_CHECK_FAILED,
                   static_cast<int16_t>(transient ? MilestoneDiagnostics::UpdateCheckFailure::TRANSIENT
                                                  : MilestoneDiagnostics::UpdateCheckFailure::FATAL));
  setUpdateState(UpdateState::ERROR_STATE);
  restoreBluetoothAfterFirmwareOperation();
  logLine(String("firmware check failed: ") + reason);
}

void failFirmwareUpdate(const String &reason) {
  updateError = reason;
  updateInstallRequested = false;
  updateInstallNotBeforeMs = 0;
  updatePromptVisible = false;
  nextUpdateRetryMs = millis() + UPDATE_FAILURE_RETRY_MS;
  lastOtaResult = "failed";
  if (!putStringVerified("ota_result", lastOtaResult)) logLine("OTA failure result save failed");
  if (!clearOtaAttemptRecord()) logLine("OTA attempt record clear failed");
  if (!clearRollbackActiveRecord()) logLine("OTA rollback record clear failed");
  recordDiagnostic(MilestoneDiagnostics::Event::OTA_FAILED,
                   static_cast<int16_t>(classifyOtaFailure(reason)), 0, true);
  restoreNetworkAfterFirmwareOperation();
  setUpdateState(UpdateState::ERROR_STATE);
  restoreBluetoothAfterFirmwareOperation();
  logLine(String("firmware update failed: ") + reason);
}

void deferFirmwareInstall(const String &reason) {
  updateError = reason;
  updateInstallRequested = false;
  updateInstallNotBeforeMs = 0;
  updatePromptVisible = false;
  lastOtaResult = "not-installed";
  if (!putStringVerified("ota_result", lastOtaResult)) logLine("OTA deferral result save failed");
  if (!clearOtaAttemptRecord()) logLine("OTA attempt record clear failed");
  setUpdateState(UpdateState::AVAILABLE);
  restoreBluetoothAfterFirmwareOperation();
  logLine(String("firmware installation deferred: ") + reason);
}

bool prepareUpdateNetworkRequest(const char *resource, String &failure) {
  cancelPortalWifiScan();
  if (!stationNetworkReady()) {
    failure = String(resource) + " Wi-Fi disconnected";
    return false;
  }
  // Do not call hostByName() as a separate preflight. HTTPClient/secureClient
  // must resolve the target anyway, so a second resolver transaction only
  // adds another blocking/failure point and can make a weak DNS path look less
  // reliable than it is. Log the DHCP-provided resolvers instead; the actual
  // HTTPS result below remains the authoritative GitHub-path check.
  logLine(String(resource) + " network ready: ip=" + WiFi.localIP().toString() +
          " gw=" + WiFi.gatewayIP().toString() +
          " dns0=" + WiFi.dnsIP(0).toString() +
          " dns1=" + WiFi.dnsIP(1).toString() +
          " rssi=" + String(WiFi.RSSI()) + " dBm" +
          " heap=" + ESP.getFreeHeap() + " max=" + ESP.getMaxAllocHeap() +
          " stack=" + uxTaskGetStackHighWaterMark(nullptr));
  failure = "";
  return true;
}

String describeHttpFailure(const char *resource, int response, NetworkClientSecure &secureClient) {
  char tlsDetails[160] = {};
  const int tlsError = secureClient.lastError(tlsDetails, sizeof(tlsDetails));
  const String httpDetails = response < 0 ? HTTPClient::errorToString(response) : String();

  String diagnostic = String(resource) +
                      (response < 0 ? " HTTPS transport failed before HTTP response: code="
                                    : " HTTP request failed: status=") +
                      String(response);
  if (httpDetails.length()) diagnostic += " (HTTPClient: " + httpDetails + ")";
  diagnostic += " wifi=" + String(static_cast<int>(WiFi.status())) +
                " rssi=" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) +
                " heap=" + String(ESP.getFreeHeap()) + " max=" + String(ESP.getMaxAllocHeap());
  logLine(diagnostic);
  if (tlsError != 0) {
    logLine(String(resource) + " TLS detail " + tlsError + ": " + tlsDetails);
    return String(resource) + " HTTPS transport failed";
  }
  if (response < 0 && httpDetails.length()) return String(resource) + " " + httpDetails;
  return String(resource) + " HTTP " + response;
}

bool transientHttpFailure(int response) {
  if (response < 0) return true;
  return response == HTTP_CODE_REQUEST_TIMEOUT || response == HTTP_CODE_MISDIRECTED_REQUEST ||
         response == 425 || response == HTTP_CODE_TOO_MANY_REQUESTS ||
         (response >= 500 && response <= 599);
}

UpdateCheckAttemptResult readManifestBody(HTTPClient &http, String &body, String &failure) {
  const int declaredLength = http.getSize();
  if (declaredLength > static_cast<int>(UPDATE_MANIFEST_MAX_BYTES)) {
    failure = "manifest too large";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }

  // HTTPClient::getString() routes through writeToStream(), which correctly
  // decodes both identity and chunked transfer encodings. The previous custom
  // socket loop could mistake a valid unknown-length/chunked response for a
  // timeout while a keep-alive connection remained open.
  body = http.getString();
  const int decodedLength = http.getSize();
  if (body.length() > UPDATE_MANIFEST_MAX_BYTES) {
    failure = "manifest too large";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }
  if (body.length() == 0) {
    failure = "manifest body empty or timed out";
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }
  if (declaredLength >= 0 && body.length() != static_cast<size_t>(declaredLength)) {
    failure = "manifest body incomplete";
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }
  if (decodedLength >= 0 && body.length() != static_cast<size_t>(decodedLength)) {
    failure = "manifest decoded length mismatch";
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }
  body.trim();
  // getString() intentionally hides writeToStream()'s return code. A transfer
  // interrupted while using an unknown-length response can therefore leave a
  // non-empty partial String. Require a complete JSON object envelope before
  // trusting any fields so a truncated 200 response is retried rather than
  // accidentally accepted as a valid release manifest.
  if (!body.startsWith("{") || !body.endsWith("}")) {
    failure = "manifest body incomplete";
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }
  failure = "";
  return UpdateCheckAttemptResult::SUCCESS;
}

void requestFirmwareUpdateCheck(UpdateCheckReason reason, const String &profile) {
  if (pendingUpdateCheckReason != UpdateCheckReason::NONE || firmwareUpdateBusy() ||
      updateState == UpdateState::CURRENT) return;
  if (!validFirmwareProfile(profile)) return;
  requestedUpdateProfile = profile;
  latestFirmwareVersion = "";
  latestFirmwareProfile = "";
  latestFirmwareAsset = "";
  latestFirmwareSha256 = "";
  latestFirmwareNotes = "";
  latestFirmwareSize = 0;
  pendingUpdateCheckReason = reason;
  updateCheckAttempt = 0;
  updateCheckIndicatorRendered = false;
  updateError = "";
}

UpdateCheckAttemptResult checkFirmwareManifestAttempt(UpdateCheckReason reason, String &failure) {
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    failure = "temperature protection active";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }
  if (!stationNetworkReady()) {
    failure = "Wi-Fi disconnected";
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }
  if (!timeIsValid() || !freshNtpSinceBoot) {
    failure = "secure clock unavailable";
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }
  if (ESP.getFreeHeap() < UPDATE_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < UPDATE_MIN_LARGEST_BLOCK) {
    failure = "not enough internal RAM";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }

  WiFi.setSleep(false);
  if (!prepareUpdateNetworkRequest("manifest", failure)) {
    return UpdateCheckAttemptResult::RETRYABLE_FAILURE;
  }

  logLine(String("manifest HTTPS attempt ") + String(updateCheckAttempt) + "/" +
          String(UPDATE_CHECK_MAX_ATTEMPTS));
  NetworkClientSecure secureClient;
  secureClient.setCACert(MILESTONE_UPDATE_ROOT_CA);
  secureClient.setHandshakeTimeout(portalActive ? UPDATE_PORTAL_TLS_HANDSHAKE_TIMEOUT_SEC
                                                : UPDATE_TLS_HANDSHAKE_TIMEOUT_SEC);
  HTTPClient http;
  http.setConnectTimeout(portalActive ? UPDATE_PORTAL_HTTP_CONNECT_TIMEOUT_MS
                                      : UPDATE_HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(portalActive ? UPDATE_PORTAL_HTTP_TIMEOUT_MS : UPDATE_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(UPDATE_REDIRECT_LIMIT);
  http.setReuse(false);
  http.setAcceptEncoding("identity");
  http.setUserAgent(String("MILESTONE-") + FIRMWARE_PROFILE_LABEL + '/' + FIRMWARE_VERSION);
  const String manifestUrl = String(UPDATE_LATEST_BASE_URL) + manifestAssetForProfile(requestedUpdateProfile);
  if (!http.begin(secureClient, manifestUrl)) {
    failure = "manifest request setup failed";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  const int response = http.GET();
  if (response != HTTP_CODE_OK) {
    failure = describeHttpFailure("manifest", response, secureClient);
    http.end();
    return transientHttpFailure(response) ? UpdateCheckAttemptResult::RETRYABLE_FAILURE
                                          : UpdateCheckAttemptResult::FATAL_FAILURE;
  }

  String manifest;
  UpdateCheckAttemptResult bodyResult = readManifestBody(http, manifest, failure);
  http.end();
  if (bodyResult != UpdateCheckAttemptResult::SUCCESS) return bodyResult;

  String version, profile, asset, sha256, notes;
  uint32_t size = 0;
  uint16_t versionParts[3];
  if (!parseJsonStringField(manifest, "version", version) || !parseSemanticVersion(version, versionParts) ||
      !parseJsonStringField(manifest, "profile", profile) || profile != requestedUpdateProfile ||
      !parseJsonStringField(manifest, "asset", asset) || asset != firmwareAssetForProfile(profile) ||
      !parseJsonUintField(manifest, "size", size) || size == 0 ||
      !parseJsonStringField(manifest, "sha256", sha256) || !validSha256(sha256)) {
    failure = "invalid release manifest";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }
  parseJsonStringField(manifest, "notes", notes);
  sha256.toLowerCase();
  latestFirmwareVersion = version;
  latestFirmwareProfile = profile;
  latestFirmwareAsset = asset;
  latestFirmwareSize = size;
  latestFirmwareSha256 = sha256;
  latestFirmwareNotes = notes;
  lastUpdateCheckEpoch = static_cast<uint64_t>(time(nullptr));
  if (prefs.putULong64("ota_last_ok", lastUpdateCheckEpoch) != sizeof(uint64_t) ||
      !putStringVerified("ota_latest", latestFirmwareVersion) ||
      !putStringVerified("ota_check", "ok")) {
    logLine("OTA check metadata save failed");
  }
  bootUpdateCheckPending = false;
  nextUpdateRetryMs = 0;
  updateError = "";

  const bool profileSwitch = latestFirmwareProfile != FIRMWARE_PROFILE;
  const int versionComparison = compareSemanticVersions(FIRMWARE_VERSION, latestFirmwareVersion);
  if (profileSwitch && versionComparison > 0) {
    failure = "target profile release is older than running firmware";
    return UpdateCheckAttemptResult::FATAL_FAILURE;
  }
  if (profileSwitch || versionComparison < 0) {
    // An available release is one of the two update-check outcomes that should
    // deliberately interrupt the OLED (the other is a check error). Do this for
    // both automatic and portal-initiated checks so a manual check cannot hide
    // the device-side notification.
    updatePromptVisible = true;
    updatePromptStartedMs = millis();
    setUpdateState(UpdateState::AVAILABLE);
    logLine(String(profileSwitch ? "firmware profile switch available: " : "firmware update available: ") +
            currentFirmwareIdentity() + " -> " +
            firmwareIdentity(latestFirmwareVersion, latestFirmwareProfile));
    failure = "";
    return UpdateCheckAttemptResult::SUCCESS;
  }

  updatePromptVisible = false;
  setUpdateState(UpdateState::CURRENT);
  updateCurrentVisibleMs = millis();
  logLine(String("firmware is current: ") + currentFirmwareIdentity());
  failure = "";
  return UpdateCheckAttemptResult::SUCCESS;
}

bool installFirmwareUpdate() {
  if (updateState != UpdateState::AVAILABLE || latestFirmwareVersion.length() == 0 ||
      latestFirmwareProfile.length() == 0 || latestFirmwareAsset.length() == 0 ||
      latestFirmwareSize == 0 || !validSha256(latestFirmwareSha256)) {
    failFirmwareUpdate("release metadata unavailable");
    return false;
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning ||
      (!isnan(chipTemperatureC) && chipTemperatureC >= THERMAL_THROTTLE_C)) {
    deferFirmwareInstall("temperature too high");
    return false;
  }
  if (!stationNetworkReady()) {
    deferFirmwareInstall("Wi-Fi disconnected");
    return false;
  }
  isolateBluetoothForFirmwareOperation();
  if (ESP.getFreeHeap() < UPDATE_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < UPDATE_MIN_LARGEST_BLOCK) {
    deferFirmwareInstall("not enough internal RAM");
    return false;
  }
  if (latestFirmwareSize > ESP.getFreeSketchSpace()) {
    deferFirmwareInstall("OTA partition too small");
    return false;
  }
  String networkFailure;
  if (!prepareUpdateNetworkRequest("firmware", networkFailure)) {
    deferFirmwareInstall(networkFailure.length() ? networkFailure : "firmware network unavailable");
    return false;
  }

  // Keep the setup AP and the already loaded portal page alive until the
  // verified image is ready to reboot. The synchronous transfer temporarily
  // pauses HTTP status polling, but it must not disconnect the user at start.
  stopMdns();
  WiFi.scanDelete();
  portalWifiScanActive = false;
  portalWifiScanDeadlineMs = 0;
  WiFi.setSleep(false);
  updatePromptVisible = false;
  updateDownloadedBytes = 0;
  const String targetIdentity = firmwareIdentity(latestFirmwareVersion, latestFirmwareProfile);
  if (!prepareRollbackRecord(targetIdentity)) {
    deferFirmwareInstall("rollback source preparation failed");
    return false;
  }
  lastOtaResult = "installing";
  if (!putStringVerified("ota_result", lastOtaResult) ||
      !putStringVerified("ota_target", targetIdentity) ||
      !recordOtaStage("starting")) {
    failFirmwareUpdate("OTA state save failed");
    return false;
  }
  recordDiagnostic(MilestoneDiagnostics::Event::OTA_START, 0,
                   latestFirmwareSize > MilestoneDiagnostics::VALUE_MAX ? MilestoneDiagnostics::VALUE_MAX : static_cast<int32_t>(latestFirmwareSize), true);
  setUpdateState(UpdateState::DOWNLOADING);
  drawUpdateScreen();

  const String firmwareUrl = String(UPDATE_RELEASE_BASE_URL) + latestFirmwareVersion + '/' + latestFirmwareAsset;
  NetworkClientSecure secureClient;
  secureClient.setCACert(MILESTONE_UPDATE_ROOT_CA);
  secureClient.setHandshakeTimeout(UPDATE_TLS_HANDSHAKE_TIMEOUT_SEC);
  HTTPClient http;
  http.setConnectTimeout(UPDATE_HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(UPDATE_DOWNLOAD_STALL_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(UPDATE_REDIRECT_LIMIT);
  http.setReuse(false);
  http.setAcceptEncoding("identity");
  http.setUserAgent(String("MILESTONE-") + FIRMWARE_PROFILE_LABEL + '/' + FIRMWARE_VERSION);
  if (!http.begin(secureClient, firmwareUrl)) {
    failFirmwareUpdate("firmware request setup failed");
    return false;
  }
  http.addHeader("Accept", "application/octet-stream");
  int response = http.GET();
  if (response != HTTP_CODE_OK) {
    const String failure = describeHttpFailure("firmware", response, secureClient);
    http.end();
    failFirmwareUpdate(failure);
    return false;
  }
  int responseSize = http.getSize();
  if (responseSize < 0) {
    http.end();
    failFirmwareUpdate("firmware size header missing");
    return false;
  }
  if (static_cast<uint32_t>(responseSize) != latestFirmwareSize) {
    http.end();
    failFirmwareUpdate("firmware size header mismatch");
    return false;
  }
  if (!Update.begin(latestFirmwareSize, U_FLASH)) {
    http.end();
    failFirmwareUpdate(String("OTA begin: ") + Update.errorString());
    return false;
  }
  if (!recordOtaStage("downloading")) {
    http.end();
    Update.abort();
    failFirmwareUpdate("OTA progress save failed");
    return false;
  }

  // OTA never runs inside STREAM_MODE, so do not permanently reserve this
  // internal-SRAM transfer buffer. Allocate it only for the active download
  // and release it before verification/finalization.
  uint8_t *updateDownloadBuffer = static_cast<uint8_t *>(
      heap_caps_malloc(UPDATE_DOWNLOAD_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!updateDownloadBuffer) {
    http.end();
    Update.abort();
    failFirmwareUpdate("OTA transfer buffer allocation failed");
    return false;
  }

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts(&shaContext, 0);
  NetworkClient *stream = http.getStreamPtr();
  uint32_t lastDataMs = millis();
  bool failed = false;
  String failure;
  while (updateDownloadedBytes < latestFirmwareSize) {
    int available = stream->available();
    if (available > 0) {
      size_t remaining = latestFirmwareSize - updateDownloadedBytes;
      size_t wanted = min(min(static_cast<size_t>(available), UPDATE_DOWNLOAD_BUFFER_BYTES), remaining);
      int count = stream->read(updateDownloadBuffer, wanted);
      if (count <= 0) continue;
      mbedtls_sha256_update(&shaContext, updateDownloadBuffer, count);
      if (Update.write(updateDownloadBuffer, count) != static_cast<size_t>(count)) {
        failed = true;
        failure = String("OTA write: ") + Update.errorString();
        break;
      }
      updateDownloadedBytes += static_cast<uint32_t>(count);
      lastDataMs = millis();
      drawUpdateScreen();
      processLed();
      refreshChipTemperature();
      if (temperatureSensorFault || (!isnan(chipTemperatureC) && chipTemperatureC >= THERMAL_THROTTLE_C)) {
        failed = true;
        failure = "temperature protection stopped update";
        break;
      }
      delay(1);  // Feed loopTask/Wi-Fi watchdogs even while data is continuous.
    } else {
      if (!http.connected() || elapsed(millis(), lastDataMs, UPDATE_DOWNLOAD_STALL_MS)) {
        failed = true;
        failure = "firmware download interrupted";
        break;
      }
      processLed();
      delay(1);
    }
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&shaContext, digest);
  mbedtls_sha256_free(&shaContext);
  http.end();
  heap_caps_free(updateDownloadBuffer);
  updateDownloadBuffer = nullptr;

  if (failed || updateDownloadedBytes != latestFirmwareSize) {
    Update.abort();
    failFirmwareUpdate(failure.length() ? failure : "firmware length mismatch");
    return false;
  }
  if (!recordOtaStage("verifying")) {
    Update.abort();
    failFirmwareUpdate("OTA verification state save failed");
    return false;
  }
  setUpdateState(UpdateState::VERIFYING);
  drawUpdateScreen();
  if (!sha256Hex(digest).equalsIgnoreCase(latestFirmwareSha256)) {
    Update.abort();
    failFirmwareUpdate("SHA-256 mismatch");
    return false;
  }
  if (!Update.end()) {
    const String failure = String("OTA finalize: ") + Update.errorString();
    Update.abort();
    failFirmwareUpdate(failure);
    return false;
  }

  lastOtaResult = "installed";
  if (!putStringVerified("ota_result", lastOtaResult) ||
      !recordOtaStage("rebooting") || !armRollbackRecord()) {
    const esp_err_t rollbackResult = esp_ota_set_boot_partition(esp_ota_get_running_partition());
    clearRollbackActiveRecord();
    failFirmwareUpdate(rollbackResult == ESP_OK ? "OTA reboot/rollback state save failed" :
                                                "OTA reboot state and boot rollback failed");
    return false;
  }
  recordDiagnostic(MilestoneDiagnostics::Event::OTA_READY, 0,
                   latestFirmwareSize > MilestoneDiagnostics::VALUE_MAX ? MilestoneDiagnostics::VALUE_MAX : static_cast<int32_t>(latestFirmwareSize), true);
  setUpdateState(UpdateState::READY_TO_REBOOT);
  drawUpdateScreen();
  processLed();
  logLine(String("firmware verified; rebooting into ") + targetIdentity);
  delay(1200);
  ESP.restart();
  return true;
}
