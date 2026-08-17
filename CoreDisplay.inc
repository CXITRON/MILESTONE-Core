bool detectOledAddress() {
  for (uint8_t address : {OLED_ADDR_PRIMARY, OLED_ADDR_SECONDARY}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      oledAddress = address;
      return true;
    }
  }
  return false;
}

void drawCenteredStr(const char *text, int baseline, int8_t offsetX = 0) {
  const int width = display.getStrWidth(text);
  display.drawStr(max(0, (128 - width) / 2 + static_cast<int>(offsetX)), baseline, text);
}

void drawBootSplashFrame() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  drawCenteredStr("CYTRON//MILESTONE", 52);
  drawCenteredStr("MILESTONE D1", 72);
  display.setFont(u8g2_font_5x8_tf);
  String version = String("CORE ") + FIRMWARE_VERSION;
  drawCenteredStr(version.c_str(), 94);
  display.sendBuffer();
}

bool bootSplashActive() {
  return bootSplashStartedMs != 0 && !elapsed(millis(), bootSplashStartedMs, BOOT_SPLASH_MS);
}

bool initDisplay() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  if (!detectOledAddress()) {
    logLine("OLED not found at 0x3C or 0x3D");
    return false;
  }
  display.setI2CAddress(oledAddress << 1);  // U8g2 expects the 8-bit address.
  display.setBusClock(400000);
  display.begin();
  display.enableUTF8Print();
  display.setContrast(config.brightness);
  drawBootSplashFrame();
  logLine(String("OLED ready at 0x") + String(oledAddress, HEX));
  return true;
}

void wakeDisplay() {
  lastInteractionMs = millis();
  if (displaySleeping && oledReady) {
    display.setPowerSave(0);
    displaySleeping = false;
  }
}

bool isNightTime(const tm &local) {
  uint16_t minute = local.tm_hour * 60 + local.tm_min;
  if (config.nightStartMin == config.nightEndMin) return false;
  if (config.nightStartMin < config.nightEndMin) {
    return minute >= config.nightStartMin && minute < config.nightEndMin;
  }
  return minute >= config.nightStartMin || minute < config.nightEndMin;
}

void invalidateTimeDisplayCache() {
  cachedNightMinute = UINT64_MAX;
  appliedDisplayContrast = -1;
}

bool nightModeActive() {
  if (!timeIsValid()) return false;
  const uint64_t minute = static_cast<uint64_t>(time(nullptr)) / 60ULL;
  if (minute == cachedNightMinute) return cachedNightMode;
  tm local{};
  cachedNightMode = getLocalTime(&local, 10) && isNightTime(local);
  cachedNightMinute = minute;
  return cachedNightMode;
}

uint8_t activeLedBrightness() {
  return nightModeActive() ? config.ledNightLevel : config.ledBrightness;
}

uint8_t ledPulse(uint32_t periodMs, uint8_t minimum = 72) {
  if (periodMs < 2) return 255;
  uint32_t phase = millis() % periodMs;
  uint32_t half = periodMs / 2;
  uint32_t triangle = phase < half ? phase : periodMs - phase;
  uint32_t range = 255U - minimum;
  return static_cast<uint8_t>(minimum + (triangle * range) / half);
}

LedState currentLedState() {
  if (thermalSafeMode) return LedState::THERMAL_CRITICAL;
  if (resetConfirmation) return LedState::RESET_WARNING;
  if (buttonStablePressed) return LedState::BUTTON_HOLD;
  if (temperatureSensorFault) return LedState::TEMPERATURE_SENSOR_ERROR;
  if (thermalWarning) return LedState::THERMAL_WARNING;
  if (firmwareTransferActive()) return LedState::UPDATE_DOWNLOADING;
  if (updateState == UpdateState::CHECKING) return LedState::UPDATE_CHECKING;
  if (updateState == UpdateState::ERROR_STATE && !elapsed(millis(), updateStateStartedMs, 10000UL)) return LedState::UPDATE_ERROR;
  if (updateState == UpdateState::AVAILABLE && updatePromptVisible) return LedState::UPDATE_AVAILABLE;
  if (!oledReady) return LedState::DISPLAY_ERROR;
  if (wifiTestState == WifiTestState::CONNECTING) return LedState::CONNECTING;
  if (wifiTestState == WifiTestState::TIME_SYNCING) return LedState::TIME_SYNCING;
  if (portalClosingAfterSuccess) return LedState::ONLINE;
  if (portalActive) return LedState::SETUP;
  switch (runtimeState) {
    case RuntimeState::BOOTING: return LedState::BOOTING;
    case RuntimeState::UNPROVISIONED:
    case RuntimeState::SETUP_AP: return LedState::SETUP;
    case RuntimeState::CONNECTING: return LedState::CONNECTING;
    case RuntimeState::TIME_SYNCING: return LedState::TIME_SYNCING;
    case RuntimeState::RUNNING_ONLINE:
      return WiFi.status() == WL_CONNECTED && internetVerified && timeIsValid() && !ntpFailed
               ? LedState::ONLINE
               : LedState::NTP_ERROR;
    case RuntimeState::WIFI_SLEEP: return LedState::WIFI_SLEEP;
    case RuntimeState::ERROR_DISPLAY: return LedState::DISPLAY_ERROR;
    case RuntimeState::RUNNING_OFFLINE:
      return WiFi.status() == WL_CONNECTED ? LedState::NTP_ERROR : LedState::WIFI_ERROR;
  }
  return LedState::WIFI_ERROR;
}

void writeStatusLed(uint8_t red, uint8_t green, uint8_t blue, uint8_t animationScale = 255) {
  uint16_t brightness = activeLedBrightness();
  if (thermalSafeMode && brightness < 32) brightness = 32;
  else if ((thermalWarning || temperatureSensorFault) && brightness < 20) brightness = 20;
  red = static_cast<uint8_t>((static_cast<uint32_t>(red) * brightness * animationScale) / 65025UL);
  green = static_cast<uint8_t>((static_cast<uint32_t>(green) * brightness * animationScale) / 65025UL);
  blue = static_cast<uint8_t>((static_cast<uint32_t>(blue) * brightness * animationScale) / 65025UL);
  const uint32_t color = statusLed.Color(red, green, blue);
  if (color == appliedLedColor) return;
  appliedLedColor = color;
  statusLed.setPixelColor(0, color);
  statusLed.show();
}

