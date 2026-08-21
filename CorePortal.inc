bool rejectPortalWorkDuringStream() {
  if (!mediaStreamActive) return false;
  server.send(423, "application/json; charset=UTF-8", "{\"error\":\"stream-active\"}");
  return true;
}

void handleStatus() {
  if (!requireSetupApRequest()) return;
  if (mediaStreamActive) {
    server.send(200, "application/json; charset=UTF-8", "{\"stream_active\":true}");
    return;
  }
  String wifiLabel;
  if (runtimeState == RuntimeState::WIFI_SLEEP) wifiLabel = "sleep";
  else if (WiFi.status() == WL_CONNECTED) wifiLabel = String(WiFi.SSID()) + " (" + WiFi.RSSI() + " dBm)";
  else wifiLabel = "disconnected";
  String ip = portalActive ? WiFi.softAPIP().toString() : (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "");
  String json = "{";
  json.reserve(1024);
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"profile\":\"" + String(FIRMWARE_PROFILE) + "\",";
  json += "\"profile_label\":\"" + String(FIRMWARE_PROFILE_LABEL) + "\",";
  json += "\"media_supported\":" + String(MILESTONE_HAS_MEDIA ? "true" : "false") + ",";
  json += "\"bluetooth_supported\":" + String(MILESTONE_HAS_BLUETOOTH ? "true" : "false") + ",";
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
  json += "\"bluetooth_enabled\":" + String(config.bluetoothNowPlaying ? "true" : "false") + ",";
  json += "\"bluetooth_active\":" + String(bluetoothNowPlayingRuntimeActive() ? "true" : "false") + ",";
  json += "\"bluetooth_connected\":" + String(bluetoothNowPlayingConnected() ? "true" : "false") + ",";
  json += "\"bluetooth_ams_ready\":" + String(bluetoothNowPlayingAmsReady() ? "true" : "false") + ",";
  json += "\"bluetooth_advertising\":" + String(bluetoothNowPlayingAdvertising() ? "true" : "false") + ",";
  json += "\"bluetooth_stage\":\"" + jsonEscape(bluetoothNowPlayingStage()) + "\",";
  json += "\"bluetooth_error\":\"" + jsonEscape(bluetoothNowPlayingError()) + "\",";
  json += "\"bluetooth_error_code\":" + String(bluetoothNowPlayingErrorCode()) + ",";
  json += "\"last_sync\":\"" + jsonEscape(formatEpoch(config.lastSync)) + "\",";
  json += "\"wifi_test\":\"" + String(wifiTestStateName(wifiTestState)) + "\",";
  json += "\"wifi_error\":\"" + jsonEscape(wifiTestError) + "\",";
  json += "\"update_state\":\"" + String(updateStateName(updateState)) + "\",";
  json += "\"latest_firmware\":\"" + jsonEscape(latestFirmwareVersion) + "\",";
  json += "\"latest_profile\":\"" + jsonEscape(latestFirmwareProfile) + "\",";
  json += "\"update_available\":" + String(updateState == UpdateState::AVAILABLE ? "true" : "false") + ",";
  const bool updateInstallReady = updateState == UpdateState::AVAILABLE && latestFirmwareVersion.length() > 0 &&
                                  validFirmwareProfile(latestFirmwareProfile) && latestFirmwareAsset.length() > 0 &&
                                  latestFirmwareSize > 0 && validSha256(latestFirmwareSha256);
  json += "\"update_install_ready\":" + String(updateInstallReady ? "true" : "false") + ",";
  json += "\"update_install_pending\":" + String(updateInstallRequested ? "true" : "false") + ",";
  const uint32_t updateProgress = latestFirmwareSize > 0
                                    ? static_cast<uint32_t>((updateDownloadedBytes * 100ULL) / latestFirmwareSize) : 0;
  json += "\"update_progress\":" + String(updateProgress) + ",";
  json += "\"update_error\":\"" + jsonEscape(updateError) + "\",";
  json += "\"last_ota_result\":\"" + jsonEscape(lastOtaResult) + "\",";
  json += "\"rollback_armed\":" + String(otaRollbackArmed ? "true" : "false") + ",";
  json += "\"rollback_previous\":\"" + jsonEscape(otaRollbackPreviousVersion) + "\",";
  json += "\"rollback_attempt\":" + String(otaRollbackBootAttempts) + ",";
  json += "\"rollback_last\":\"" + jsonEscape(otaRollbackLastAction) + "\",";
  json += "\"rollback_reason\":\"" + jsonEscape(otaRollbackLastReason) + "\",";
  json += "\"last_update_check\":\"" + jsonEscape(formatEpoch(lastUpdateCheckEpoch)) + "\"}";
  sendJson(200, json);
}

