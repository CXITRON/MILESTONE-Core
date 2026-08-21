void enterWifiSleep() {
  if (!config.wifiSleep || portalActive) return;
  stopMdns();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  setRuntimeState(RuntimeState::WIFI_SLEEP);
}

bool firmwareUpdateCheckDue() {
  if (!timeIsValid()) return false;
  const uint64_t now = static_cast<uint64_t>(time(nullptr));
  return lastUpdateCheckEpoch == 0 || now >= lastUpdateCheckEpoch + UPDATE_WEEKLY_SEC;
}

void finishNormalNtpSuccess() {
  markNtpSuccess();
  if (!promoteSavedNetwork(activeWifiIndex)) logLine("preferred Wi-Fi order could not be persisted");
  setRuntimeState(RuntimeState::RUNNING_ONLINE);
  startMdns();
  if (bootUpdateCheckPending) {
    updateCheckAfterNetworkReady = false;
    wifiSleepDeferredForUpdate = config.wifiSleep;
    updateCheckNotBeforeMs = millis() + UPDATE_NETWORK_SETTLE_MS;
    requestFirmwareUpdateCheck(UpdateCheckReason::BOOT);
  } else if (updateCheckAfterNetworkReady || firmwareUpdateCheckDue()) {
    updateCheckAfterNetworkReady = false;
    wifiSleepDeferredForUpdate = config.wifiSleep;
    updateCheckNotBeforeMs = millis() + UPDATE_NETWORK_SETTLE_MS;
    requestFirmwareUpdateCheck(UpdateCheckReason::WEEKLY);
  } else if (config.wifiSleep) {
    enterWifiSleep();
  } else {
    // Always-connected mode prioritizes network reliability. Do not silently
    // enable modem power-save after NTP; setup/AP tests also run with the radio
    // fully awake, so keeping STA awake removes that behavioral mismatch.
    WiFi.setSleep(false);
  }
}

bool stationNetworkReady() {
  if (WiFi.status() != WL_CONNECTED) return false;
  const IPAddress zero(0, 0, 0, 0);
  return WiFi.localIP() != zero && WiFi.gatewayIP() != zero &&
         (WiFi.dnsIP(0) != zero || WiFi.dnsIP(1) != zero);
}

bool stationNetworkStable(uint32_t now) {
  if (!stationNetworkReady()) {
    wifiNetworkReadySinceMs = 0;
    return false;
  }
  if (wifiNetworkReadySinceMs == 0) {
    wifiNetworkReadySinceMs = now;
    logLine(String("Wi-Fi obtained IP ") + WiFi.localIP().toString() +
            " gateway=" + WiFi.gatewayIP().toString() +
            " dns0=" + WiFi.dnsIP(0).toString() +
            " dns1=" + WiFi.dnsIP(1).toString() +
            " RSSI=" + String(WiFi.RSSI()) + " dBm; stabilizing");
    return false;
  }
  return elapsed(now, wifiNetworkReadySinceMs, WIFI_IP_STABLE_MS);
}