void processLed() {
  uint32_t now = millis();
  if (!elapsed(now, lastLedMs, LED_REFRESH_MS)) return;
  lastLedMs = now;
  if (!config.ledEnabled && !thermalWarning && !temperatureSensorFault && !thermalSafeMode) {
    writeStatusLed(0, 0, 0);
    return;
  }

  switch (currentLedState()) {
    case LedState::BOOTING:
      writeStatusLed(40, 255, 180, ledPulse(1400, 96));
      break;
    case LedState::SETUP:
      writeStatusLed(0, 210, 255, ledPulse(1800, 88));
      break;
    case LedState::CONNECTING:
      writeStatusLed(35, 90, 255, ledPulse(1100, 72));
      break;
    case LedState::TIME_SYNCING:
      writeStatusLed(155, 65, 255, ledPulse(1100, 72));
      break;
    case LedState::ONLINE:
      writeStatusLed(35, 255, 165);
      break;
    case LedState::WIFI_SLEEP:
      writeStatusLed(20, 120, 125);
      break;
    case LedState::WIFI_ERROR:
      writeStatusLed(255, 28, 35, ledPulse(1800, 96));
      break;
    case LedState::NTP_ERROR:
      writeStatusLed(255, 120, 15, ledPulse(1800, 96));
      break;
    case LedState::DISPLAY_ERROR:
      writeStatusLed(255, 0, 90, ledPulse(650, 80));
      break;
    case LedState::THERMAL_WARNING:
      writeStatusLed(255, 95, 0, ledPulse(900, 96));
      break;
    case LedState::TEMPERATURE_SENSOR_ERROR:
      writeStatusLed(255, 45, 0, ledPulse(600, 80));
      break;
    case LedState::THERMAL_CRITICAL:
      writeStatusLed(255, 8, 8, ledPulse(420, 72));
      break;
    case LedState::UPDATE_CHECKING: {
      const uint32_t phase = millis() % 1600UL;
      const uint8_t on = (phase < 120 || (phase >= 260 && phase < 380)) ? 255 : 24;
      writeStatusLed(45, 105, 255, on);
      break;
    }
    case LedState::UPDATE_AVAILABLE: {
      const uint32_t phase = millis() % 2200UL;
      const uint8_t on = (phase < 180 || (phase >= 360 && phase < 540)) ? 255 : 20;
      writeStatusLed(235, 235, 255, on);
      break;
    }
    case LedState::UPDATE_DOWNLOADING:
      writeStatusLed(115, 65, 255, ledPulse(520, 52));
      break;
    case LedState::UPDATE_ERROR: {
      const uint32_t phase = millis() % 1800UL;
      const uint8_t on = (phase < 110 || (phase >= 220 && phase < 330) || (phase >= 440 && phase < 550)) ? 255 : 24;
      writeStatusLed(255, 25, 35, on);
      break;
    }
    case LedState::BUTTON_HOLD: {
      uint32_t heldMs = millis() - buttonPressedMs;
      if (resetConfirmation || heldMs >= 8000) writeStatusLed(255, 15, 25, ledPulse(500, 100));
      else if (heldMs >= 3000) writeStatusLed(0, 220, 255);
      else writeStatusLed(210, 210, 210);
      break;
    }
    case LedState::RESET_WARNING:
      writeStatusLed(255, 10, 20, ledPulse(500, 100));
      break;
  }
}

void updateContrast() {
  if (!oledReady) return;
  const uint8_t level = nightModeActive() ? config.nightLevel : config.brightness;
  if (appliedDisplayContrast == level) return;
  display.setContrast(level);
  appliedDisplayContrast = level;
}

void getBurninOffset(int8_t &x, int8_t &y) {
  x = 0;
  y = 0;
  if (!config.burninShift) return;
  uint8_t phase = (millis() / 60000UL) % 9;
  x = static_cast<int8_t>(phase % 3) - 1;
  y = static_cast<int8_t>(phase / 3) - 1;
}

bool stopChipTemperatureSensor() {
  if (chipTemperatureSensor == nullptr) return true;
  esp_err_t disableResult = temperature_sensor_disable(chipTemperatureSensor);
  if (disableResult != ESP_OK && disableResult != ESP_ERR_INVALID_STATE) {
    logLine(String("chip temperature sensor disable failed: ") + String(static_cast<int>(disableResult)));
  }
  esp_err_t uninstallResult = temperature_sensor_uninstall(chipTemperatureSensor);
  if (uninstallResult != ESP_OK) {
    logLine(String("chip temperature sensor uninstall failed: ") + String(static_cast<int>(uninstallResult)));
    return false;
  }
  chipTemperatureSensor = nullptr;
  return true;
}

bool initChipTemperatureSensor(bool highRange) {
  if (chipTemperatureSensor != nullptr) return true;
  temperature_sensor_config_t sensorConfig = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  if (highRange) {
    sensorConfig.range_min = 50;
    sensorConfig.range_max = 125;
  }
  esp_err_t result = temperature_sensor_install(&sensorConfig, &chipTemperatureSensor);
  if (result != ESP_OK) {
    chipTemperatureSensor = nullptr;
    logLine(String("chip temperature sensor install failed: ") + String(static_cast<int>(result)));
    return false;
  }
  result = temperature_sensor_enable(chipTemperatureSensor);
  if (result != ESP_OK) {
    temperature_sensor_uninstall(chipTemperatureSensor);
    chipTemperatureSensor = nullptr;
    logLine(String("chip temperature sensor enable failed: ") + String(static_cast<int>(result)));
    return false;
  }
  temperatureSensorHighRange = highRange;
  return true;
}