void handleDiagnostics() {
  if (!requireSetupApRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  String json = "{";
  json.reserve(4096);
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"storage_ready\":" + String(diagnosticsPrefsReady ? "true" : "false") + ",";
  json += "\"boot_validated\":" + String(diagnosticBootValidated ? "true" : "false") + ",";
  json += "\"last_boot\":\"" + jsonEscape(resetReasonName(esp_reset_reason())) + "\",";
  if (isnan(highestChipTemperatureC)) json += "\"max_temperature_c\":null,";
  else json += "\"max_temperature_c\":" + String(highestChipTemperatureC, 1) + ",";
  json += "\"last_ota_result\":\"" + jsonEscape(lastOtaResult) + "\",";
  json += "\"rollback_last\":\"" + jsonEscape(otaRollbackLastAction) + "\",";
  json += "\"rollback_reason\":\"" + jsonEscape(otaRollbackLastReason) + "\",";
  json += "\"history_count\":" + String(diagnosticHistory.count) + ",";
  uint32_t lastValidatedUptime = 0;
  for (uint8_t i = 0; i < diagnosticHistory.count; ++i) {
    MilestoneDiagnostics::Record candidate;
    if (!MilestoneDiagnostics::newest(diagnosticHistory, i, candidate)) break;
    if (candidate.event == MilestoneDiagnostics::Event::BOOT_VALIDATED) {
      lastValidatedUptime = candidate.uptimeSec;
      break;
    }
  }
  json += "\"last_validated_uptime_sec\":" + String(lastValidatedUptime) + ",";

  MilestoneDiagnostics::Record lastError;
  if (latestDiagnosticError(lastError)) {
    json += "\"last_error\":{\"event\":\"";
    json += MilestoneDiagnostics::eventName(lastError.event);
    json += "\",\"detail\":\"" + jsonEscape(diagnosticDetailText(lastError)) + "\",";
    json += "\"time\":\"" + jsonEscape(formatEpoch(lastError.epoch)) + "\",";
    json += "\"uptime_sec\":" + String(lastError.uptimeSec) + "},";
  } else {
    json += "\"last_error\":null,";
  }

  json += "\"events\":[";
  for (uint8_t i = 0; i < diagnosticHistory.count; ++i) {
    MilestoneDiagnostics::Record record;
    if (!MilestoneDiagnostics::newest(diagnosticHistory, i, record)) break;
    if (i) json += ',';
    json += "{\"event\":\"";
    json += MilestoneDiagnostics::eventName(record.event);
    json += "\",\"time\":\"" + jsonEscape(formatEpoch(record.epoch)) + "\",";
    json += "\"uptime_sec\":" + String(record.uptimeSec) + ",";
    json += "\"detail_code\":" + String(record.detail) + ",";
    json += "\"value\":" + String(record.value) + ",";
    json += "\"detail\":\"" + jsonEscape(diagnosticDetailText(record)) + "\",";
    json += "\"error\":" + String(MilestoneDiagnostics::isError(record.event) ? "true" : "false") + "}";
  }
  json += "]}";
  sendJson(200, json);
}

void handleDiagnosticsClear() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  if (!clearDiagnosticHistory()) {
    return sendJson(500, "{\"error\":\"진단 이력을 초기화하지 못했습니다.\"}");
  }
  sendJson(200, "{\"ok\":true}");
}

bool parseUintArg(const char *name, uint32_t &value);
bool parseUintHeader(const char *name, uint32_t &value);
bool parseBoolArg(const char *name, bool &value);

void resetMediaUploadState(bool removeTemporary) {
  if (mediaUploadFile) mediaUploadFile.close();
  if (removeTemporary && mediaStorageReady && LittleFS.exists(MEDIA_UPLOAD_TEMP)) {
    LittleFS.remove(MEDIA_UPLOAD_TEMP);
  }
  mediaUploadActive = false;
  mediaUploadComplete = false;
  mediaUploadFailed = false;
  mediaUploadExpectedSize = 0;
  mediaUploadWritten = 0;
  mediaUploadDisplaySeconds = 8;
  mediaUploadName = "";
  mediaUploadError = "";
}

void refreshMediaViewAfterCatalogChange() {
  closeMediaPlayback();
  if (currentView != View::CUSTOM_MEDIA && config.mode == TopMode::CUSTOM_MEDIA && mediaHasEnabledItems()) {
    currentView = View::CUSTOM_MEDIA;
  }
  if (currentView == View::CUSTOM_MEDIA) {
    if (mediaHasEnabledItems()) selectNextMediaItem();
    else currentView = View::DASHBOARD;
  }
  lastCycleMs = millis();
  wakeDisplay();
}

bool authorizeMediaMutation() {
  if (!authorizePortalRequest()) return false;
  if (!MILESTONE_HAS_MEDIA) {
    sendJson(409, "{\"error\":\"MEDIA 펌웨어로 전환한 뒤 사용할 수 있습니다.\"}");
    return false;
  }
  if (firmwareUpdateBusy() || mediaTransferBusy()) {
    sendJson(409, "{\"error\":\"다른 저장 작업이 진행 중입니다.\"}");
    return false;
  }
  if (thermalWarning || temperatureSensorFault || thermalSafeMode) {
    sendJson(409, "{\"error\":\"온도 보호 상태에서는 미디어를 변경할 수 없습니다.\"}");
    return false;
  }
  return true;
}

