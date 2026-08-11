constexpr uint32_t DIAGNOSTIC_DUPLICATE_WINDOW_MS = 60UL * 1000UL;

bool persistDiagnosticHistory() {
  if (!diagnosticsPrefsReady) return false;
  const size_t written = diagnosticsPrefs.putBytes("history", &diagnosticHistory, sizeof(diagnosticHistory));
  return written == sizeof(diagnosticHistory);
}

void resetDiagnosticRuntimeSuppression() {
  lastDiagnosticEvent = MilestoneDiagnostics::Event::NONE;
  lastDiagnosticDetail = 0;
  lastDiagnosticWriteMs = 0;
  lastDiagnosticWriteValid = false;
}

void initializeDiagnosticStorage() {
  MilestoneDiagnostics::reset(diagnosticHistory);
  resetDiagnosticRuntimeSuppression();
  if (!diagnosticsPrefsReady) return;

  if (diagnosticsPrefs.getBytesLength("history") != sizeof(diagnosticHistory) ||
      diagnosticsPrefs.getBytes("history", &diagnosticHistory, sizeof(diagnosticHistory)) != sizeof(diagnosticHistory) ||
      !MilestoneDiagnostics::valid(diagnosticHistory)) {
    MilestoneDiagnostics::reset(diagnosticHistory);
    logLine("diagnostic history initialized/reset");
  }
}

void recordDiagnostic(MilestoneDiagnostics::Event event, int16_t detail = 0,
                      int32_t value = 0, bool force = false) {
  if (event == MilestoneDiagnostics::Event::NONE) return;
  const uint32_t nowMs = millis();
  if (!force && MilestoneDiagnostics::shouldSuppressDuplicate(
          event, detail, nowMs, lastDiagnosticEvent, lastDiagnosticDetail,
          lastDiagnosticWriteMs, lastDiagnosticWriteValid, DIAGNOSTIC_DUPLICATE_WINDOW_MS)) {
    return;
  }

  MilestoneDiagnostics::Record record;
  const time_t nowEpoch = time(nullptr);
  record.epoch = nowEpoch >= MIN_VALID_EPOCH ? static_cast<uint32_t>(nowEpoch) : 0;
  const uint64_t up = uptimeSeconds();
  record.uptimeSec = up > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(up);
  record.event = event;
  record.detail = detail;
  record.value = value;
  MilestoneDiagnostics::append(diagnosticHistory, record);

  if (diagnosticsPrefsReady && !persistDiagnosticHistory()) {
    logLine(String("diagnostic history save failed: ") + MilestoneDiagnostics::eventName(event));
    return;
  }

  lastDiagnosticEvent = event;
  lastDiagnosticDetail = detail;
  lastDiagnosticWriteMs = nowMs;
  lastDiagnosticWriteValid = true;
}

bool clearDiagnosticHistory() {
  if (diagnosticsPrefsReady && !diagnosticsPrefs.clear()) return false;
  MilestoneDiagnostics::reset(diagnosticHistory);
  resetDiagnosticRuntimeSuppression();
  return true;
}

bool latestDiagnosticError(MilestoneDiagnostics::Record &record) {
  for (uint8_t i = 0; i < diagnosticHistory.count; ++i) {
    MilestoneDiagnostics::Record candidate;
    if (!MilestoneDiagnostics::newest(diagnosticHistory, i, candidate)) break;
    if (MilestoneDiagnostics::isError(candidate.event)) {
      record = candidate;
      return true;
    }
  }
  return false;
}