bool switchChipTemperatureRange(bool highRange) {
  if (chipTemperatureSensor != nullptr && temperatureSensorHighRange == highRange) return true;
  if (!stopChipTemperatureSensor()) return false;
  return initChipTemperatureSensor(highRange);
}

bool readChipTemperatureOnce(float &reading) {
  return initChipTemperatureSensor(temperatureSensorHighRange) &&
         temperature_sensor_get_celsius(chipTemperatureSensor, &reading) == ESP_OK &&
         !isnan(reading);
}

bool refreshChipTemperature(bool force = false) {
  uint32_t now = millis();
  if (!force && lastTemperatureReadMs != 0 && !elapsed(now, lastTemperatureReadMs, TEMPERATURE_REFRESH_MS)) return false;
  lastTemperatureReadMs = now;
  float reading = NAN;
  bool success = readChipTemperatureOnce(reading);
  if (!success) {
    const bool alternateHighRange = !temperatureSensorHighRange;
    success = switchChipTemperatureRange(alternateHighRange) && readChipTemperatureOnce(reading);
  }
  if (success && reading >= 20.0f && reading <= 125.0f) {
    chipTemperatureC = reading;
    if (isnan(highestChipTemperatureC) || reading > highestChipTemperatureC) highestChipTemperatureC = reading;
    temperatureReadFailureCount = 0;
    temperatureSensorFault = false;
    if (!temperatureSensorHighRange && reading >= TEMPERATURE_HIGH_RANGE_ENTER_C) {
      switchChipTemperatureRange(true);
    } else if (temperatureSensorHighRange && reading <= TEMPERATURE_HIGH_RANGE_EXIT_C) {
      switchChipTemperatureRange(false);
    }
  } else {
    chipTemperatureC = NAN;
    if (temperatureReadFailureCount < UINT8_MAX) ++temperatureReadFailureCount;
    if (temperatureReadFailureCount >= TEMPERATURE_FAULT_SAMPLE_COUNT) temperatureSensorFault = true;
  }
  ++temperatureSampleSequence;
  return true;
}

void drawChipTemperature(int statusX, int y) {
  if (!config.showChipTemperature && !thermalWarning && !temperatureSensorFault) return;
  refreshChipTemperature();
  char buffer[8];
  if (temperatureSensorFault) {
    snprintf(buffer, sizeof(buffer), "!TC");
  } else if (isnan(chipTemperatureC)) {
    snprintf(buffer, sizeof(buffer), "--C");
  } else {
    int rounded = static_cast<int>(chipTemperatureC >= 0.0f ? chipTemperatureC + 0.5f : chipTemperatureC - 0.5f);
    snprintf(buffer, sizeof(buffer), thermalWarning ? "!%dC" : "%dC", rounded);
  }
  display.setFont(u8g2_font_5x8_tf);
  int width = display.getStrWidth(buffer);
  display.drawStr(statusX - width - 4, y + 7, buffer);
}

void drawStatusIcon(int x, int y) {
  if (portalActive) {
    display.setFont(u8g2_font_5x8_tf);
    display.drawStr(x - 2, y + 7, "AP");
    return;
  }
  if (ntpRequestActive || runtimeState == RuntimeState::TIME_SYNCING) {
    // Letter icons remain distinguishable on the low-resolution OLED even
    // when circular/spinner glyphs blur together.
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(x + 2, y + 8, "T");
    return;
  }
  if (updateState == UpdateState::CHECKING) {
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(x + 2, y + 8, "U");
    updateCheckIndicatorRendered = true;
    return;
  }
  if (runtimeState == RuntimeState::WIFI_SLEEP) {
    display.drawCircle(x + 5, y + 5, 4);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (!timeIsValid()) {
      display.setFont(u8g2_font_6x10_tf);
      display.drawStr(x + 2, y + 8, "?");
    } else {
      display.drawLine(x + 1, y + 1, x + 9, y + 9);
      display.drawLine(x + 9, y + 1, x + 1, y + 9);
    }
    return;
  }
  if (ntpFailed || !internetVerified) {
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(x + 2, y + 8, "!");
    return;
  }
  if (!timeIsValid()) {
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(x + 2, y + 8, "?");
    return;
  }
  if (runtimeState == RuntimeState::RUNNING_ONLINE) {
    display.drawDisc(x + 5, y + 5, 4);
    return;
  }
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(x + 2, y + 8, "!");
}

void drawTopTitle(const String &title, int8_t offsetX, int8_t offsetY) {
  display.setFont(u8g2_font_unifont_t_korean2);
  String shown = title;
  const int titleWidth = (thermalWarning || temperatureSensorFault) ? 84 : (config.showChipTemperature ? 89 : 103);
  while (shown.length() > 0 && display.getUTF8Width(shown.c_str()) > titleWidth) {
    int last = shown.length() - 1;
    while (last > 0 && (static_cast<uint8_t>(shown[last]) & 0xC0) == 0x80) --last;
    shown.remove(last);
  }
  display.drawUTF8(clampInt(static_cast<int>(offsetX), 0, 127), 15 + offsetY, shown.c_str());
  drawChipTemperature(116 + offsetX, 2 + offsetY);
  drawStatusIcon(116 + offsetX, 2 + offsetY);
  display.drawHLine(0, 17 + offsetY, 128);
}

String formatDday(int days) {
  if (days > 0) {
    if (config.ddayTextStyle) return String(days) + "일 남음";
    return String("D-") + days;
  }
  if (days == 0) return "D-DAY";
  if (config.afterComplete) return "";
  return String("D+") + (-days);
}