void handleMediaStatus() {
  if (!requireSetupApRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  const size_t total = mediaStorageReady ? LittleFS.totalBytes() : 0;
  const size_t used = mediaStorageReady ? LittleFS.usedBytes() : 0;
  String json = "{";
  json.reserve(1024);
  json += "\"supported\":" + String(MILESTONE_HAS_MEDIA ? "true" : "false") + ",";
  json += "\"ready\":" + String(mediaStorageReady ? "true" : "false") + ",";
  json += "\"psram\":" + String(psramFound() ? "true" : "false") + ",";
  json += "\"total_bytes\":" + String(total) + ",";
  json += "\"filesystem_used_bytes\":" + String(used) + ",";
  json += "\"media_limit_bytes\":" + String(mediaStorageLimit()) + ",";
  json += "\"media_used_bytes\":" + String(mediaCatalogBytesUsed(mediaCatalog)) + ",";
  json += "\"item_count\":" + String(mediaCatalog.count) + ",";
  json += "\"max_items\":" + String(MEDIA_MAX_ITEMS) + ",";
  json += "\"upload_busy\":" + String(mediaUploadActive ? "true" : "false") + "}";
  sendJson(200, json);
}

void handleMediaList() {
  if (!requireSetupApRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  String json = "{\"items\":[";
  json.reserve(512 + static_cast<size_t>(mediaCatalog.count) * 160U);
  for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
    if (i) json += ',';
    const MediaEntry &entry = mediaCatalog.entries[i];
    json += "{\"id\":" + String(entry.id) + ",";
    json += "\"name\":\"" + jsonEscape(String(entry.name)) + "\",";
    json += "\"size\":" + String(entry.fileSize) + ",";
    json += "\"frames\":" + String(entry.frameCount) + ",";
    json += "\"duration_ms\":" + String(entry.durationMs) + ",";
    json += "\"display_seconds\":" + String(entry.displaySeconds) + ",";
    json += "\"animated\":" + String((entry.flags & MEDIA_ENTRY_ANIMATED) ? "true" : "false") + ",";
    json += "\"loop\":" + String((entry.flags & MEDIA_ENTRY_LOOP) ? "true" : "false") + ",";
    json += "\"enabled\":" + String((entry.flags & MEDIA_ENTRY_ENABLED) ? "true" : "false") + "}";
  }
  json += "]}";
  sendJson(200, json);
}

String mediaStreamStatusJson() {
  String json;
  json.reserve(500);
  json = "{";
  json += "\"active\":" + String(mediaStreamActive ? "true" : "false") + ",";
  json += "\"phase\":\"" + String(mediaStreamPhaseName()) + "\",";
  json += "\"session\":" + String(mediaStreamSessionId) + ",";
  json += "\"fps\":" + String(mediaStreamFps) + ",";
  json += "\"actual_fps\":" + String(mediaStreamActualFpsX10 / 10.0f, 1) + ",";
  json += "\"queue\":" + String(mediaStreamQueueCount) + ",";
  json += "\"queue_capacity\":" + String(MEDIA_STREAM_QUEUE_FRAMES) + ",";
  json += "\"source_ended\":" + String(mediaStreamSourceEnded ? "true" : "false") + ",";
  json += "\"push_max\":" + String(MEDIA_STREAM_MAX_PUSH_FRAMES) + ",";
  json += "\"received\":" + String(mediaStreamReceivedFrames) + ",";
  json += "\"encoded_bytes\":" + String(mediaStreamEncodedBytes) + ",";
  json += "\"displayed\":" + String(mediaStreamDisplayedFrames) + ",";
  json += "\"dropped\":" + String(mediaStreamDroppedFrames) + ",";
  json += "\"underruns\":" + String(mediaStreamUnderruns) + ",";
  json += "\"flush_us\":" + String(mediaStreamFlushEmaUs) + ",";
  json += "\"render_interval_ms\":" + String(mediaStreamRenderIntervalMs) + ",";
  json += "\"dirty_tiles\":" + String(mediaStreamLastDirtyTiles) + ",";
  json += "\"temperature_c\":" + String(isnan(chipTemperatureC) ? 0.0f : chipTemperatureC, 1) + ",";
  json += "\"heap_min\":" + String(mediaStreamMinFreeHeap == 0xFFFFFFFFUL ? 0 : mediaStreamMinFreeHeap) + ",";
  json += "\"stack_min\":" + String(mediaStreamMinStackFree == 0xFFFFFFFFUL ? 0 : mediaStreamMinStackFree) + ",";
  json += "\"stop_reason\":\"" + String(mediaStreamLastStopReason) + "\"}";
  return json;
}

void sendMediaStreamAck() {
  uint8_t ack[28] = {'M', 'S', 'A', '1', 1, static_cast<uint8_t>(mediaStreamPhase),
                     mediaStreamQueueCount, MEDIA_STREAM_QUEUE_FRAMES};
  MilestoneMedia::writeLe32(ack + 8, mediaStreamReceivedFrames);
  MilestoneMedia::writeLe32(ack + 12, mediaStreamDisplayedFrames);
  MilestoneMedia::writeLe32(ack + 16, mediaStreamDroppedFrames);
  MilestoneMedia::writeLe16(ack + 20, static_cast<uint16_t>(mediaStreamUnderruns > 65535U ? 65535U : mediaStreamUnderruns));
  const int16_t tempDeci = isnan(chipTemperatureC) ? 0 : static_cast<int16_t>(chipTemperatureC * 10.0f);
  MilestoneMedia::writeLe16(ack + 22, static_cast<uint16_t>(tempDeci));
  MilestoneMedia::writeLe16(ack + 24, mediaStreamActualFpsX10);
  const uint32_t flush10 = mediaStreamFlushEmaUs / 10U;
  MilestoneMedia::writeLe16(ack + 26, static_cast<uint16_t>(flush10 > 65535U ? 65535U : flush10));
  server.setContentLength(sizeof(ack));
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char *>(ack), sizeof(ack));
}

void handleMediaStreamStatus() {
  if (!requireSetupApRequest()) return;
  sendJson(200, mediaStreamStatusJson());
}

void handleMediaStreamStart() {
  if (!authorizePortalRequest()) return;
  if (!MILESTONE_HAS_MEDIA) {
    return sendJson(409, "{\"error\":\"MEDIA 펌웨어로 전환한 뒤 사용할 수 있습니다.\"}");
  }
  if (mediaStreamActive) {
    return sendJson(409, "{\"error\":\"이미 다른 스트리밍 세션이 실행 중입니다.\"}");
  }
  if (mediaUploadActive || firmwareUpdateBusy() || wifiTestState != WifiTestState::IDLE) {
    return sendJson(409, "{\"error\":\"미디어 저장·Wi-Fi 시험·펌웨어 작업이 끝난 뒤 시작하세요.\"}");
  }
  refreshChipTemperature(true);
  if (temperatureSensorFault || thermalSafeMode || thermalThrottled ||
      (!isnan(chipTemperatureC) && chipTemperatureC >= MEDIA_STREAM_STOP_C)) {
    return sendJson(409, "{\"error\":\"현재 온도 또는 센서 상태에서는 STREAM_MODE를 시작할 수 없습니다.\"}");
  }
  uint32_t fps = 0;
  if (!parseUintArg("fps", fps) || fps < MEDIA_STREAM_MIN_FPS || fps > MEDIA_STREAM_MAX_FPS) {
    return sendJson(400, "{\"error\":\"스트리밍 FPS는 5~24 범위여야 합니다.\"}");
  }
  if (!startMediaStream(static_cast<uint16_t>(fps))) {
    return sendJson(503, "{\"error\":\"PSRAM 스트림 버퍼를 확보하지 못했습니다.\"}");
  }
  portalClosingAfterSuccess = false;
  portalStartedMs = millis();
  wakeDisplay();
  sendJson(200, mediaStreamStatusJson());
}