void processNetwork() {
  if (thermalSafeMode) return;
  const uint32_t now = millis();

  if (portalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
    // A live stream owns the setup AP session. While streaming, cancel any
    // previously armed post-Wi-Fi-success close and suppress every normal AP
    // timeout path. The browser that is feeding frames must not lose the AP.
    const bool streamPinsPortal = mediaStreamActive || mediaStreamRawActive;
    if (streamPinsPortal) {
      portalStartedMs = now;
      portalClosingAfterSuccess = false;
    }
    if (!streamPinsPortal && portalClosingAfterSuccess &&
        elapsed(now, portalSuccessMs, PORTAL_SUCCESS_HOLD_MS)) {
      stopPortal();
      return;
    }
    if (!streamPinsPortal && !portalClosingAfterSuccess &&
        wifiTestState != WifiTestState::CONNECTING &&
        wifiTestState != WifiTestState::TIME_SYNCING &&
        elapsed(now, portalStartedMs, AP_TIMEOUT_MS)) {
      stopPortal();
      return;
    }
  }

  if (wifiTestState == WifiTestState::CONNECTING) {
    if (stationNetworkStable(now)) {
      noteWifiConnected(now);
      wifiNetworkReadySinceMs = 0;
      wifiTestState = WifiTestState::TIME_SYNCING;
      beginNtpRequest();
      setRuntimeState(RuntimeState::TIME_SYNCING);
    } else if (deadlineReached(now, wifiDeadlineMs)) {
      recordDiagnostic(MilestoneDiagnostics::Event::WIFI_CONNECT_TIMEOUT,
                       static_cast<int16_t>(lastWifiDisconnectReason));
      failWifiTest("Wi-Fi 연결 시간 초과. SSID와 Wi-Fi 자격 증명을 확인하세요.");
    }
    return;
  }

  if (wifiTestState == WifiTestState::TIME_SYNCING) {
    if (!stationNetworkReady()) {
      ntpRequestStartedMs = 0;
      recordDiagnostic(MilestoneDiagnostics::Event::WIFI_DISCONNECTED,
                       static_cast<int16_t>(lastWifiDisconnectReason));
      failWifiTest("시간 확인 중 Wi-Fi 연결이 끊겼습니다.");
    } else if (ntpSyncEvent && timeIsValid()) {
      ntpSyncEvent = false;
      finishWifiTestSuccess();
    } else if (deadlineReached(now, ntpDeadlineMs)) {
      ntpRequestStartedMs = 0;
      recordDiagnostic(MilestoneDiagnostics::Event::NTP_TIMEOUT);
      failWifiTest("인터넷 시간 확인에 실패했습니다. 인터넷 연결을 확인하세요.");
    }
    return;
  }

  if (runtimeState == RuntimeState::CONNECTING) {
    if (stationNetworkStable(now)) {
      noteWifiConnected(now);
      wifiNetworkReadySinceMs = 0;
      if (savedWifiScanActive) {
        WiFi.scanDelete();
        savedWifiScanActive = false;
      }
      if (initialStationAttempt && !config.bootSync && timeIsValid()) {
        initialStationAttempt = false;
        if (!promoteSavedNetwork(activeWifiIndex)) logLine("preferred Wi-Fi order could not be persisted");
        setRuntimeState(RuntimeState::RUNNING_OFFLINE);
        startMdns();
        scheduleRetry();
        logLine("boot-time NTP skipped; update check waits for the next confirmed NTP sync");
      } else {
        initialStationAttempt = false;
        beginNtpRequest();
        setRuntimeState(RuntimeState::TIME_SYNCING);
      }
    } else if (savedWifiScanActive) {
      if (!finishSavedWifiScanAndConnect()) {
        initialStationAttempt = false;
        setRuntimeState(RuntimeState::RUNNING_OFFLINE);
        scheduleRetry();
        parkDisconnectedWifiUntilRetry();
        recordDiagnostic(MilestoneDiagnostics::Event::WIFI_CONNECT_TIMEOUT);
        logLine("no reachable saved Wi-Fi network found; offline mode with normal retry backoff");
      }
    } else if (deadlineReached(now, wifiDeadlineMs)) {
      if (tryNextSavedWifiCandidate()) return;
      initialStationAttempt = false;
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      scheduleRetry();
      parkDisconnectedWifiUntilRetry();
      recordDiagnostic(MilestoneDiagnostics::Event::WIFI_CONNECT_TIMEOUT,
                       static_cast<int16_t>(lastWifiDisconnectReason));
      logLine("Wi-Fi connection timed out; offline mode with normal retry backoff");
    }
    return;
  }

  if (runtimeState == RuntimeState::TIME_SYNCING) {
    if (!stationNetworkReady()) {
      ntpRequestActive = false;
      ntpRequestStartedMs = 0;
      recordDiagnostic(MilestoneDiagnostics::Event::WIFI_DISCONNECTED,
                       static_cast<int16_t>(lastWifiDisconnectReason));
      internetVerified = false;
      ntpFailed = false;
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      scheduleRetry(true);
    } else if (ntpSyncEvent && timeIsValid()) {
      ntpSyncEvent = false;
      finishNormalNtpSuccess();
    } else if (deadlineReached(now, ntpDeadlineMs)) {
      ntpRequestActive = false;
      ntpRequestStartedMs = 0;
      recordDiagnostic(MilestoneDiagnostics::Event::NTP_TIMEOUT);
      internetVerified = false;
      ntpFailed = true;
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      // Before the first successful NTP exchange of this boot, time/DDAY sync
      // remains the highest-priority network task. Keep the existing STA/DHCP
      // lease and retry NTP quickly instead of waiting the normal configured
      // offline interval. Periodic failures after a successful boot sync still
      // respect the user's normal retry interval.
      const bool quickNtpRetry = !freshNtpSinceBoot;
      const uint32_t retrySeconds = quickNtpRetry ? WIFI_QUICK_RETRY_MS / 1000UL
                                                  : (config.retryPeriodSec < 30 ? 30 : config.retryPeriodSec);
      scheduleRetry(quickNtpRetry);
      logLine(String("NTP timed out; retry in ") + String(retrySeconds) +
              " sec; last known D-day will be displayed");
    }
    return;
  }

  if (runtimeState == RuntimeState::RUNNING_ONLINE && !stationNetworkReady()) {
    recordDiagnostic(MilestoneDiagnostics::Event::WIFI_DISCONNECTED,
                       static_cast<int16_t>(lastWifiDisconnectReason));
    internetVerified = false;
    ntpFailed = false;
    setRuntimeState(RuntimeState::RUNNING_OFFLINE);
    scheduleRetry(true);
  }

  // SNTP itself can complete just after the application-level deadline. Do
  // not discard that valid external round trip while Wi-Fi is still present.
  if (runtimeState == RuntimeState::RUNNING_OFFLINE && stationNetworkReady() &&
      ntpSyncEvent && timeIsValid()) {
    ntpSyncEvent = false;
    finishNormalNtpSuccess();
    return;
  }

  bool periodicNtpDue = (runtimeState == RuntimeState::RUNNING_ONLINE || runtimeState == RuntimeState::WIFI_SLEEP) &&
                         config.ntpPeriodSec > 0 && timeIsValid() && config.lastSync > 0 &&
                         static_cast<uint64_t>(time(nullptr)) >= config.lastSync + config.ntpPeriodSec;
  bool retryDue = runtimeState == RuntimeState::RUNNING_OFFLINE && deadlineReached(now, nextRetryMs);

  if (!portalActive && config.savedNetworkCount > 0) {
    if (periodicNtpDue && stationNetworkReady()) {
      beginNtpRequest();
      setRuntimeState(RuntimeState::TIME_SYNCING);
    } else if (retryDue && stationNetworkReady()) {
      beginNtpRequest();
      setRuntimeState(RuntimeState::TIME_SYNCING);
    } else if (periodicNtpDue || retryDue) {
      startSavedWifiSequence(false);
    }
  }

  const bool updateRetryDue = nextUpdateRetryMs != 0 && deadlineReached(now, nextUpdateRetryMs);
  const bool updateCanPause = updateState == UpdateState::IDLE || updateState == UpdateState::AVAILABLE;
  if (config.wifiSleep && runtimeState == RuntimeState::RUNNING_ONLINE &&
      stationNetworkReady() && !portalActive && !ntpRequestActive &&
      pendingUpdateCheckReason == UpdateCheckReason::NONE && !updateInstallRequested &&
      !updatePromptVisible && updateCanPause && !bootUpdateCheckPending &&
      !updateCheckAfterNetworkReady && !firmwareUpdateCheckDue() && !updateRetryDue) {
    wifiSleepDeferredForUpdate = false;
    enterWifiSleep();
  }
}