bool computeDday(int &result, bool force = false) {
  if (!timeIsValid()) {
    result = config.lastDday;
    return false;
  }
  int year, month, day;
  if (!parseDate(config.target, year, month, day)) return false;
  tm nowLocal{};
  if (!getLocalTime(&nowLocal, 20)) return false;
  bool due = force || lastDdayYear < 0;
  if (!due && config.ddayPeriodSec == 0) {
    due = nowLocal.tm_year != lastDdayYear || nowLocal.tm_yday != lastDdayYearDay;
  } else if (!due) {
    due = elapsed(millis(), lastDdayCalcMs, config.ddayPeriodSec * 1000UL);
  }
  if (!due) {
    result = config.lastDday;
    return true;
  }
  const MilestoneCoreLogic::CivilDate today{nowLocal.tm_year + 1900, nowLocal.tm_mon + 1, nowLocal.tm_mday};
  const MilestoneCoreLogic::CivilDate target{year, month, day};
  result = MilestoneCoreLogic::daysBetween(today, target);
  lastDdayCalcMs = millis();
  lastDdayYear = nowLocal.tm_year;
  lastDdayYearDay = nowLocal.tm_yday;
  if (result != config.lastDday) {
    config.lastDday = result;
    if (prefs.putInt("last_dday", result) != sizeof(int32_t)) logLine("D-day cache save failed");
  }
  return true;
}

String weekdayKorean(int weekday) {
  static const char *names[] = {"일", "월", "화", "수", "목", "금", "토"};
  if (weekday < 0 || weekday > 6) return "?";
  return names[weekday];
}

String formatTimeLine(bool includeDate) {
  if (!timeIsValid()) return "시간 미확정";
  tm local{};
  if (!getLocalTime(&local, 20)) return "시간 미확정";
  char buffer[40];
  int hour = local.tm_hour;
  String prefix;
  if (!config.hour24) {
    prefix = hour < 12 ? "AM " : "PM ";
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  if (config.showSeconds) snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hour, local.tm_min, local.tm_sec);
  else snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, local.tm_min);
  String out = prefix + buffer;
  if (includeDate) {
    snprintf(buffer, sizeof(buffer), " · %02d.%02d ", local.tm_mon + 1, local.tm_mday);
    out += buffer;
    out += weekdayKorean(local.tm_wday);
  }
  return out;
}

void drawCenteredUtf8(const String &text, int baseline, int8_t offsetX = 0) {
  int width = display.getUTF8Width(text.c_str());
  display.drawUTF8(clampInt((128 - width) / 2 + static_cast<int>(offsetX), 0, 127), baseline, text.c_str());
}

void drawAlignedUtf8(const String &text, int baseline, int left, int width, int8_t offsetX = 0) {
  int textWidth = display.getUTF8Width(text.c_str());
  int x = config.messageLeft ? left : left + (width - textWidth) / 2;
  display.drawUTF8(clampInt(x + static_cast<int>(offsetX), left - 1, 127), baseline, text.c_str());
}

void drawScrollingUtf8(String text, int baseline, int left, int width, bool centerIfFits) {
  text.replace("\r", "");
  text.replace("\n", " ");
  int textWidth = display.getUTF8Width(text.c_str());
  if (!config.messageScroll || textWidth <= width) {
    int x = config.messageLeft || !centerIfFits ? left : left + (width - textWidth) / 2;
    display.drawUTF8(x < left ? left : x, baseline, text.c_str());
    return;
  }
  uint32_t elapsedMs = millis() - scrollStartedMs;
  int travel = textWidth + width + 12;
  int shift = static_cast<int>((elapsedMs * config.scrollSpeed / 1000UL) % travel);
  int x = left + width - shift;
  display.setClipWindow(left, clampInt(baseline - 16, 0, 127),
                        left + width - 1, clampInt(baseline + 2, 0, 127));
  display.drawUTF8(x, baseline, text.c_str());
  display.setMaxClipWindow();
}

void addEllipsisToFit(String &text, int width) {
  const String suffix = "...";
  while (text.length() > 0 && display.getUTF8Width((text + suffix).c_str()) > width) {
    int last = text.length() - 1;
    while (last > 0 && (static_cast<uint8_t>(text[last]) & 0xC0) == 0x80) --last;
    text.remove(last);
  }
  text += suffix;
}

bool splitMessageLines(const String &text, String &line1, String &line2, int maxWidth = 124) {
  line1 = "";
  line2 = "";
  String current;
  bool second = false;
  bool overflow = false;
  for (size_t i = 0; i < text.length();) {
    size_t bytes = 1;
    uint8_t first = static_cast<uint8_t>(text[i]);
    if ((first & 0xE0) == 0xC0) bytes = 2;
    else if ((first & 0xF0) == 0xE0) bytes = 3;
    else if ((first & 0xF8) == 0xF0) bytes = 4;
    size_t end = i + bytes;
    if (end > text.length()) end = text.length();
    String character = text.substring(i, end);
    if (character == "\n") {
      if (!second) {
        line1 = current;
        current = "";
        second = true;
      } else {
        overflow = true;
        break;
      }
      i += bytes;
      continue;
    }
    String candidate = current + character;
    if (display.getUTF8Width(candidate.c_str()) > maxWidth && current.length() > 0) {
      if (!second) {
        line1 = current;
        current = character;
        second = true;
      } else {
        overflow = true;
        break;
      }
    } else {
      current = candidate;
    }
    i += bytes;
  }
  if (!second) line1 = current;
  else line2 = current;
  return overflow;
}

void drawMessageBlock(int8_t ox, int8_t oy, int singleY, int firstY, int secondY) {
  display.setFont(u8g2_font_unifont_t_korean2);
  String line1, line2;
  bool overflow = splitMessageLines(config.message, line1, line2);
  if (overflow && !config.messageScroll && line2.length() == 0) line2 = "...";
  if (overflow && config.messageScroll) {
    drawScrollingUtf8(config.message, singleY + oy, 1, 126, true);
  } else if (line2.length() != 0) {
    if (overflow) addEllipsisToFit(line2, 124);
    drawAlignedUtf8(line1, firstY + oy, 2, 124, ox);
    drawAlignedUtf8(line2, secondY + oy, 2, 124, ox);
  } else {
    drawAlignedUtf8(line1, singleY + oy, 2, 124, ox);
  }
}