void handleMediaStreamPushRaw() {
  HTTPRaw &raw = server.raw();
  if (raw.status == RAW_START) {
    resetMediaStreamRawState();
    if (!requestFromSetupAp() || !validToken()) {
      mediaStreamRawFailed = true;
      mediaStreamRawError = "unauthorized";
      return;
    }
    if (!mediaStreamActive || temperatureSensorFault || thermalSafeMode || firmwareUpdateBusy()) {
      mediaStreamRawFailed = true;
      mediaStreamRawError = "stream-inactive";
      return;
    }
    // Arduino-ESP32 WebServer 3.3.11 does not parse URL query arguments on
    // routes that use the raw-body callback. Stream metadata therefore travels
    // in explicitly collected headers while the request body stays frame-only.
    uint32_t session = 0, seq = 0, count = 0, encodedBytes = 0;
    if (!parseUintHeader("X-MILESTONE-Session", session) ||
        !parseUintHeader("X-MILESTONE-Seq", seq) ||
        !parseUintHeader("X-MILESTONE-Count", count) ||
        !parseUintHeader("X-MILESTONE-Bytes", encodedBytes)) {
      mediaStreamRawFailed = true;
      mediaStreamRawError = "invalid-stream-metadata";
      return;
    }
    if (count == 0 || count > MEDIA_STREAM_MAX_PUSH_FRAMES) {
      mediaStreamRawFailed = true;
      mediaStreamRawError = "invalid-frame-count";
      return;
    }
    beginMediaStreamRawWrite(session, seq, static_cast<uint8_t>(count),
                             static_cast<size_t>(encodedBytes));
    return;
  }
  if (raw.status == RAW_WRITE) {
    if (!mediaStreamRawFailed) writeMediaStreamRawChunk(raw.buf, raw.currentSize);
    return;
  }
  if (raw.status == RAW_END) {
    if (!mediaStreamRawFailed) finishMediaStreamRawWrite(raw.totalSize);
    return;
  }
  if (raw.status == RAW_ABORTED) {
    mediaStreamRawFailed = true;
    mediaStreamRawError = "aborted";
    mediaStreamRawActive = false;
  }
}

void handleMediaStreamPushComplete() {
  if (!mediaStreamActive) {
    resetMediaStreamRawState();
    return server.send(409, "text/plain; charset=UTF-8", "stream-inactive");
  }
  if (mediaStreamRawFailed || !mediaStreamRawComplete) {
    const char *error = mediaStreamRawError ? mediaStreamRawError : "raw-incomplete";
    const bool unauthorized = strcmp(error, "unauthorized") == 0;
    resetMediaStreamRawState();
    return server.send(unauthorized ? 403 : 409, "text/plain; charset=UTF-8", error);
  }
  if (!commitMediaStreamRawWrite()) {
    resetMediaStreamRawState();
    return server.send(409, "text/plain; charset=UTF-8", "commit-failed");
  }
  portalStartedMs = millis();
  sendMediaStreamAck();
}

void handleMediaStreamFinish() {
  if (!authorizePortalRequest()) return;
  uint32_t session = 0;
  if (!parseUintArg("session", session) || !finishMediaStreamSource(session)) {
    return sendJson(409, "{\"error\":\"stale-stream-session\"}");
  }
  portalStartedMs = millis();
  sendJson(202, "{\"ok\":true,\"state\":\"draining\"}");
}

void handleMediaStreamStop() {
  if (!authorizePortalRequest()) return;
  uint32_t session = 0;
  if (mediaStreamActive && (!parseUintArg("session", session) || session != mediaStreamSessionId)) {
    return sendJson(409, "{\"error\":\"stale-stream-session\"}");
  }
  if (mediaStreamActive) mediaStreamLastStopReason = "user-stop";
  stopMediaStream();
  wakeDisplay();
  sendJson(200, mediaStreamStatusJson());
}

bool prepareMediaUploadRequest() {
  if (!requestFromSetupAp() || !validToken()) {
    mediaUploadError = "업로드 세션이 유효하지 않습니다.";
    return false;
  }
  if (!mediaStorageReady) {
    mediaUploadError = "미디어 저장소를 사용할 수 없습니다.";
    return false;
  }
  if (firmwareUpdateBusy() || mediaUploadActive) {
    mediaUploadError = "다른 저장 작업이 진행 중입니다.";
    return false;
  }
  if (thermalWarning || temperatureSensorFault || thermalSafeMode) {
    mediaUploadError = "온도 보호 상태에서는 미디어를 저장할 수 없습니다.";
    return false;
  }
  uint32_t expected = 0;
  uint32_t displaySeconds = 0;
  if (!parseUintArg("size", expected) || !parseUintArg("display", displaySeconds) ||
      expected < MilestoneMedia::HEADER_BYTES || expected > MilestoneMedia::MAX_FILE_BYTES ||
      displaySeconds < 3 || displaySeconds > 60) {
    mediaUploadError = "업로드 크기 또는 표시시간이 잘못되었습니다.";
    return false;
  }
  const String name = server.arg("name");
  if (name.length() == 0 || name.length() >= MEDIA_NAME_BYTES || utf8Codepoints(name) > 12) {
    mediaUploadError = "미디어 이름은 UTF-8 48바이트, 12자 이하여야 합니다.";
    return false;
  }
  if (mediaCatalog.count >= MEDIA_MAX_ITEMS ||
      mediaCatalogBytesUsed(mediaCatalog) + expected > mediaStorageLimit()) {
    mediaUploadError = "미디어 저장 한도 또는 항목 수를 초과합니다.";
    return false;
  }
  const size_t physicalFree = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (physicalFree < expected + 4096U) {
    mediaUploadError = "안전한 임시 업로드 공간이 부족합니다.";
    return false;
  }
  LittleFS.remove(MEDIA_UPLOAD_TEMP);
  mediaUploadFile = LittleFS.open(MEDIA_UPLOAD_TEMP, "w");
  if (!mediaUploadFile) {
    mediaUploadError = "임시 업로드 파일을 만들지 못했습니다.";
    return false;
  }
  mediaUploadExpectedSize = expected;
  mediaUploadDisplaySeconds = static_cast<uint8_t>(displaySeconds);
  mediaUploadName = name;
  mediaUploadWritten = 0;
  mediaUploadFailed = false;
  mediaUploadComplete = false;
  mediaUploadActive = true;
  return true;
}