String diagnosticDetailText(const MilestoneDiagnostics::Record &record) {
  using MilestoneDiagnostics::Event;
  switch (record.event) {
    case Event::BOOT:
      return resetReasonName(static_cast<esp_reset_reason_t>(record.detail));
    case Event::WIFI_CONNECTED:
      return String(record.value) + " dBm / " + String(record.detail) + " ms";
    case Event::WIFI_CONNECT_TIMEOUT:
      if (record.detail > 0) return String("connection timeout / reason ") + String(record.detail);
      return "connection timeout";
    case Event::WIFI_DISCONNECTED:
      if (record.detail > 0) return String("connection lost / reason ") + String(record.detail);
      return "connection lost";
    case Event::WIFI_TEST_FAILED:
      return "setup Wi-Fi test failed";
    case Event::NTP_SUCCESS:
      return String(record.value) + " ms";
    case Event::NTP_TIMEOUT:
      return "time synchronization timeout";
    case Event::UPDATE_CHECK_FAILED:
      return record.detail == static_cast<int16_t>(MilestoneDiagnostics::UpdateCheckFailure::TRANSIENT)
               ? "transient failure" : "fatal failure";
    case Event::OTA_FAILED: {
      switch (static_cast<MilestoneDiagnostics::OtaFailure>(record.detail)) {
        case MilestoneDiagnostics::OtaFailure::METADATA: return "release metadata";
        case MilestoneDiagnostics::OtaFailure::TEMPERATURE: return "temperature protection";
        case MilestoneDiagnostics::OtaFailure::NETWORK: return "network";
        case MilestoneDiagnostics::OtaFailure::MEMORY: return "memory";
        case MilestoneDiagnostics::OtaFailure::PARTITION: return "OTA partition";
        case MilestoneDiagnostics::OtaFailure::STATE: return "OTA state persistence";
        case MilestoneDiagnostics::OtaFailure::HTTP: return "HTTP/TLS";
        case MilestoneDiagnostics::OtaFailure::DOWNLOAD: return "download interrupted";
        case MilestoneDiagnostics::OtaFailure::WRITE: return "flash write";
        case MilestoneDiagnostics::OtaFailure::SHA256: return "SHA-256 mismatch";
        case MilestoneDiagnostics::OtaFailure::FINALIZE: return "OTA finalize";
        case MilestoneDiagnostics::OtaFailure::ROLLBACK: return "rollback protection";
        default: return "unknown OTA failure";
      }
    }
    case Event::ROLLBACK_TRIGGERED:
      if (record.detail == static_cast<int16_t>(MilestoneDiagnostics::RollbackAction::BOOTLOADER)) return "bootloader rollback";
      if (record.detail == static_cast<int16_t>(MilestoneDiagnostics::RollbackAction::FAILED)) return "rollback failed";
      return "application rollback";
    case Event::THERMAL_WARNING:
    case Event::THERMAL_THROTTLE:
    case Event::THERMAL_PROTECTION:
    case Event::THERMAL_RECOVERED:
      return String(record.value / 10.0f, 1) + " C";
    case Event::TEMPERATURE_SENSOR_FAULT:
      return "chip temperature sensor fault";
    default:
      return "";
  }
}

void noteWifiConnected(uint32_t now) {
  uint32_t duration = wifiConnectionStartedMs == 0 ? 0 : static_cast<uint32_t>(now - wifiConnectionStartedMs);
  if (duration > MilestoneDiagnostics::DETAIL_MAX) duration = MilestoneDiagnostics::DETAIL_MAX;
  recordDiagnostic(MilestoneDiagnostics::Event::WIFI_CONNECTED,
                   static_cast<int16_t>(duration), WiFi.RSSI());
  wifiConnectionStartedMs = 0;
  lastWifiDisconnectReason = 0;
}

void processDiagnostics() {
  if (!diagnosticBootValidationPending ||
      !elapsed(millis(), diagnosticBootValidationStartedMs, OTA_BOOT_CONFIRM_MS)) return;
  if (!oledReady || !prefsReady) return;
  diagnosticBootValidationPending = false;
  diagnosticBootValidated = true;
  recordDiagnostic(MilestoneDiagnostics::Event::BOOT_VALIDATED, 0, 0, true);
}