void drawDdayView(bool withMessage, int8_t ox, int8_t oy) {
  drawTopTitle(config.title, ox, oy);
  int days = config.lastDday;
  bool confirmed = computeDday(days);
  String dday = formatDday(days);
  if (!confirmed) dday += "?";
  if (config.ddayTextStyle) display.setFont(u8g2_font_unifont_t_korean2);
  else display.setFont(u8g2_font_logisoso32_tf);
  drawCenteredUtf8(dday, 65 + oy, ox);
  if (withMessage) {
    display.setFont(u8g2_font_unifont_t_korean2);
    drawScrollingUtf8(config.message, 118 + oy, 1, 126, true);
    return;
  }

  // D-day + time mode uses two fixed rows. Date and time never scroll.
  tm local{};
  if (timeIsValid() && getLocalTime(&local, 20)) {
    char dateBuffer[20];
    strftime(dateBuffer, sizeof(dateBuffer), "%Y.%m.%d", &local);
    display.setFont(u8g2_font_unifont_t_korean2);
    drawCenteredUtf8(String(dateBuffer) + " " + weekdayKorean(local.tm_wday), 92 + oy, ox);
    String clockLine = formatTimeLine(false);
    display.setFont(u8g2_font_logisoso20_tf);
    if (display.getUTF8Width(clockLine.c_str()) > 126) display.setFont(u8g2_font_6x10_tf);
    drawCenteredUtf8(clockLine, 120 + oy, ox);
  } else {
    display.setFont(u8g2_font_unifont_t_korean2);
    drawCenteredUtf8("날짜 미확정", 92 + oy, ox);
    display.setFont(u8g2_font_logisoso20_tf);
    drawCenteredUtf8("--:--", 120 + oy, ox);
  }
}

void drawMessageOnly(int8_t ox, int8_t oy) {
  drawTopTitle("MILESTONE", ox, oy);
  drawMessageBlock(ox, oy, 75, 59, 83);
}