void handleMediaUploadData() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    resetMediaUploadState(true);
    if (!prepareMediaUploadRequest()) mediaUploadFailed = true;
    return;
  }
  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!mediaUploadActive || mediaUploadFailed) return;
    if (mediaUploadWritten + upload.currentSize > mediaUploadExpectedSize ||
        mediaUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      mediaUploadFailed = true;
      mediaUploadError = "미디어 파일 저장 중 오류가 발생했습니다.";
      return;
    }
    mediaUploadWritten += upload.currentSize;
    return;
  }
  if (upload.status == UPLOAD_FILE_END) {
    if (mediaUploadFile) {
      mediaUploadFile.flush();
      mediaUploadFile.close();
    }
    if (!mediaUploadActive || mediaUploadFailed || mediaUploadWritten != mediaUploadExpectedSize ||
        upload.totalSize != mediaUploadExpectedSize) {
      mediaUploadFailed = true;
      if (mediaUploadError.length() == 0) mediaUploadError = "업로드 길이가 예상 크기와 다릅니다.";
    } else {
      mediaUploadComplete = true;
    }
    mediaUploadActive = false;
    return;
  }
  if (upload.status == UPLOAD_FILE_ABORTED) {
    mediaUploadFailed = true;
    mediaUploadError = "미디어 업로드가 중단되었습니다.";
    if (mediaUploadFile) mediaUploadFile.close();
    mediaUploadActive = false;
  }
}

void handleMediaUploadComplete() {
  if (!authorizePortalRequest()) {
    resetMediaUploadState(true);
    return;
  }
  if (!mediaUploadComplete || mediaUploadFailed) {
    const String error = mediaUploadError.length() ? mediaUploadError : "미디어 업로드를 완료하지 못했습니다.";
    resetMediaUploadState(true);
    return sendJson(400, String("{\"error\":\"") + jsonEscape(error) + "\"}");
  }
  if (thermalWarning || temperatureSensorFault || thermalSafeMode || firmwareUpdateBusy()) {
    resetMediaUploadState(true);
    return sendJson(409, "{\"error\":\"안전 상태가 바뀌어 업로드를 확정하지 않았습니다.\"}");
  }
  MilestoneMedia::Header header;
  String error;
  const String name = mediaUploadName;
  const uint8_t displaySeconds = mediaUploadDisplaySeconds;
  if (!addUploadedMedia(name, displaySeconds, header, error)) {
    resetMediaUploadState(true);
    return sendJson(400, String("{\"error\":\"") + jsonEscape(error) + "\"}");
  }
  resetMediaUploadState(false);
  refreshMediaViewAfterCatalogChange();
  sendJson(201, String("{\"ok\":true,\"frames\":") + String(header.frameCount) +
                    ",\"duration_ms\":" + String(header.durationMs) + "}");
}

void handleMediaUpdate() {
  if (!authorizeMediaMutation()) return;
  uint32_t id = 0;
  uint32_t displaySeconds = 0;
  bool enabled = false;
  const String name = server.arg("name");
  if (!parseUintArg("id", id) || !parseUintArg("display", displaySeconds) ||
      !parseBoolArg("enabled", enabled) || name.length() == 0 ||
      name.length() >= MEDIA_NAME_BYTES || utf8Codepoints(name) > 12 ||
      displaySeconds < 3 || displaySeconds > 60) {
    return sendJson(400, "{\"error\":\"미디어 설정 값이 잘못되었습니다.\"}");
  }
  if (!updateMediaEntry(id, name, static_cast<uint8_t>(displaySeconds), enabled)) {
    return sendJson(500, "{\"error\":\"미디어 설정을 저장하지 못했습니다.\"}");
  }
  refreshMediaViewAfterCatalogChange();
  sendJson(200, "{\"ok\":true}");
}

void handleMediaOrder() {
  if (!authorizeMediaMutation()) return;
  uint32_t id = 0;
  const String direction = server.arg("direction");
  if (!parseUintArg("id", id) || (direction != "up" && direction != "down")) {
    return sendJson(400, "{\"error\":\"미디어 순서 요청이 잘못되었습니다.\"}");
  }
  if (!moveMediaEntry(id, direction == "up" ? -1 : 1)) {
    return sendJson(500, "{\"error\":\"미디어 순서를 저장하지 못했습니다.\"}");
  }
  refreshMediaViewAfterCatalogChange();
  sendJson(200, "{\"ok\":true}");
}