void processFirmwareUpdate() {
  const uint32_t now = millis();

  processRollbackConfirmation();

  // LittleFS upload and OTA both perform long flash writes. Never start a new
  // firmware operation while the setup portal is receiving custom media.
  if (mediaTransferBusy()) return;

  if (updateState == UpdateState::CURRENT && updateCurrentVisibleMs != 0 &&
      elapsed(now, updateCurrentVisibleMs, UPDATE_CURRENT_HOLD_MS)) {
    setUpdateState(UpdateState::IDLE);
  }
  if (updateState == UpdateState::ERROR_STATE && elapsed(now, updateStateStartedMs, 10000UL)) {
    setUpdateState(UpdateState::IDLE);
    if (wifiSleepDeferredForUpdate && !portalActive) {
      wifiSleepDeferredForUpdate = false;
      enterWifiSleep();
    }
  }
  if (updateState == UpdateState::AVAILABLE && updatePromptVisible &&
      elapsed(now, updatePromptStartedMs, UPDATE_PROMPT_MS)) {
    updatePromptVisible = false;
    wakeDisplay();
    logLine("firmware update postponed");
    if (wifiSleepDeferredForUpdate && !portalActive) {
      wifiSleepDeferredForUpdate = false;
      enterWifiSleep();
    }
  }
  if (updateInstallRequested && deadlineReached(now, updateInstallNotBeforeMs)) {
    updateInstallRequested = false;
    updateInstallNotBeforeMs = 0;
    installFirmwareUpdate();
    return;
  }
  if (pendingUpdateCheckReason != UpdateCheckReason::NONE) {
    // Always let the three-second splash finish and render the normal screen
    // once before a synchronous HTTPS check can occupy loopTask.
    if (bootSplashActive() || !deadlineReached(now, updateCheckNotBeforeMs)) return;

    // A Wi-Fi/DHCP loss is a link-layer recovery problem, not a consumed
    // GitHub attempt. processNetwork() owns reconnection; wait until the STA
    // again has IP/gateway/DNS before spending one of the HTTPS retry slots.
    // The same principle applies to the secure clock prerequisite.
    if (!stationNetworkReady() || !timeIsValid() || !freshNtpSinceBoot) {
      updateCheckNotBeforeMs = now + 1000UL;
      return;
    }

    const UpdateCheckReason reason = pendingUpdateCheckReason;
    if (updateState != UpdateState::CHECKING) {
      // The HTTPS manifest request is synchronous. Previously CHECKING could be
      // entered and left again before processDisplay() ever sent a frame, so the
      // U indicator was invisible on successful first-attempt checks. Enter the
      // state first, return to loop(), and do not start HTTPS until a display
      // renderer has confirmed that the U frame reached the OLED.
      updateCheckIndicatorRendered = false;
      setUpdateState(UpdateState::CHECKING);
      return;
    }
    // If the OLED itself is unavailable there is nothing to render, so do not
    // make networking depend on a failed display. With a working OLED, however,
    // require one completed UI pass before entering the blocking HTTPS request.
    if (oledReady && !updateCheckIndicatorRendered) return;
    if (updateCheckAttempt < UINT8_MAX) ++updateCheckAttempt;

    String failure;
    const UpdateCheckAttemptResult result = checkFirmwareManifestAttempt(reason, failure);
    if (result == UpdateCheckAttemptResult::SUCCESS) {
      pendingUpdateCheckReason = UpdateCheckReason::NONE;
      updateCheckAttempt = 0;
      internetVerified = true;
      ntpFailed = false;
      if (runtimeState != RuntimeState::SETUP_AP) setRuntimeState(RuntimeState::RUNNING_ONLINE);
      restoreNetworkAfterFirmwareOperation();
      if (wifiSleepDeferredForUpdate && !updatePromptVisible && !portalActive) {
        wifiSleepDeferredForUpdate = false;
        enterWifiSleep();
      }
      return;
    }

    if (result == UpdateCheckAttemptResult::RETRYABLE_FAILURE &&
        updateCheckAttempt < UPDATE_CHECK_MAX_ATTEMPTS) {
      updateError = failure;
      // If the HTTPS attempt itself knocked the STA offline, let processNetwork()
      // perform its normal quick reconnect before consuming another GitHub
      // attempt. Otherwise use a short increasing backoff for transient
      // DNS/TLS/HTTP failures while the link remains healthy.
      const uint32_t retryDelay = stationNetworkReady() && timeIsValid()
                                    ? UPDATE_CHECK_ATTEMPT_BACKOFF_MS * updateCheckAttempt
                                    : WIFI_QUICK_RETRY_MS + 1000UL;
      updateCheckNotBeforeMs = millis() + retryDelay;
      logLine(String("manifest check retry scheduled after attempt ") + String(updateCheckAttempt) +
              " in " + String(retryDelay) + " ms: " + failure);
      return;
    }

    pendingUpdateCheckReason = UpdateCheckReason::NONE;
    if (reason == UpdateCheckReason::BOOT) bootUpdateCheckPending = false;
    const bool transient = result == UpdateCheckAttemptResult::RETRYABLE_FAILURE;
    failFirmwareCheck(failure.length() ? failure : "manifest check failed", transient);
    updateCheckAttempt = 0;
    restoreNetworkAfterFirmwareOperation();
    if (wifiSleepDeferredForUpdate && !updatePromptVisible && !portalActive) {
      wifiSleepDeferredForUpdate = false;
      enterWifiSleep();
    }
    return;
  }

  const bool retryWaiting = nextUpdateRetryMs != 0 && !deadlineReached(now, nextUpdateRetryMs);
  const bool retryDue = nextUpdateRetryMs != 0 && !retryWaiting;
  if (retryWaiting) return;
  if (!bootUpdateCheckPending && !firmwareUpdateCheckDue() && !retryDue) return;
  if (portalActive || resetConfirmation || thermalSafeMode || temperatureSensorFault || thermalWarning ||
      wifiTestState != WifiTestState::IDLE) return;

  if (stationNetworkReady() && timeIsValid() && freshNtpSinceBoot) {
    wifiSleepDeferredForUpdate = config.wifiSleep;
    updateCheckNotBeforeMs = millis() + UPDATE_NETWORK_SETTLE_MS;
    requestFirmwareUpdateCheck(bootUpdateCheckPending ? UpdateCheckReason::BOOT : UpdateCheckReason::WEEKLY);
  } else if (runtimeState == RuntimeState::WIFI_SLEEP && config.savedNetworkCount > 0) {
    updateCheckAfterNetworkReady = true;
    startSavedWifiSequence(false);
  }
}