void drawClockOnly(int8_t ox, int8_t oy) {
  drawTopTitle("현재 시각", ox, oy);
  tm local{};
  if (!timeIsValid() || !getLocalTime(&local, 20)) {
    display.setFont(u8g2_font_unifont_t_korean2);
    drawCenteredUtf8("시간 미확정", 66 + oy, ox);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("HOLD BOOT: SETUP", 92 + oy, ox);
    return;
  }
  int hour = local.tm_hour;
  String prefix;
  if (!config.hour24) {
    prefix = hour < 12 ? "AM " : "PM ";
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  char timeBuffer[16];
  if (config.showSeconds) snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", hour, local.tm_min, local.tm_sec);
  else snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", hour, local.tm_min);
  String clock = prefix + timeBuffer;
  display.setFont(config.showSeconds ? u8g2_font_logisoso20_tf : u8g2_font_logisoso28_tf);
  if (display.getUTF8Width(clock.c_str()) > 126) display.setFont(u8g2_font_6x10_tf);
  drawCenteredUtf8(clock, 61 + oy, ox);
  char dateBuffer[20];
  strftime(dateBuffer, sizeof(dateBuffer), "%Y.%m.%d", &local);
  display.setFont(u8g2_font_6x10_tf);
  int dateWidth = display.getStrWidth(dateBuffer);
  display.drawStr((128 - dateWidth) / 2 + ox, 83 + oy, dateBuffer);
  display.setFont(u8g2_font_unifont_t_korean2);
  drawCenteredUtf8(weekdayKorean(local.tm_wday) + "요일 · KST", 113 + oy, ox);
}

void drawMessageClock(int8_t ox, int8_t oy) {
  String dateTitle = "날짜 미확정";
  if (timeIsValid()) {
    tm local{};
    if (getLocalTime(&local, 20)) {
      char dateBuffer[16];
      strftime(dateBuffer, sizeof(dateBuffer), "%Y.%m.%d", &local);
      dateTitle = dateBuffer;
    }
  }
  drawTopTitle(dateTitle, ox, oy);
  if (timeIsValid()) {
    display.setFont(config.showSeconds ? u8g2_font_logisoso20_tf : u8g2_font_logisoso28_tf);
    String clockLine = formatTimeLine(false);
    if (display.getUTF8Width(clockLine.c_str()) > 126) display.setFont(u8g2_font_6x10_tf);
    drawCenteredUtf8(clockLine, 57 + oy, ox);
  } else {
    display.setFont(u8g2_font_unifont_t_korean2);
    drawCenteredUtf8("시간 미확정", 53 + oy, ox);
  }
  display.drawHLine(4, 68 + oy, 120);
  drawMessageBlock(ox, oy, 105, 94, 118);
}

void drawDashboard(int8_t ox, int8_t oy) {
  drawTopTitle(config.title, ox, oy);

  int days = config.lastDday;
  bool confirmed = computeDday(days);
  String dday = formatDday(days);
  if (!confirmed) dday += "?";
  display.setFont(config.ddayTextStyle ? u8g2_font_unifont_t_korean2 : u8g2_font_logisoso20_tf);
  drawCenteredUtf8(dday, 45 + oy, ox);

  tm local{};
  if (timeIsValid() && getLocalTime(&local, 20)) {
    display.setFont(config.showSeconds ? u8g2_font_6x10_tf : u8g2_font_logisoso20_tf);
    drawCenteredUtf8(formatTimeLine(false), 72 + oy, ox);
    char dateBuffer[20];
    strftime(dateBuffer, sizeof(dateBuffer), "%Y.%m.%d", &local);
    display.setFont(u8g2_font_unifont_t_korean2);
    drawCenteredUtf8(String(dateBuffer) + " " + weekdayKorean(local.tm_wday), 96 + oy, ox);
  } else {
    display.setFont(u8g2_font_unifont_t_korean2);
    drawCenteredUtf8("시간 미확정", 78 + oy, ox);
  }

  display.drawHLine(4, 101 + oy, 120);
  display.setFont(u8g2_font_unifont_t_korean2);
  drawScrollingUtf8(config.message, 123 + oy, 1, 126, true);
}

void formatByteCount(uint32_t bytes, char *output, size_t outputSize) {
  if (bytes >= 1024UL * 1024UL) {
    snprintf(output, outputSize, "%lu.%02lu MB", static_cast<unsigned long>(bytes / (1024UL * 1024UL)),
             static_cast<unsigned long>((bytes % (1024UL * 1024UL)) * 100UL / (1024UL * 1024UL)));
  } else if (bytes >= 1024UL) {
    snprintf(output, outputSize, "%lu.%01lu KB", static_cast<unsigned long>(bytes / 1024UL),
             static_cast<unsigned long>((bytes % 1024UL) * 10UL / 1024UL));
  } else {
    snprintf(output, outputSize, "%lu B", static_cast<unsigned long>(bytes));
  }
}

void formatUptime(char *output, size_t outputSize) {
  const uint64_t totalSeconds = uptimeSeconds();
  const uint64_t days = totalSeconds / 86400ULL;
  const uint8_t hours = (totalSeconds / 3600UL) % 24UL;
  const uint8_t minutes = (totalSeconds / 60UL) % 60UL;
  const uint8_t seconds = totalSeconds % 60UL;
  snprintf(output, outputSize, "%llud %02u:%02u:%02u", static_cast<unsigned long long>(days),
           hours, minutes, seconds);
}

void formatEpochShort(uint64_t epoch, char *output, size_t outputSize) {
  if (epoch < static_cast<uint64_t>(MIN_VALID_EPOCH)) {
    snprintf(output, outputSize, "NEVER");
    return;
  }
  const time_t value = static_cast<time_t>(epoch);
  tm local{};
  localtime_r(&value, &local);
  snprintf(output, outputSize, "%02d-%02d %02d:%02d", local.tm_mon + 1, local.tm_mday,
           local.tm_hour, local.tm_min);
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWER ON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT WDT";
    case ESP_RST_TASK_WDT: return "TASK WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

void drawDeviceInfoHeader(const char *title, uint8_t page, int8_t ox, int8_t oy) {
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(1 + ox, 10 + oy, title);
  if (updateState == UpdateState::CHECKING) {
    // Device-info normally uses the upper-right corner for its page counter.
    // While checking for firmware, reserve that corner for the same U indicator
    // used by all other normal views so the check cannot be invisible.
    display.drawStr(118 + ox, 10 + oy, "U");
    updateCheckIndicatorRendered = true;
  } else {
    char pageText[8];
    snprintf(pageText, sizeof(pageText), "%u/%u", page + 1, DEVICE_INFO_PAGE_COUNT);
    display.drawStr(108 + ox, 10 + oy, pageText);
  }
  display.drawHLine(0, 14 + oy, 128);
  display.setFont(u8g2_font_5x8_tf);
}

void drawDeviceInfoLine(uint8_t y, const char *label, const char *value, int8_t ox, int8_t oy) {
  char line[30];
  snprintf(line, sizeof(line), "%s: %s", label, value);
  display.drawStr(2 + ox, y + oy, line);
}

void drawDeviceInfo(int8_t ox, int8_t oy) {
  const uint8_t page = static_cast<uint8_t>(((millis() - deviceInfoStartedMs) / DEVICE_INFO_PAGE_MS) %
                                             DEVICE_INFO_PAGE_COUNT);
  display.setFont(u8g2_font_5x8_tf);
  char value[32];

  if (page == 0) {
    drawDeviceInfoHeader("SYSTEM", page, ox, oy);
    drawDeviceInfoLine(29, "FW", FIRMWARE_VERSION, ox, oy);
    formatUptime(value, sizeof(value));
    drawDeviceInfoLine(45, "UP", value, ox, oy);
    drawDeviceInfoLine(61, "RESET", resetReasonName(esp_reset_reason()), ox, oy);
    snprintf(value, sizeof(value), "%s R%u", ESP.getChipModel(), ESP.getChipRevision());
    drawDeviceInfoLine(77, "CHIP", value, ox, oy);
    snprintf(value, sizeof(value), "%uC %lu MHz", ESP.getChipCores(),
             static_cast<unsigned long>(getCpuFrequencyMhz()));
    drawDeviceInfoLine(93, "CPU", value, ox, oy);
    drawDeviceInfoLine(109, "CORE", ESP.getCoreVersion(), ox, oy);
    if (isnan(chipTemperatureC)) snprintf(value, sizeof(value), "SENSOR ERROR");
    else snprintf(value, sizeof(value), "%.1f C %s", chipTemperatureC,
                  thermalWarning ? "WARN" : (thermalThrottled ? "LIMIT" : "OK"));
    drawDeviceInfoLine(125, "TEMP", value, ox, oy);
  } else if (page == 1) {
    drawDeviceInfoHeader("MEMORY", page, ox, oy);
    formatByteCount(ESP.getHeapSize(), value, sizeof(value));
    drawDeviceInfoLine(29, "HEAP", value, ox, oy);
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t heapSize = ESP.getHeapSize();
    char bytes[18];
    formatByteCount(heapSize - freeHeap, bytes, sizeof(bytes));
    snprintf(value, sizeof(value), "%s %lu%%", bytes,
             static_cast<unsigned long>(heapSize ? (heapSize - freeHeap) * 100ULL / heapSize : 0));
    drawDeviceInfoLine(47, "USED", value, ox, oy);
    formatByteCount(freeHeap, bytes, sizeof(bytes));
    drawDeviceInfoLine(65, "FREE", bytes, ox, oy);
    formatByteCount(ESP.getMinFreeHeap(), value, sizeof(value));
    drawDeviceInfoLine(83, "MIN", value, ox, oy);
    formatByteCount(ESP.getMaxAllocHeap(), value, sizeof(value));
    drawDeviceInfoLine(101, "BLOCK", value, ox, oy);
    formatByteCount(uxTaskGetStackHighWaterMark(nullptr), value, sizeof(value));
    drawDeviceInfoLine(119, "STACK", value, ox, oy);
  } else if (page == 2) {
    drawDeviceInfoHeader("STORAGE", page, ox, oy);
    formatByteCount(ESP.getFlashChipSize(), value, sizeof(value));
    drawDeviceInfoLine(33, "FLASH", value, ox, oy);
    snprintf(value, sizeof(value), "%lu MHz", static_cast<unsigned long>(ESP.getFlashChipSpeed() / 1000000UL));
    drawDeviceInfoLine(53, "SPEED", value, ox, oy);
    formatByteCount(ESP.getSketchSize(), value, sizeof(value));
    drawDeviceInfoLine(73, "APP", value, ox, oy);
    formatByteCount(ESP.getFreeSketchSpace(), value, sizeof(value));
    drawDeviceInfoLine(93, "OTA FREE", value, ox, oy);
    drawDeviceInfoLine(113, "VERIFY", "SIZE + SHA256", ox, oy);
  } else if (page == 3) {
    drawDeviceInfoHeader("NETWORK", page, ox, oy);
    const bool connected = WiFi.status() == WL_CONNECTED;
    const char *networkState = runtimeState == RuntimeState::WIFI_SLEEP ? "SLEEP" :
                               (connected && internetVerified ? "INTERNET OK" :
                               (connected ? "WIFI ONLY" : "OFFLINE"));
    drawDeviceInfoLine(29, "STATE", networkState, ox, oy);
    String ssid = connected ? WiFi.SSID() : String("-");
    if (ssid.length() > 19) ssid = ssid.substring(0, 19);
    drawDeviceInfoLine(45, "SSID", ssid.c_str(), ox, oy);
    if (connected) snprintf(value, sizeof(value), "%ld dBm CH%ld", static_cast<long>(WiFi.RSSI()),
                            static_cast<long>(WiFi.channel()));
    else snprintf(value, sizeof(value), "-");
    drawDeviceInfoLine(61, "RADIO", value, ox, oy);
    String address = connected ? WiFi.localIP().toString() : String("-");
    drawDeviceInfoLine(77, "IP", address.c_str(), ox, oy);
    address = connected ? WiFi.gatewayIP().toString() : String("-");
    drawDeviceInfoLine(93, "GW", address.c_str(), ox, oy);
    const String mac = WiFi.macAddress();
    drawDeviceInfoLine(109, "MAC", mac.c_str(), ox, oy);
  } else {
    drawDeviceInfoHeader("TIME / UPDATE", page, ox, oy);
    drawDeviceInfoLine(29, "CLOCK", timeIsValid() ? "VALID" : "NOT SET", ox, oy);
    formatEpochShort(config.lastSync, value, sizeof(value));
    drawDeviceInfoLine(45, "SYNC", value, ox, oy);
    String otaSummary = String(updateStateName(updateState));
    if (lastOtaResult.length()) otaSummary += "/" + lastOtaResult;
    drawDeviceInfoLine(61, "OTA", otaSummary.c_str(), ox, oy);
    drawDeviceInfoLine(77, "LATEST", latestFirmwareVersion.length() ? latestFirmwareVersion.c_str() : "UNKNOWN", ox, oy);
    formatEpochShort(lastUpdateCheckEpoch, value, sizeof(value));
    drawDeviceInfoLine(93, "CHECK", value, ox, oy);
    drawDeviceInfoLine(109, "SECURE", "HTTPS + SHA256", ox, oy);
    drawDeviceInfoLine(125, "IDF", ESP.getSdkVersion(), ox, oy);
  }
}

void drawPortalScreen() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 10, "MILESTONE SETUP");
  if (ntpRequestActive || runtimeState == RuntimeState::TIME_SYNCING) {
    display.drawStr(118, 10, "T");
  } else if (updateState == UpdateState::CHECKING) {
    display.drawStr(118, 10, "U");
    updateCheckIndicatorRendered = true;
  } else {
    display.drawStr(116, 10, "AP");
  }
  display.drawHLine(0, 14, 128);
  display.drawStr(2, 33, "Wi-Fi:");
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(2, 46, AP_SSID);
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(2, 65, "Password:");
  display.setFont(u8g2_font_7x14B_tf);
  display.drawStr(15, 83, apPassword.c_str());
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(13, 104, "192.168.4.1");
  uint32_t secondsLeft = AP_TIMEOUT_MS > millis() - portalStartedMs ? (AP_TIMEOUT_MS - (millis() - portalStartedMs)) / 1000 : 0;
  String footer = String("closes in ") + secondsLeft / 60 + "m";
  display.drawStr(28, 123, footer.c_str());
  display.sendBuffer();
}