void handleMediaDelete() {
  if (!authorizeMediaMutation()) return;
  uint32_t id = 0;
  if (!parseUintArg("id", id)) return sendJson(400, "{\"error\":\"미디어 ID가 잘못되었습니다.\",\"stage\":\"parse-id\"}");
  String detail;
  if (!deleteMediaEntry(id, detail)) {
    return sendJson(500, String("{\"error\":\"미디어를 삭제하지 못했습니다.\",\"id\":") +
                         String(id) + ",\"stage\":\"" + jsonEscape(detail) + "\"}");
  }
  refreshMediaViewAfterCatalogChange();
  sendJson(200, String("{\"ok\":true,\"id\":") + String(id) +
                    ",\"stage\":\"" + jsonEscape(detail) + "\"}");
}

void handleMediaClear() {
  if (!authorizeMediaMutation()) return;
  if (server.arg("confirm") != "MEDIA") return sendJson(400, "{\"error\":\"전체 삭제 확인 문자열이 올바르지 않습니다.\"}");
  if (!clearAllMediaFiles()) return sendJson(500, "{\"error\":\"일부 미디어 파일을 삭제하지 못했습니다.\"}");
  refreshMediaViewAfterCatalogChange();
  sendJson(200, "{\"ok\":true}");
}

void handleMediaRepair() {
  if (!authorizeMediaMutation()) return;
  if (server.arg("confirm") != "REPAIR") {
    return sendJson(400, "{\"error\":\"저장소 복구 확인 문자열이 올바르지 않습니다.\"}");
  }
  if (!repairMediaStorage()) {
    recordDiagnostic(MilestoneDiagnostics::Event::MEDIA_FAILED,
                     static_cast<int16_t>(MilestoneDiagnostics::MediaFailure::STORAGE), 0, false);
    return sendJson(500, "{\"error\":\"미디어 저장소를 복구하지 못했습니다.\"}");
  }
  resetMediaUploadState(false);
  refreshMediaViewAfterCatalogChange();
  sendJson(200, "{\"ok\":true}");
}

void handleGetRadioConfig() {
  if (!requireSetupApRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  String json = "{";
  json += "\"fixed_ap\":" + String(config.fixedApSecurity ? "true" : "false") + ",";
  json += "\"ap_password_set\":" + String(config.fixedApPassword.length() > 0 ? "true" : "false") + ",";
  json += "\"bluetooth_now_playing\":" + String(config.bluetoothNowPlaying ? "true" : "false") + ",";
#if MILESTONE_HAS_BLUETOOTH && defined(CONFIG_NIMBLE_ENABLED)
  json += "\"bluetooth_supported\":true";
#else
  json += "\"bluetooth_supported\":false";
#endif
  json += "}";
  sendJson(200, json);
}

void handlePostRadioConfig() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;

  const bool hasApUpdate = server.hasArg("fixed_ap") || server.hasArg("ap_password");
  const bool hasBluetoothUpdate = server.hasArg("bluetooth_now_playing");
  if (!hasApUpdate && !hasBluetoothUpdate) {
    return sendJson(400, "{\"error\":\"변경할 AP 또는 Bluetooth 설정 값이 없습니다.\"}");
  }

  bool fixedAp = config.fixedApSecurity;
  String password = config.fixedApPassword;
  if (hasApUpdate) {
    if (!server.hasArg("fixed_ap") || !server.hasArg("ap_password") || !parseBoolArg("fixed_ap", fixedAp)) {
      return sendJson(400, "{\"error\":\"AP 보안 설정 값이 누락되었습니다.\"}");
    }
    password = server.arg("ap_password");
    if (fixedAp && password.length() > 0 && (password.length() < 8 || password.length() > 63)) {
      return sendJson(400, "{\"error\":\"고정 AP 비밀번호는 비워 두거나 8~63자로 입력하세요.\"}");
    }
  }

  bool bluetoothEnabled = config.bluetoothNowPlaying;
  if (hasBluetoothUpdate && !parseBoolArg("bluetooth_now_playing", bluetoothEnabled)) {
    return sendJson(400, "{\"error\":\"Bluetooth 설정 값이 올바르지 않습니다.\"}");
  }
  if (hasBluetoothUpdate && bluetoothEnabled && !MILESTONE_HAS_BLUETOOTH) {
    return sendJson(409, "{\"error\":\"CORE 펌웨어로 전환한 뒤 Bluetooth를 켤 수 있습니다.\"}");
  }
#if !defined(CONFIG_NIMBLE_ENABLED)
  if (hasBluetoothUpdate && bluetoothEnabled) {
    return sendJson(501, "{\"error\":\"현재 BLE 빌드에서는 iPhone Now Playing을 지원하지 않습니다. NimBLE 빌드가 필요합니다.\"}");
  }
#endif

  const Config previous = config;
  if (hasApUpdate) {
    config.fixedApSecurity = fixedAp;
    // A blank password while fixed mode is enabled explicitly means an open AP.
    // Turning fixed mode off also removes the dormant saved secret.
    config.fixedApPassword = fixedAp ? password : "";
  }
  if (hasBluetoothUpdate) config.bluetoothNowPlaying = bluetoothEnabled;

  if (!saveGeneralConfig()) {
    config = previous;
    if (!saveGeneralConfig()) logLine("radio/AP configuration rollback could not be persisted");
    return sendJson(500, "{\"error\":\"AP/블루투스 설정을 저장하지 못했습니다.\"}");
  }

  const bool bluetoothChanged = previous.bluetoothNowPlaying != config.bluetoothNowPlaying;
  bool bluetoothActive = bluetoothNowPlayingRuntimeActive();
  if (bluetoothChanged) {
    if (config.bluetoothNowPlaying) bluetoothActive = initializeBluetoothNowPlaying();
    else {
      shutdownBluetoothNowPlaying();
      bluetoothActive = false;
    }
  }
  String result = "{\"ok\":true,\"ap_updated\":";
  result += hasApUpdate ? "true" : "false";
  result += ",\"ap_applies_next_start\":";
  result += hasApUpdate ? "true" : "false";
  result += ",\"bluetooth_active\":";
  result += bluetoothActive ? "true}" : "false}";
  sendJson(200, result);
}