void enterThermalSafeMode(bool sensorFault = false) {
  if (thermalSafeMode) return;
  thermalSafeMode = true;
  thermalSafeModeFromSensorFault = sensorFault;
  thermalWarning = true;
  thermalRecoveryStartedMs = 0;
  ntpRequestActive = false;
  ntpSyncEvent = false;
  wifiTestState = WifiTestState::IDLE;
  wifiTestError = "";
  pendingSsid = "";
  pendingPass = "";
  pendingWifiSecurity = WifiSecurityType::PERSONAL;
  pendingWifiUsername = "";
  pendingWifiIdentity = "";
  savedWifiScanActive = false;
  portalWifiScanActive = false;
  portalWifiScanDeadlineMs = 0;
  WiFi.scanDelete();
  if (!thermalThrottled) {
    thermalThrottled = setCpuFrequencyMhz(THERMAL_THROTTLE_CPU_MHZ);
  }
  stopMdns();
  if (portalActive) {
    stopMediaStream();
    dnsServer.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    portalActive = false;
    portalClosingAfterSuccess = false;
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  setRuntimeState(RuntimeState::RUNNING_OFFLINE);
  wakeDisplay();
  const int32_t thermalValue = isnan(chipTemperatureC) ? 0 : static_cast<int32_t>(chipTemperatureC * 10.0f);
  recordDiagnostic(MilestoneDiagnostics::Event::THERMAL_PROTECTION, sensorFault ? 1 : 0, thermalValue, true);
  if (sensorFault) {
    if (thermalThrottled) logLine("temperature sensor fault sustained; Wi-Fi off and CPU limited to 80 MHz");
    else logLine("temperature sensor fault sustained; Wi-Fi off (CPU frequency change failed)");
  } else {
    if (thermalThrottled) logLine("critical chip temperature sustained; Wi-Fi off and CPU limited to 80 MHz");
    else logLine("critical chip temperature sustained; Wi-Fi off (CPU frequency change failed)");
  }
}

void leaveThermalSafeMode() {
  thermalSafeMode = false;
  thermalSafeModeFromSensorFault = false;
  thermalCriticalStartedMs = 0;
  thermalRecoveryStartedMs = 0;
  bool frequencyRestored = true;
  if (thermalThrottled && normalCpuFrequencyMhz > THERMAL_THROTTLE_CPU_MHZ) {
    frequencyRestored = setCpuFrequencyMhz(normalCpuFrequencyMhz);
  }
  thermalThrottled = !frequencyRestored;
  if (frequencyRestored) {
    const int32_t thermalValue = isnan(chipTemperatureC) ? 0 : static_cast<int32_t>(chipTemperatureC * 10.0f);
    recordDiagnostic(MilestoneDiagnostics::Event::THERMAL_RECOVERED, 0, thermalValue, true);
    logLine("chip temperature recovered; normal operation resumed");
  } else logLine("chip temperature recovered; CPU frequency restore will be retried");
  if (config.savedNetworkCount > 0) startSavedWifiSequence(false);
  else {
    setRuntimeState(RuntimeState::UNPROVISIONED);
    startPortal();
  }
}

void processThermalProtection() {
  refreshChipTemperature();
  if (thermalProcessedSequence == temperatureSampleSequence) return;
  thermalProcessedSequence = temperatureSampleSequence;
  const uint32_t now = millis();

  if (temperatureSensorFault) {
    thermalWarning = true;
    if (!thermalThrottled) {
      thermalThrottled = setCpuFrequencyMhz(THERMAL_THROTTLE_CPU_MHZ);
      if (thermalThrottled) logLine("temperature sensor fault; CPU limited to 80 MHz");
      else logLine("temperature sensor fault; CPU frequency change failed");
    }
    if (temperatureFaultStartedMs == 0) {
      temperatureFaultStartedMs = now;
      recordDiagnostic(MilestoneDiagnostics::Event::TEMPERATURE_SENSOR_FAULT);
    } else if (!thermalSafeMode && elapsed(now, temperatureFaultStartedMs, TEMPERATURE_FAULT_SAFE_HOLD_MS)) {
      enterThermalSafeMode(true);
    }
    return;
  }
  temperatureFaultStartedMs = 0;
  if (isnan(chipTemperatureC)) return;
  const bool wasThermalWarning = thermalWarning;
  thermalWarning = thermalWarning ? chipTemperatureC >= THERMAL_WARNING_CLEAR_C
                                  : chipTemperatureC >= THERMAL_WARNING_C;
  if (!wasThermalWarning && thermalWarning) {
    recordDiagnostic(MilestoneDiagnostics::Event::THERMAL_WARNING, 0,
                     static_cast<int32_t>(chipTemperatureC * 10.0f));
  }

  if (!thermalThrottled && chipTemperatureC >= THERMAL_THROTTLE_C) {
    thermalThrottled = setCpuFrequencyMhz(THERMAL_THROTTLE_CPU_MHZ);
    thermalRecoveryStartedMs = 0;
    if (thermalThrottled) {
      recordDiagnostic(MilestoneDiagnostics::Event::THERMAL_THROTTLE, 0,
                       static_cast<int32_t>(chipTemperatureC * 10.0f));
      logLine(String("high chip temperature; CPU limited to 80 MHz: ") + String(chipTemperatureC, 1) + "C");
    } else {
      logLine(String("high chip temperature; CPU frequency change failed: ") + String(chipTemperatureC, 1) + "C");
    }
  }

  if (thermalSafeMode) {
    thermalWarning = true;
    if (chipTemperatureC <= THERMAL_SAFE_RECOVERY_C) {
      if (thermalRecoveryStartedMs == 0) thermalRecoveryStartedMs = now;
      else if (elapsed(now, thermalRecoveryStartedMs, THERMAL_RECOVERY_HOLD_MS)) leaveThermalSafeMode();
    } else {
      thermalRecoveryStartedMs = 0;
    }
    return;
  }

  if (chipTemperatureC >= THERMAL_CRITICAL_C) {
    if (thermalCriticalStartedMs == 0) thermalCriticalStartedMs = now;
    else if (elapsed(now, thermalCriticalStartedMs, THERMAL_CRITICAL_HOLD_MS)) enterThermalSafeMode(false);
  } else {
    thermalCriticalStartedMs = 0;
  }

  if (thermalThrottled && chipTemperatureC <= THERMAL_THROTTLE_RECOVERY_C) {
    if (thermalRecoveryStartedMs == 0) thermalRecoveryStartedMs = now;
    else if (elapsed(now, thermalRecoveryStartedMs, THERMAL_THROTTLE_RECOVERY_HOLD_MS)) {
      const bool restored = normalCpuFrequencyMhz <= THERMAL_THROTTLE_CPU_MHZ ||
                            setCpuFrequencyMhz(normalCpuFrequencyMhz);
      thermalRecoveryStartedMs = 0;
      if (restored) {
        thermalThrottled = false;
        logLine("chip temperature normalized; CPU frequency restored");
      } else {
        logLine("chip temperature normalized; CPU frequency restore failed, retrying later");
      }
    }
  } else if (chipTemperatureC > THERMAL_THROTTLE_RECOVERY_C) {
    thermalRecoveryStartedMs = 0;
  }
}

bool viewEnabled(View view) {
  if (view == View::CUSTOM_MEDIA && (!MILESTONE_HAS_MEDIA || !mediaHasEnabledItems())) return false;
  return (config.cycleMask & (1U << static_cast<uint8_t>(view))) != 0;
}

void selectFirstEnabledCycleView() {
  for (uint8_t i = 0; i < VIEW_COUNT; ++i) {
    uint8_t index = (config.cycleIndex + i) % VIEW_COUNT;
    View candidate = static_cast<View>(config.cycleOrder[index]);
    if (viewEnabled(candidate)) {
      currentCycleIndex = index;
      currentView = candidate;
      return;
    }
  }
  currentCycleIndex = 0;
  currentView = View::DDAY_TIME;
}

void advanceView(bool persist) {
  const View previousView = currentView;
  if (config.mode == TopMode::SELECTED_CYCLE) {
    for (uint8_t step = 1; step <= VIEW_COUNT; ++step) {
      uint8_t index = (currentCycleIndex + step) % VIEW_COUNT;
      View candidate = static_cast<View>(config.cycleOrder[index]);
      if (viewEnabled(candidate)) {
        currentCycleIndex = index;
        currentView = candidate;
        break;
      }
    }
  } else {
    do {
      currentView = static_cast<View>((static_cast<uint8_t>(currentView) + 1) % VIEW_COUNT);
    } while (currentView == View::CUSTOM_MEDIA && !MILESTONE_HAS_MEDIA);
  }
  if (previousView == View::CUSTOM_MEDIA && currentView != View::CUSTOM_MEDIA) closeMediaPlayback();
  if (currentView == View::CUSTOM_MEDIA && previousView != View::CUSTOM_MEDIA) selectNextMediaItem();
  if (currentView == View::DEVICE_INFO) deviceInfoStartedMs = millis();
  if (persist) {
    config.lastView = currentView;
    config.cycleIndex = currentCycleIndex;
    scheduleViewStateSave();
  }
  scrollStartedMs = millis();
  lastCycleMs = millis();
  if (persist) wakeDisplay();
}

void processCycle() {
  if (thermalSafeMode || bootSplashActive() || portalActive || resetConfirmation || buttonRawPressed ||
      buttonStablePressed || displaySleeping || firmwareUpdateBusy() || updatePromptVisible) return;
  if (config.mode == TopMode::CUSTOM_MEDIA && currentView == View::CUSTOM_MEDIA) {
    if (elapsed(millis(), lastCycleMs, static_cast<uint32_t>(currentMediaDisplaySeconds()) * 1000UL)) {
      selectNextMediaItem();
      lastCycleMs = millis();
    }
    return;
  }
  if (config.mode != TopMode::SELECTED_CYCLE || config.cycleIntervalSec == 0) return;
  const uint8_t interval = currentView == View::CUSTOM_MEDIA
                             ? currentMediaDisplaySeconds() : config.cycleIntervalSec;
  if (elapsed(millis(), lastCycleMs, static_cast<uint32_t>(interval) * 1000UL)) {
    advanceView(false);
  }
}

void performPhysicalFactoryReset() {
  if (oledReady) {
    display.clearBuffer();
    display.setFont(u8g2_font_7x14B_tf);
    drawCenteredStr("RESETTING...", 55);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("Settings cleared", 80);
    display.sendBuffer();
  }
  logLine("physical factory reset confirmed");
  if (!clearAllMediaFiles()) logLine("physical factory reset media clear incomplete");
  if (!prefs.clear()) {
    logLine("physical factory reset failed: NVS clear error");
    if (oledReady) {
      display.clearBuffer();
      display.setFont(u8g2_font_7x14B_tf);
      drawCenteredStr("RESET FAILED", 56);
      display.setFont(u8g2_font_6x10_tf);
      drawCenteredStr("Storage error", 82);
      display.sendBuffer();
    }
    resetConfirmation = false;
    return;
  }
  if (!clearDiagnosticHistory()) logLine("physical factory reset diagnostic clear failed");
  restartAfterFactoryReset();
}

void handleButtonRelease(uint32_t heldMs) {
  if (resetConfirmation) {
    if (resetConfirmPressEligible && heldMs >= RESET_CONFIRM_HOLD_MS) {
      performPhysicalFactoryReset();
    } else {
      resetConfirmation = false;
      resetConfirmPressEligible = false;
      wakeDisplay();
      logLine("factory reset confirmation cancelled");
    }
    return;
  }
  if (heldMs < 50) return;
  if (updateState == UpdateState::AVAILABLE && updatePromptVisible) {
    if (heldMs < 1000) {
      updateInstallRequested = true;
      updateInstallNotBeforeMs = millis();
      logLine("firmware installation approved with BOOT button");
    }
    return;
  }
  if (heldMs < 1000) {
    advanceView(true);
  } else if (heldMs < 3000) {
    // Deliberate no-op zone: prevents accidental setup entry.
  } else if (heldMs < 8000) {
    startPortal();
  } else {
    resetConfirmation = true;
    resetConfirmStartedMs = millis();
    resetConfirmPressEligible = false;
    wakeDisplay();
  }
}

void processButton() {
  const uint32_t now = millis();
  const bool rawPressed = digitalRead(PIN_BOOT) == LOW;
  if (rawPressed != buttonRawPressed) {
    buttonRawPressed = rawPressed;
    buttonRawChangedMs = now;
  }
  if (rawPressed != buttonStablePressed && elapsed(now, buttonRawChangedMs, BUTTON_DEBOUNCE_MS)) {
    buttonStablePressed = rawPressed;
    if (buttonStablePressed) {
      buttonPressedMs = now;
      if (resetConfirmation) {
        resetConfirmPressEligible = !elapsed(buttonRawChangedMs, resetConfirmStartedMs, RESET_CONFIRM_WINDOW_MS);
      }
      wakeDisplay();
    } else {
      handleButtonRelease(now - buttonPressedMs);
    }
  }
  if (resetConfirmation && !buttonRawPressed && elapsed(now, resetConfirmStartedMs, RESET_CONFIRM_WINDOW_MS)) {
    resetConfirmation = false;
    resetConfirmPressEligible = false;
    wakeDisplay();
    logLine("factory reset confirmation expired");
  }
}

void processDisplay() {
  if (!oledReady) return;
  const uint32_t now = millis();
  if (config.screenOffMin > 0 && !thermalSafeMode && !bootSplashActive() && !portalActive && !resetConfirmation && !buttonStablePressed &&
      !firmwareUpdateBusy() && !updatePromptVisible &&
      elapsed(now, lastInteractionMs, static_cast<uint32_t>(config.screenOffMin) * 60000UL)) {
    if (!displaySleeping) {
      display.setPowerSave(1);
      displaySleeping = true;
    }
    return;
  }
  if (mediaDisplayOwnsFrame()) {
    if (elapsed(now, lastDisplayMs, DISPLAY_REFRESH_MS)) updateContrast();
    if (renderMediaFrameIfNeeded(now)) {
      lastDisplayMs = now;
    } else if ((mediaPlaybackFailed || !mediaHasEnabledItems()) &&
               elapsed(now, lastDisplayMs, 1000UL)) {
      drawMediaUnavailable();
      lastDisplayMs = now;
    }
    return;
  }
  const uint32_t refreshMs = buttonStablePressed ? BUTTON_DISPLAY_REFRESH_MS : DISPLAY_REFRESH_MS;
  if (!elapsed(now, lastDisplayMs, refreshMs)) return;
  lastDisplayMs = now;
  updateContrast();
  if (buttonStablePressed) drawButtonOverlay(now - buttonPressedMs);
  else drawMainScreen();
}

void initializeView() {
  currentView = config.lastView;
  currentCycleIndex = config.cycleIndex % VIEW_COUNT;
  if (config.mode == TopMode::SELECTED_CYCLE) {
    selectFirstEnabledCycleView();
  }
  if (currentView == View::CUSTOM_MEDIA) {
    if (mediaHasEnabledItems()) selectNextMediaItem();
    else currentView = View::DASHBOARD;
  }
  if (currentView == View::DEVICE_INFO) deviceInfoStartedMs = millis();
  lastCycleMs = millis();
  scrollStartedMs = millis();
}


uint32_t mediaStreamPreviousCpuMhz = 240;
uint32_t mediaStreamLastThermalCheckMs = 0;
bool mediaStreamButtonPressed = false;
bool mediaStreamSntpWasEnabled = false;

void enterMediaStreamPerformanceMode() {
  suspendBluetoothNowPlaying();
  mediaStreamPreviousCpuMhz = getCpuFrequencyMhz();
  portalClosingAfterSuccess = false;
  portalStartedMs = millis();
  cancelPortalWifiScan();
  if (savedWifiScanActive) {
    WiFi.scanDelete();
    savedWifiScanActive = false;
  }
  stopMdns();
  WiFi.setSleep(false);
  mediaStreamSntpWasEnabled = esp_sntp_enabled();
  if (mediaStreamSntpWasEnabled) esp_sntp_stop();
  ntpRequestActive = false;
  ntpSyncEvent = false;
  ntpRequestStartedMs = 0;
  server.enableDelay(false);
  if (mediaStreamPreviousCpuMhz < 240U && !thermalThrottled) {
    if (!setCpuFrequencyMhz(240)) logLine("STREAM_MODE could not force 240 MHz CPU");
  }
  mediaStreamLastThermalCheckMs = 0;
  mediaStreamButtonPressed = digitalRead(PIN_BOOT) == LOW;
  buttonRawPressed = mediaStreamButtonPressed;
  buttonStablePressed = mediaStreamButtonPressed;
  buttonRawChangedMs = millis();
  buttonPressedMs = buttonRawChangedMs;
  // One fixed LED write on entry; normal LED animation is suspended until exit.
  writeStatusLed(0, 170, 255, 72);
  wakeDisplay();
}

void leaveMediaStreamPerformanceMode() {
  server.enableDelay(true);
  if (!thermalSafeMode && !thermalThrottled && mediaStreamPreviousCpuMhz > 0 &&
      getCpuFrequencyMhz() != mediaStreamPreviousCpuMhz) {
    if (!setCpuFrequencyMhz(mediaStreamPreviousCpuMhz)) {
      logLine("STREAM_MODE CPU restore failed");
    }
  }
  if (!thermalSafeMode && stationNetworkReady()) {
    startMdns();
    if (mediaStreamSntpWasEnabled) {
      configTzTime(TZ_INFO, "time.google.com", "pool.ntp.org", "time.cloudflare.com");
    }
  }
  mediaStreamSntpWasEnabled = false;
  if (portalActive && !thermalSafeMode) setRuntimeState(RuntimeState::SETUP_AP);
  appliedLedColor = UINT32_MAX;
  lastDisplayMs = 0;
  wakeDisplay();
  if (!thermalSafeMode) resumeBluetoothNowPlaying();
}

void processMediaStreamNetworkOnly() {
  if (!portalActive) {
    mediaStreamLastStopReason = "portal-closed";
    stopMediaStream();
    return;
  }
  portalClosingAfterSuccess = false;
  portalStartedMs = millis();
  dnsServer.processNextRequest();
  server.handleClient();
}

void processMediaStreamThermalOnly(uint32_t now) {
  if (!mediaStreamActive || !elapsed(now, mediaStreamLastThermalCheckMs, 1000UL)) return;
  mediaStreamLastThermalCheckMs = now;
  refreshChipTemperature(true);
  if (temperatureSensorFault || isnan(chipTemperatureC)) {
    mediaStreamLastStopReason = "temperature-sensor";
    stopMediaStream();
    return;
  }
  mediaStreamThermalWarning = chipTemperatureC >= MEDIA_STREAM_WARNING_C;
  if (chipTemperatureC >= MEDIA_STREAM_EMERGENCY_C) {
    mediaStreamLastStopReason = "temperature-emergency";
    // enterThermalSafeMode() marks the safety state before it stops the stream,
    // so STREAM_MODE exit cannot briefly restart mDNS/SNTP during an emergency.
    enterThermalSafeMode(false);
    return;
  }
  if (chipTemperatureC >= MEDIA_STREAM_STOP_C) {
    mediaStreamLastStopReason = "temperature-limit";
    stopMediaStream();
  }
}

void processMediaStreamButtonOnly(uint32_t now) {
  if (!mediaStreamActive) return;
  const bool rawPressed = digitalRead(PIN_BOOT) == LOW;
  if (rawPressed != buttonRawPressed) {
    buttonRawPressed = rawPressed;
    buttonRawChangedMs = now;
  }
  if (rawPressed != buttonStablePressed && elapsed(now, buttonRawChangedMs, BUTTON_DEBOUNCE_MS)) {
    const bool wasPressed = buttonStablePressed;
    buttonStablePressed = rawPressed;
    if (buttonStablePressed) {
      buttonPressedMs = now;
      mediaStreamButtonPressed = true;
    } else if (wasPressed && mediaStreamButtonPressed) {
      mediaStreamButtonPressed = false;
      mediaStreamLastStopReason = "button-stop";
      stopMediaStream();
    }
  }
}

void processMediaStreamMode() {
  processMediaStreamNetworkOnly();
  if (!mediaStreamActive) {
    delay(1);
    return;
  }
  const uint32_t now = millis();
  processMediaStreamThermalOnly(now);
  if (!mediaStreamActive) {
    delay(1);
    return;
  }
  processMediaStreamState(now);
  if (!mediaStreamActive) {
    delay(1);
    return;
  }
  processMediaStreamButtonOnly(now);
  if (!mediaStreamActive) {
    delay(1);
    return;
  }
  if (mediaStreamPhase == MediaStreamPhase::PLAYING) {
    renderMediaStreamFrameIfDue(millis());
  }
  delay(1);
}

void setupFirmware() {
  Serial.begin(115200);
  normalCpuFrequencyMhz = getCpuFrequencyMhz();
  statusLed.begin();
  statusLed.clear();
  statusLed.show();
  writeStatusLed(40, 255, 180);
  delay(250);
  logLine(String("booting firmware ") + currentFirmwareIdentity() + " [" + FIRMWARE_PROFILE_MARKER + "]");
  Serial.printf("[MILESTONE] reset=%s (%d), heap=%lu, min_heap=%lu, largest=%lu\n",
                resetReasonName(esp_reset_reason()), static_cast<int>(esp_reset_reason()),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  // ESP32 requires the STA hostname to be set before Wi-Fi is started.
  WiFi.setHostname(HOSTNAME);
  // The disconnect reason is valuable when distinguishing RF/AP failures from
  // DNS/TLS/GitHub failures. The callback itself remains diagnostic-only.
  WiFi.onEvent(wifiDisconnectedEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  buttonRawPressed = digitalRead(PIN_BOOT) == LOW;
  buttonStablePressed = buttonRawPressed;
  buttonRawChangedMs = millis();
  buttonPressedMs = buttonRawChangedMs;
  setenv("TZ", TZ_INFO, 1);
  tzset();
  sntp_set_time_sync_notification_cb(ntpTimeAvailable);

  prefsReady = prefs.begin(PREFS_NS, false);
  if (!prefsReady) {
    logLine("NVS STORAGE ERROR: Preferences namespace could not be opened");
  }
  if (prefsReady && prefs.getBool("stream_open", false)) {
    logLine(String("previous reset interrupted live stream: reset=") +
            resetReasonName(esp_reset_reason()) +
            " (" + String(static_cast<int>(esp_reset_reason())) + ")");
    prefs.putBool("stream_open", false);
  }
  diagnosticsPrefsReady = diagnosticsPrefs.begin(DIAGNOSTICS_PREFS_NS, false);
  if (!diagnosticsPrefsReady) logLine("diagnostics NVS namespace could not be opened");
  initializeDiagnosticStorage();
  recordDiagnostic(MilestoneDiagnostics::Event::BOOT, static_cast<int16_t>(esp_reset_reason()), 0, true);
  diagnosticBootValidated = false;
  diagnosticBootValidationPending = true;
  diagnosticBootValidationStartedMs = millis();
  lastOtaResult = prefsReady ? prefs.getString("ota_result", "") : "";
  const String previousOtaStage = prefsReady ? prefs.getString("ota_stage", "") : "";
  const String previousOtaTarget = prefsReady ? prefs.getString("ota_target", "") : "";
  initializeRollbackProtection(previousOtaStage, previousOtaTarget);
  if (previousOtaStage.length() && !otaBootConfirmationPending) {
    if (!(previousOtaStage == "rebooting" && otaTargetMatchesCurrent(previousOtaTarget))) {
      lastOtaResult = String("interrupted-") + previousOtaStage;
      logLine(String("previous OTA interrupted at ") + previousOtaStage +
              " target=" + previousOtaTarget + " reset=" + resetReasonName(esp_reset_reason()));
      if (!putStringVerified("ota_result", lastOtaResult)) logLine("interrupted OTA result save failed");
      if (!clearOtaAttemptRecord()) logLine("interrupted OTA record clear failed");
      if (!clearRollbackActiveRecord()) logLine("interrupted rollback record clear failed");
    }
  }
  const bool configLoaded = loadConfig();
  bool provisioned = configLoaded && config.savedNetworkCount > 0;
  lastUpdateCheckEpoch = prefsReady ? prefs.getULong64("ota_last_ok", 0) : 0;
  latestFirmwareVersion = prefsReady ? prefs.getString("ota_latest", "") : "";
  oledReady = initDisplay();
  if (oledReady) bootSplashStartedMs = millis();
  if (!oledReady) setRuntimeState(RuntimeState::ERROR_DISPLAY);
  if (MILESTONE_HAS_MEDIA) initializeMediaStorage();
  initializeView();
  if (MILESTONE_HAS_BLUETOOTH) {
    if (config.bluetoothNowPlaying) {
      if (!initializeBluetoothNowPlaying()) {
        logLine("Bluetooth Now Playing initialization failed; continuing without BLE");
      }
    }
  }
  lastInteractionMs = millis();

  if (provisioned) {
    startSavedWifiSequence(false);
  } else {
    setRuntimeState(RuntimeState::UNPROVISIONED);
    startPortal();
  }
}

void loopFirmware() {
  if (mediaStreamActive) {
    processMediaStreamMode();
    return;
  }

  processThermalProtection();
  processButton();
  processViewStateSave();
  processNetwork();
  // A /api/stream/start request can switch modes inside processNetwork().
  // Do not run any normal background subsystem after that transition.
  if (mediaStreamActive) {
    processMediaStreamMode();
    return;
  }
  processFirmwareUpdate();
  if (MILESTONE_HAS_BLUETOOTH) processBluetoothNowPlaying();
  processDiagnostics();
  processCycle();
  processDisplay();
  processLed();
  delay(1);  // Yield to the Wi-Fi/USB stacks; all application work remains non-blocking.
}