void drawButtonOverlay(uint32_t heldMs) {
  if (!oledReady) return;
  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  if (resetConfirmation && heldMs >= RESET_CONFIRM_HOLD_MS) drawCenteredStr("RELEASE TO RESET", 50);
  else if (resetConfirmation) drawCenteredStr("HOLD 3S TO RESET", 50);
  else if (heldMs >= 8000) drawCenteredStr("RELEASE: RESET?", 50);
  else if (heldMs >= 3000) drawCenteredStr("RELEASE: SETUP", 50);
  else drawCenteredStr("HOLDING...", 50);
  display.setFont(u8g2_font_logisoso24_tn);
  char seconds[12];
  snprintf(seconds, sizeof(seconds), "%lu.%lus", static_cast<unsigned long>(heldMs / 1000UL),
           static_cast<unsigned long>((heldMs % 1000UL) / 100UL));
  drawCenteredStr(seconds, 91);
  display.sendBuffer();
}

void drawResetConfirmation() {
  if (!oledReady) return;
  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  drawCenteredStr("RESET SETTINGS?", 28);
  display.setFont(u8g2_font_6x10_tf);
  drawCenteredStr("Within 5 seconds:", 55);
  drawCenteredStr("Hold BOOT 3s", 74);
  uint32_t left = RESET_CONFIRM_WINDOW_MS > millis() - resetConfirmStartedMs ? (RESET_CONFIRM_WINDOW_MS - (millis() - resetConfirmStartedMs) + 999) / 1000 : 0;
  String countdown = String(left) + " sec";
  drawCenteredStr(countdown.c_str(), 105);
  display.sendBuffer();
}