void handleGetConfig() {
  if (!requireSetupApRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
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
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  if (wifiTestState == WifiTestState::CONNECTING || wifiTestState == WifiTestState::TIME_SYNCING) {
    sendJson(409, "{\"error\":\"Wi-Fi 연결 시험이 진행 중입니다. 완료 후 검색하세요.\"}");
    return;
  }
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
      sendJson(504, "{\"error\":\"Wi-Fi 검색 시간이 초과되었습니다. 설정 AP는 유지됩니다. 다시 시도하세요.\"}");
      return;
    }
    sendJson(202, "{\"state\":\"scanning\"}");
    return;
  }
  if (count == WIFI_SCAN_FAILED) {
    stopWifiScanBestEffort();
    int started = WiFi.scanNetworks(true, false, false, PORTAL_WIFI_SCAN_DWELL_MS, 0);
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

bool parseUintText(const String &text, uint32_t &value) {
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

bool parseUintArg(const char *name, uint32_t &value) {
  if (!server.hasArg(name)) return false;
  return parseUintText(server.arg(name), value);
}

bool parseUintHeader(const char *name, uint32_t &value) {
  if (!server.hasHeader(name)) return false;
  return parseUintText(server.header(name), value);
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
  if (rejectPortalWorkDuringStream()) return;
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
  if (mode > 8) return sendJson(400, "{\"error\":\"표시 모드가 잘못되었습니다.\"}");
  if (cycleMask < 1 || cycleMask > 255) return sendJson(400, "{\"error\":\"순환 화면을 하나 이상 선택하세요.\"}");
  if (!(cycleInterval == 0 || (cycleInterval >= 3 && cycleInterval <= 60))) return sendJson(400, "{\"error\":\"자동 전환은 0초 또는 3~60초여야 합니다.\"}");
  if (scrollSpeed < 5 || scrollSpeed > 80 || brightness < 1 || brightness > 255 ||
      nightLevel < 1 || nightLevel > 255 || ledBrightness < 1 || ledBrightness > 64 ||
      ledNightLevel < 1 || ledNightLevel > 32 || nightStart > 1439 || nightEnd > 1439 ||
      screenOff > 1440) {
    return sendJson(400, "{\"error\":\"밝기·시간·스크롤 설정 범위를 확인하세요.\"}");
  }
  uint8_t parsedOrder[VIEW_COUNT];
  if (!parseCycleOrderCount(server.arg("cycle_order"), parsedOrder, VIEW_COUNT)) return sendJson(400, "{\"error\":\"순환 순서는 0~7을 중복 없이 입력하세요.\"}");
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
  if (currentView == View::CUSTOM_MEDIA) {
    if (mediaHasEnabledItems()) selectNextMediaItem();
    else currentView = View::DASHBOARD;
  } else {
    closeMediaPlayback();
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
  if (rejectPortalWorkDuringStream()) return;
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
  const uint32_t testTimeoutMs = pendingWifiSecurity == WifiSecurityType::ENTERPRISE_PEAP
                                   ? WIFI_CONNECT_TIMEOUT_MS : WIFI_PORTAL_TEST_CONNECT_TIMEOUT_MS;
  beginStationConnection(pendingSsid, pendingPass, pendingWifiSecurity,
                         pendingWifiUsername, pendingWifiIdentity, true, testTimeoutMs);
  sendJson(202, "{\"ok\":true,\"state\":\"connecting\"}");
}

void handleTimeSync() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
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
  if (rejectPortalWorkDuringStream()) return;
  if (!prefsReady) return sendJson(503, "{\"error\":\"NVS 저장공간을 사용할 수 없어 공장 초기화를 실행할 수 없습니다.\"}");
  if (server.arg("confirm") != "RESET") return sendJson(400, "{\"error\":\"초기화 확인 문자열이 올바르지 않습니다.\"}");
  if (!clearAllMediaFiles()) return sendJson(500, "{\"error\":\"커스텀 미디어를 초기화하지 못했습니다.\"}");
  if (!prefs.clear()) return sendJson(500, "{\"error\":\"설정 저장공간을 초기화하지 못했습니다.\"}");
  if (!clearDiagnosticHistory()) logLine("diagnostic history clear failed during factory reset");
  sendJson(200, "{\"ok\":true,\"restarting\":true}");
  restartAfterFactoryReset();
}

void handleSettingsReset() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
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

  if (previous.bluetoothNowPlaying && !config.bluetoothNowPlaying) shutdownBluetoothNowPlaying();
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
  logLine("display, time, LED, AP security, and Bluetooth settings restored to defaults; saved Wi-Fi retained");
  sendJson(200, "{\"ok\":true,\"wifi_retained\":true}");
}

void handleDeleteSavedWifi() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
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
  if (rejectPortalWorkDuringStream()) return;
  if (mediaTransferBusy()) {
    return sendJson(409, "{\"error\":\"미디어 업로드가 진행 중입니다. 완료 후 다시 시도하세요.\"}");
  }
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
  requestFirmwareUpdateCheck(UpdateCheckReason::MANUAL, FIRMWARE_PROFILE);
  sendJson(202, "{\"ok\":true,\"state\":\"queued\"}");
}