void drawThermalSafeScreen() {
  if (!oledReady) return;
  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  drawCenteredStr(thermalSafeModeFromSensorFault ? "! TEMP SENSOR !" : "! OVERHEAT !", 27);
  char temperature[10];
  if (isnan(chipTemperatureC)) snprintf(temperature, sizeof(temperature), "--C");
  else snprintf(temperature, sizeof(temperature), "%dC", static_cast<int>(chipTemperatureC + 0.5f));
  drawCenteredStr(temperature, 68);
  display.setFont(u8g2_font_6x10_tf);
  drawCenteredStr("CPU 80MHz / WIFI OFF", 91);
  drawCenteredStr(thermalSafeModeFromSensorFault ? "SENSOR RETRY ACTIVE" : "AUTOMATIC COOLING", 108);
  drawCenteredStr(thermalSafeModeFromSensorFault ? "UNPLUG IF HOT" : "UNPLUG IF NOT COOL", 123);
  display.sendBuffer();
}

void drawUpdateScreen() {
  if (!oledReady) return;
  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  if (updateState == UpdateState::AVAILABLE && updatePromptVisible) {
    drawCenteredStr("UPDATE AVAILABLE", 25);
    display.setFont(u8g2_font_6x10_tf);
    String versions = String(FIRMWARE_VERSION) + " -> " + latestFirmwareVersion;
    drawCenteredStr(versions.c_str(), 52);
    drawCenteredStr("Tap BOOT: install", 78);
    uint32_t left = UPDATE_PROMPT_MS > millis() - updatePromptStartedMs
                      ? (UPDATE_PROMPT_MS - (millis() - updatePromptStartedMs) + 999) / 1000 : 0;
    String footer = String("Later in ") + left + " sec";
    drawCenteredStr(footer.c_str(), 106);
  } else if (updateState == UpdateState::DOWNLOADING) {
    drawCenteredStr("DOWNLOADING", 25);
    // The numeric-only _tn font omits symbols such as '%'. Use the full font
    // so the complete progress label is rendered and centered as one string.
    display.setFont(u8g2_font_logisoso28_tf);
    uint32_t percent = latestFirmwareSize > 0
                         ? static_cast<uint32_t>((updateDownloadedBytes * 100ULL) / latestFirmwareSize) : 0;
    String value = String(percent) + "%";
    drawCenteredStr(value.c_str(), 70);
    display.drawFrame(8, 91, 112, 10);
    const uint32_t progressWidth = percent >= 100 ? 108 : percent * 108 / 100;
    display.drawBox(10, 93, static_cast<uint8_t>(progressWidth), 6);
    display.setFont(u8g2_font_5x8_tf);
    drawCenteredStr("DO NOT POWER OFF", 121);
  } else if (updateState == UpdateState::VERIFYING) {
    drawCenteredStr("VERIFYING", 38);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("SHA-256 + image", 70);
    drawCenteredStr("Please wait...", 96);
  } else if (updateState == UpdateState::READY_TO_REBOOT) {
    drawCenteredStr("UPDATE READY", 40);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("Rebooting into", 72);
    String version = String("version ") + latestFirmwareVersion;
    drawCenteredStr(version.c_str(), 94);
  } else if (updateState == UpdateState::ERROR_STATE) {
    drawCenteredStr("UPDATE ERROR", 27);
    display.setFont(u8g2_font_5x8_tf);
    String line = updateError;
    if (line.length() > 23) line = line.substring(0, 23);
    drawCenteredStr(line.c_str(), 62);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("AUTO RETRY SCHEDULED", 94);
    drawCenteredStr("or next reboot", 116);
  }
  display.sendBuffer();
}

void drawMainScreen() {
  if (!oledReady || displaySleeping) return;
  if (thermalSafeMode) {
    drawThermalSafeScreen();
    return;
  }
  if (bootSplashActive()) {
    drawBootSplashFrame();
    return;
  }
  if (firmwareTransferActive() || updateState == UpdateState::ERROR_STATE) {
    drawUpdateScreen();
    return;
  }
  if (updateState == UpdateState::AVAILABLE && updatePromptVisible) {
    drawUpdateScreen();
    return;
  }
  if (portalActive) {
    drawPortalScreen();
    return;
  }
  if (resetConfirmation) {
    drawResetConfirmation();
    return;
  }
  int8_t ox, oy;
  getBurninOffset(ox, oy);
  display.clearBuffer();
  switch (currentView) {
    case View::DDAY_TIME: drawDdayView(false, ox, oy); break;
    case View::DDAY_MESSAGE: drawDdayView(true, ox, oy); break;
    case View::MESSAGE_ONLY: drawMessageOnly(ox, oy); break;
    case View::CLOCK_ONLY: drawClockOnly(ox, oy); break;
    case View::MESSAGE_CLOCK: drawMessageClock(ox, oy); break;
    case View::DASHBOARD: drawDashboard(ox, oy); break;
    case View::DEVICE_INFO: drawDeviceInfo(ox, oy); break;
    case View::CUSTOM_MEDIA:
      if (!copyCurrentMediaFrameToDisplayBuffer()) {
        display.setFont(u8g2_font_7x14B_tf);
        drawCenteredStr("NO MEDIA", 54);
        display.setFont(u8g2_font_6x10_tf);
        drawCenteredStr("OPEN SETUP TO ADD", 82);
      }
      break;
  }
  display.sendBuffer();
}