void handleProfileSwitch() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  const String profile = server.arg("profile");
  if (!validFirmwareProfile(profile)) {
    return sendJson(400, "{\"error\":\"지원하지 않는 펌웨어 프로필입니다.\"}");
  }
  if (profile == FIRMWARE_PROFILE) {
    return sendJson(409, "{\"error\":\"이미 선택한 프로필을 실행 중입니다.\"}");
  }
  if (mediaTransferBusy() || firmwareUpdateBusy() || updateInstallRequested) {
    return sendJson(409, "{\"error\":\"저장 또는 펌웨어 작업이 끝난 뒤 전환하세요.\"}");
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    return sendJson(409, "{\"error\":\"온도 보호 상태에서는 프로필을 전환할 수 없습니다.\"}");
  }
  if (!stationNetworkReady()) {
    return sendJson(409, "{\"error\":\"인터넷에 연결된 Wi-Fi가 필요합니다.\"}");
  }
  if (!timeIsValid() || !freshNtpSinceBoot) {
    return sendJson(409, "{\"error\":\"먼저 시간 동기화를 완료한 뒤 다시 시도하세요.\"}");
  }
  if (updateState == UpdateState::CURRENT || updateState == UpdateState::ERROR_STATE) {
    setUpdateState(UpdateState::IDLE);
  }
  updateCheckNotBeforeMs = millis() + UPDATE_NETWORK_SETTLE_MS;
  requestFirmwareUpdateCheck(UpdateCheckReason::MANUAL, profile);
  sendJson(202, String("{\"ok\":true,\"state\":\"queued\",\"profile\":\"") + profile + "\"}");
}

void handleUpdateInstall() {
  if (!authorizePortalRequest()) return;
  if (rejectPortalWorkDuringStream()) return;
  if (mediaTransferBusy()) {
    return sendJson(409, "{\"error\":\"미디어 업로드가 진행 중입니다. 완료 후 다시 시도하세요.\"}");
  }
  if (updateInstallRequested || firmwareTransferActive()) {
    return sendJson(409, "{\"error\":\"업데이트 설치가 이미 진행 중입니다.\"}");
  }
  if (updateState != UpdateState::AVAILABLE || latestFirmwareVersion.length() == 0 ||
      !validFirmwareProfile(latestFirmwareProfile) || latestFirmwareAsset.length() == 0 ||
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
  if (mediaStreamActive) {
    // Do not send the large general portal while video timing owns the device.
    // A direct root visit moves to the lightweight stream page; captive-probe
    // endpoints get an empty response instead of downloading either page.
    if (server.uri() == "/") {
      server.sendHeader("Location", "/stream", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(204, "text/plain", "");
    }
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Set-Cookie", String("MILESTONE_TOKEN=") + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
  server.send_P(200, "text/html; charset=UTF-8", MILESTONE_PORTAL_HTML);
}

void handleStreamPortal() {
  if (!requireSetupApRequest(false)) return;
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Set-Cookie", String("MILESTONE_TOKEN=") + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
  server.send_P(200, "text/html; charset=UTF-8", MILESTONE_STREAM_HTML);
}

void registerPortalRoutes() {
  static bool registered = false;
  if (registered) return;
  registered = true;
  static const char *headerKeys[] = {
      "Cookie", "X-MILESTONE-Session", "X-MILESTONE-Seq", "X-MILESTONE-Count",
      "X-MILESTONE-Bytes"};
  server.collectHeaders(headerKeys, 5);
  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/stream", HTTP_GET, handleStreamPortal);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/diagnostics", HTTP_GET, handleDiagnostics);
  server.on("/api/diagnostics/clear", HTTP_POST, handleDiagnosticsClear);
  server.on("/api/media/status", HTTP_GET, handleMediaStatus);
  server.on("/api/media/list", HTTP_GET, handleMediaList);
  server.on("/api/media/upload", HTTP_POST, handleMediaUploadComplete, handleMediaUploadData);
  server.on("/api/media/update", HTTP_POST, handleMediaUpdate);
  server.on("/api/media/order", HTTP_POST, handleMediaOrder);
  server.on("/api/media/delete", HTTP_POST, handleMediaDelete);
  server.on("/api/media/clear", HTTP_POST, handleMediaClear);
  server.on("/api/media/repair", HTTP_POST, handleMediaRepair);
  server.on("/api/stream/status", HTTP_GET, handleMediaStreamStatus);
  server.on("/api/stream/start", HTTP_POST, handleMediaStreamStart);
  server.on("/api/stream/push", HTTP_POST, handleMediaStreamPushComplete, handleMediaStreamPushRaw);
  server.on("/api/stream/finish", HTTP_POST, handleMediaStreamFinish);
  server.on("/api/stream/stop", HTTP_POST, handleMediaStreamStop);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/radio-config", HTTP_GET, handleGetRadioConfig);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/radio-config", HTTP_POST, handlePostRadioConfig);
  server.on("/api/wifi/test", HTTP_POST, handleWifiTest);
  server.on("/api/wifi/delete", HTTP_POST, handleDeleteSavedWifi);
  server.on("/api/time/sync", HTTP_POST, handleTimeSync);
  server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/api/profile/switch", HTTP_POST, handleProfileSwitch);
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

void stabilizePortalAfterFailedWifiTest() {
  // Do not immediately start another saved-network connection under the setup
  // AP. When the requested AP is absent this used to chain one channel search
  // into another and made the captive portal appear to disappear. Keep the STA
  // idle until the user retries explicitly or exits setup; stopPortal() will
  // resume the normal saved-network sequence.
  WiFi.disconnect(false, false);
  delay(30);
  stopWifiScanBestEffort();
  resetEnterpriseStationAuth();
  wifiNetworkReadySinceMs = 0;
  wifiDeadlineMs = 0;
  activeWifiIndex = NO_WIFI_INDEX;
  logLine("Wi-Fi test failed; STA paused to keep setup AP stable");
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
  stabilizePortalAfterFailedWifiTest();
  recordDiagnostic(MilestoneDiagnostics::Event::WIFI_TEST_FAILED);
  logLine(String("Wi-Fi test failed: ") + reason);
}
