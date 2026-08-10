String savedWifiSsidKey(uint8_t index, uint8_t bank = 0) {
  return bank == 0 ? String("wifi_ssid") + String(index) : String("wfb_ssid") + String(index);
}

String savedWifiPassKey(uint8_t index, uint8_t bank = 0) {
  return bank == 0 ? String("wifi_pass") + String(index) : String("wfb_pass") + String(index);
}

String savedWifiSecurityKey(uint8_t index, uint8_t bank = 0) {
  return bank == 0 ? String("wifi_sec") + String(index) : String("wfb_sec") + String(index);
}

String savedWifiUsernameKey(uint8_t index, uint8_t bank = 0) {
  return bank == 0 ? String("wifi_user") + String(index) : String("wfb_user") + String(index);
}

String savedWifiIdentityKey(uint8_t index, uint8_t bank = 0) {
  return bank == 0 ? String("wifi_id") + String(index) : String("wfb_id") + String(index);
}

const char *savedWifiCountKey(uint8_t bank) {
  return bank == 0 ? "wifi_count" : "wfb_count";
}

uint8_t activeWifiBank() {
  if (!prefsReady) return 0;
  const uint8_t bank = prefs.getUChar("wifi_bank", 0);
  return bank <= 1 ? bank : 0;
}

const char *wifiSecurityName(WifiSecurityType security) {
  switch (security) {
    case WifiSecurityType::ENTERPRISE_PEAP: return "enterprise_peap";
    case WifiSecurityType::PERSONAL:
    default: return "personal";
  }
}

bool enterpriseWifiSupported() {
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
  return true;
#else
  return false;
#endif
}

bool wifiAuthIsEnterprise(wifi_auth_mode_t auth) {
  return auth == WIFI_AUTH_ENTERPRISE || auth == WIFI_AUTH_WPA2_ENTERPRISE ||
         auth == WIFI_AUTH_WPA3_ENTERPRISE || auth == WIFI_AUTH_WPA2_WPA3_ENTERPRISE ||
         auth == WIFI_AUTH_WPA_ENTERPRISE || auth == WIFI_AUTH_WPA3_ENT_192;
}

bool wifiAuthRequiresEnterpriseCa(wifi_auth_mode_t auth) {
  return auth == WIFI_AUTH_WPA3_ENTERPRISE || auth == WIFI_AUTH_WPA2_WPA3_ENTERPRISE ||
         auth == WIFI_AUTH_WPA3_ENT_192;
}

int findSavedNetwork(const String &ssid) {
  for (uint8_t i = 0; i < config.savedNetworkCount; ++i) {
    if (config.savedNetworks[i].ssid == ssid) return i;
  }
  return -1;
}

bool putStringVerified(const char *key, const String &value) {
  if (!prefsReady) return false;
  prefs.putString(key, value);
  return prefs.isKey(key) && prefs.getString(key, "") == value;
}

bool removePreferenceVerified(const char *key) {
  if (!prefsReady) return false;
  return !prefs.isKey(key) || prefs.remove(key);
}

bool writeWifiBank(uint8_t bank) {
  if (!prefsReady || bank > 1) return false;
  bool success = true;
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; ++i) {
    const String ssidKey = savedWifiSsidKey(i, bank);
    const String passKey = savedWifiPassKey(i, bank);
    const String securityKey = savedWifiSecurityKey(i, bank);
    const String usernameKey = savedWifiUsernameKey(i, bank);
    const String identityKey = savedWifiIdentityKey(i, bank);
    if (i < config.savedNetworkCount) {
      if (!putStringVerified(ssidKey.c_str(), config.savedNetworks[i].ssid)) success = false;
      if (!putStringVerified(passKey.c_str(), config.savedNetworks[i].password)) success = false;
      if (config.savedNetworks[i].security == WifiSecurityType::ENTERPRISE_PEAP) {
        const uint8_t security = static_cast<uint8_t>(WifiSecurityType::ENTERPRISE_PEAP);
        if (prefs.putUChar(securityKey.c_str(), security) != sizeof(uint8_t) ||
            prefs.getUChar(securityKey.c_str(), 0xFF) != security) success = false;
        if (!putStringVerified(usernameKey.c_str(), config.savedNetworks[i].username)) success = false;
        if (!putStringVerified(identityKey.c_str(), config.savedNetworks[i].identity)) success = false;
      } else {
        if (!removePreferenceVerified(securityKey.c_str())) success = false;
        if (!removePreferenceVerified(usernameKey.c_str())) success = false;
        if (!removePreferenceVerified(identityKey.c_str())) success = false;
      }
    } else {
      if (!removePreferenceVerified(ssidKey.c_str())) success = false;
      if (!removePreferenceVerified(passKey.c_str())) success = false;
      if (!removePreferenceVerified(securityKey.c_str())) success = false;
      if (!removePreferenceVerified(usernameKey.c_str())) success = false;
      if (!removePreferenceVerified(identityKey.c_str())) success = false;
    }
  }
  if (!success) return false;
  const char *countKey = savedWifiCountKey(bank);
  return prefs.putUChar(countKey, config.savedNetworkCount) == sizeof(uint8_t) &&
         prefs.getUChar(countKey, 0xFF) == config.savedNetworkCount;
}

bool saveWifiNetworks() {
  if (!prefsReady) return false;
  // Bank A is the exact legacy wifi_ssidN/wifi_passN layout. New writes go to
  // the inactive bank and are fully verified before the one-byte bank selector
  // is changed. A power loss can therefore leave only the old or the new full
  // list active, never a partially reordered list.
  const uint8_t previousBank = activeWifiBank();
  const uint8_t nextBank = previousBank ^ 1U;
  if (!writeWifiBank(nextBank)) return false;
  if (prefs.putUChar("wifi_bank", nextBank) != sizeof(uint8_t)) return false;
  return prefs.getUChar("wifi_bank", 0xFF) == nextBank;
}

void removeSavedNetworkAt(uint8_t index) {
  if (index >= config.savedNetworkCount) return;
  for (uint8_t i = index; i + 1 < config.savedNetworkCount; ++i) {
    config.savedNetworks[i] = config.savedNetworks[i + 1];
  }
  --config.savedNetworkCount;
  config.savedNetworks[config.savedNetworkCount] = SavedNetwork();
}

bool saveWifiOrRestore(const Config &previous, uint8_t previousActiveWifiIndex, const char *failure) {
  if (saveWifiNetworks()) return true;
  config = previous;
  activeWifiIndex = previousActiveWifiIndex;
  logLine(failure);
  return false;
}

bool upsertSavedNetwork(const String &ssid, const String &password, WifiSecurityType security,
                        const String &username, const String &identity) {
  const Config previous = config;
  const uint8_t previousActiveWifiIndex = activeWifiIndex;
  int existing = findSavedNetwork(ssid);
  SavedNetwork network;
  network.ssid = ssid;
  network.password = password;
  network.security = security;
  network.username = username;
  network.identity = identity;
  if (existing >= 0) removeSavedNetworkAt(static_cast<uint8_t>(existing));
  uint8_t newCount = config.savedNetworkCount < MAX_SAVED_NETWORKS
                       ? config.savedNetworkCount + 1
                       : MAX_SAVED_NETWORKS;
  for (uint8_t i = newCount - 1; i > 0; --i) {
    config.savedNetworks[i] = config.savedNetworks[i - 1];
  }
  config.savedNetworks[0] = network;
  config.savedNetworkCount = newCount;
  activeWifiIndex = 0;
  return saveWifiOrRestore(previous, previousActiveWifiIndex, "saved Wi-Fi update aborted; previous bank retained");
}

bool promoteSavedNetwork(uint8_t index) {
  if (index == NO_WIFI_INDEX || index >= config.savedNetworkCount || index == 0) return true;
  const Config previous = config;
  const uint8_t previousActiveWifiIndex = activeWifiIndex;
  SavedNetwork network = config.savedNetworks[index];
  for (uint8_t i = index; i > 0; --i) {
    config.savedNetworks[i] = config.savedNetworks[i - 1];
  }
  config.savedNetworks[0] = network;
  activeWifiIndex = 0;
  return saveWifiOrRestore(previous, previousActiveWifiIndex, "preferred Wi-Fi update aborted; previous bank retained");
}

bool saveGeneralConfig() {
  if (!prefsReady) return false;
  bool success = true;
  if (prefs.putUChar("mode", static_cast<uint8_t>(config.mode)) != sizeof(uint8_t)) success = false;
  if (prefs.putUChar("last_view", static_cast<uint8_t>(config.lastView)) != sizeof(uint8_t)) success = false;
  if (!putStringVerified("title", config.title)) success = false;
  if (!putStringVerified("target", config.target)) success = false;
  if (!putStringVerified("message", config.message)) success = false;
  if (prefs.putBool("dday_text", config.ddayTextStyle) != sizeof(bool)) success = false;
  if (prefs.putBool("after_done", config.afterComplete) != sizeof(bool)) success = false;
  if (prefs.putBool("msg_left", config.messageLeft) != sizeof(bool)) success = false;
  if (prefs.putBool("msg_scroll", config.messageScroll) != sizeof(bool)) success = false;
  if (prefs.putUChar("scroll_spd", config.scrollSpeed) != sizeof(uint8_t)) success = false;
  if (prefs.putBool("hour24", config.hour24) != sizeof(bool)) success = false;
  if (prefs.putBool("seconds", config.showSeconds) != sizeof(bool)) success = false;
  if (prefs.putBool("show_temp", config.showChipTemperature) != sizeof(bool)) success = false;
  if (prefs.putBool("boot_sync", config.bootSync) != sizeof(bool)) success = false;
  if (prefs.putUInt("ntp_sec", config.ntpPeriodSec) != sizeof(uint32_t)) success = false;
  if (prefs.putUInt("dday_sec", config.ddayPeriodSec) != sizeof(uint32_t)) success = false;
  if (prefs.putUInt("retry_sec", config.retryPeriodSec) != sizeof(uint32_t)) success = false;
  if (prefs.putBool("wifi_sleep", config.wifiSleep) != sizeof(bool)) success = false;
  if (prefs.putUChar("bright", config.brightness) != sizeof(uint8_t)) success = false;
  if (prefs.putUChar("night_lvl", config.nightLevel) != sizeof(uint8_t)) success = false;
  if (prefs.putBool("led_en", config.ledEnabled) != sizeof(bool)) success = false;
  if (prefs.putUChar("led_lvl", config.ledBrightness) != sizeof(uint8_t)) success = false;
  if (prefs.putUChar("led_night", config.ledNightLevel) != sizeof(uint8_t)) success = false;
  if (prefs.putUShort("night_start", config.nightStartMin) != sizeof(uint16_t)) success = false;
  if (prefs.putUShort("night_end", config.nightEndMin) != sizeof(uint16_t)) success = false;
  if (prefs.putBool("burnin", config.burninShift) != sizeof(bool)) success = false;
  if (prefs.putUShort("screen_off", config.screenOffMin) != sizeof(uint16_t)) success = false;
  if (prefs.putUChar("cycle_mask", config.cycleMask) != sizeof(uint8_t)) success = false;
  if (!putStringVerified("cycle_ord", cycleOrderToString(config))) success = false;
  if (prefs.putUChar("cycle_int", config.cycleIntervalSec) != sizeof(uint8_t)) success = false;
  if (prefs.putUChar("cycle_idx", config.cycleIndex) != sizeof(uint8_t)) success = false;
  if (prefs.putULong64("last_sync", config.lastSync) != sizeof(uint64_t)) success = false;
  if (prefs.putInt("last_dday", config.lastDday) != sizeof(int32_t)) success = false;
  // Commit the schema marker last so an interrupted migration is retried safely.
  if (!success) return false;
  return prefs.putUShort("cfg_ver", config.version) == sizeof(uint16_t);
}

bool saveViewState() {
  if (!prefsReady) return false;
  const bool viewSaved = prefs.putUChar("last_view", static_cast<uint8_t>(config.lastView)) == sizeof(uint8_t);
  const bool cycleSaved = prefs.putUChar("cycle_idx", config.cycleIndex) == sizeof(uint8_t);
  return viewSaved && cycleSaved;
}

void scheduleViewStateSave() {
  viewSavePending = true;
  viewSaveDueMs = millis() + VIEW_SAVE_DELAY_MS;
}

void processViewStateSave() {
  if (!viewSavePending || buttonRawPressed || buttonStablePressed || !deadlineReached(millis(), viewSaveDueMs)) return;
  if (firmwareTransferActive()) return;
  if (saveViewState()) {
    viewSavePending = false;
  } else {
    viewSaveDueMs = millis() + VIEW_SAVE_DELAY_MS;
    logLine("view state save failed; retry scheduled");
  }
}

bool loadConfig() {
  config = Config();
  if (!prefsReady) return false;
  if (!prefs.isKey("cfg_ver")) return false;
  uint16_t version = prefs.getUShort("cfg_ver", 0);
  if (version < 1 || version > CONFIG_VERSION) {
    logLine("unknown config schema; defaults restored");
    return false;
  }
  const bool migrateV1 = version == 1;
  const bool migrateToV3 = version < 3;
  const bool migrateToV4 = version < 4;
  const bool migrateToV5 = version < 5;
  const bool migrateToV6 = version < 6;
  const bool migrateToV7 = version < 7;
  const bool migrateToV8 = version < 8;
  config.version = CONFIG_VERSION;
  if (migrateToV4) {
    String legacySsid = prefs.getString("wifi_ssid", "");
    String legacyPass = prefs.getString("wifi_pass", "");
    if (legacySsid.length() > 0 && legacySsid.length() <= 32 && legacyPass.length() <= 63) {
      config.savedNetworks[0].ssid = legacySsid;
      config.savedNetworks[0].password = legacyPass;
      config.savedNetworkCount = 1;
    }
  } else {
    const uint8_t storedBank = migrateToV8 ? 0 : activeWifiBank();
    uint8_t storedCount = prefs.getUChar(savedWifiCountKey(storedBank), 0);
    if (storedCount > MAX_SAVED_NETWORKS) storedCount = MAX_SAVED_NETWORKS;
    for (uint8_t i = 0; i < storedCount; ++i) {
      String ssid = prefs.getString(savedWifiSsidKey(i, storedBank).c_str(), "");
      String password = prefs.getString(savedWifiPassKey(i, storedBank).c_str(), "");
      WifiSecurityType security = WifiSecurityType::PERSONAL;
      String username;
      String identity;
      if (!migrateToV7) {
        const uint8_t storedSecurity = prefs.getUChar(savedWifiSecurityKey(i, storedBank).c_str(), 0);
        if (storedSecurity == static_cast<uint8_t>(WifiSecurityType::ENTERPRISE_PEAP)) {
          security = WifiSecurityType::ENTERPRISE_PEAP;
          username = prefs.getString(savedWifiUsernameKey(i, storedBank).c_str(), "");
          identity = prefs.getString(savedWifiIdentityKey(i, storedBank).c_str(), "");
        }
      }
      if (ssid.length() == 0 || ssid.length() > 32) continue;
      if (security == WifiSecurityType::PERSONAL) {
        if (password.length() > 63) continue;
      } else {
        if (password.length() == 0 || password.length() > 64 || username.length() == 0 ||
            username.length() > 64 || identity.length() > 64) continue;
      }
      SavedNetwork &network = config.savedNetworks[config.savedNetworkCount];
      network.ssid = ssid;
      network.password = password;
      network.security = security;
      network.username = username;
      network.identity = identity;
      ++config.savedNetworkCount;
    }
  }
  uint8_t storedMode = prefs.getUChar("mode", 0);
  if (migrateV1 && storedMode == 4) storedMode = static_cast<uint8_t>(TopMode::SELECTED_CYCLE);
  config.mode = static_cast<TopMode>(clampInt(storedMode, 0, 7));
  const uint8_t maximumStoredView = migrateV1 ? 3 : (migrateToV6 ? 5 : 6);
  config.lastView = static_cast<View>(clampInt(prefs.getUChar("last_view", 0), 0, maximumStoredView));
  config.title = prefs.getString("title", config.title);
  config.target = prefs.getString("target", config.target);
  config.message = prefs.getString("message", config.message);
  if (config.title.length() == 0 || config.title.length() > 96 || utf8Codepoints(config.title) > 24) {
    config.title = "2027 수능";
  }
  if (config.message.length() > 240 || utf8Codepoints(config.message) > 60) {
    config.message = "오늘도 한 칸 앞으로";
  }
  config.ddayTextStyle = prefs.getBool("dday_text", false);
  config.afterComplete = prefs.getBool("after_done", false);
  config.messageLeft = prefs.getBool("msg_left", false);
  config.messageScroll = prefs.getBool("msg_scroll", true);
  config.scrollSpeed = clampInt(prefs.getUChar("scroll_spd", 24), 5, 80);
  config.hour24 = prefs.getBool("hour24", true);
  config.showSeconds = prefs.getBool("seconds", false);
  config.showChipTemperature = prefs.getBool("show_temp", true);
  config.bootSync = prefs.getBool("boot_sync", true);
  config.ntpPeriodSec = prefs.getUInt("ntp_sec", 21600);
  if (!allowedValue(config.ntpPeriodSec, ALLOWED_NTP_PERIODS)) config.ntpPeriodSec = 21600;
  config.ddayPeriodSec = prefs.getUInt("dday_sec", 0);
  if (!allowedValue(config.ddayPeriodSec, ALLOWED_DDAY_PERIODS)) config.ddayPeriodSec = 0;
  config.retryPeriodSec = prefs.getUInt("retry_sec", 300);
  if (!allowedValue(config.retryPeriodSec, ALLOWED_RETRY_PERIODS)) config.retryPeriodSec = 300;
  config.wifiSleep = prefs.getBool("wifi_sleep", false);
  config.brightness = clampInt(prefs.getUChar("bright", 180), 1, 255);
  config.nightLevel = clampInt(prefs.getUChar("night_lvl", 45), 1, 255);
  config.ledEnabled = prefs.getBool("led_en", true);
  config.ledBrightness = clampInt(prefs.getUChar("led_lvl", 24), 1, 64);
  config.ledNightLevel = clampInt(prefs.getUChar("led_night", 6), 1, 32);
  config.nightStartMin = clampInt(prefs.getUShort("night_start", 1320), 0, 1439);
  config.nightEndMin = clampInt(prefs.getUShort("night_end", 420), 0, 1439);
  config.burninShift = prefs.getBool("burnin", true);
  config.screenOffMin = clampInt(prefs.getUShort("screen_off", 0), 0, 1440);
  const uint8_t storedCycleLimit = migrateToV6 ? 0x3F : 0x7F;
  config.cycleMask = prefs.getUChar("cycle_mask", migrateV1 ? 0x0F : storedCycleLimit) & storedCycleLimit;
  if (config.cycleMask == 0) config.cycleMask = 1;
  if (migrateV1) {
    uint8_t legacyOrder[4] = {0, 1, 2, 3};
    uint8_t parsedLegacy[4];
    if (parseCycleOrderCount(prefs.getString("cycle_ord", "0,1,2,3"), parsedLegacy, 4)) {
      memcpy(legacyOrder, parsedLegacy, sizeof(parsedLegacy));
    }
    memcpy(config.cycleOrder, legacyOrder, sizeof(legacyOrder));
    config.cycleOrder[4] = 4;
    config.cycleOrder[5] = 5;
    config.cycleOrder[6] = 6;
  } else if (migrateToV6) {
    uint8_t parsedLegacy[6];
    if (parseCycleOrderCount(prefs.getString("cycle_ord", "0,1,2,3,4,5"), parsedLegacy, 6)) {
      memcpy(config.cycleOrder, parsedLegacy, sizeof(parsedLegacy));
    }
    config.cycleOrder[6] = 6;
  } else {
    uint8_t parsed[VIEW_COUNT];
    if (parseCycleOrderCount(prefs.getString("cycle_ord", "0,1,2,3,4,5,6"), parsed, VIEW_COUNT)) {
      memcpy(config.cycleOrder, parsed, sizeof(parsed));
    }
  }
  uint8_t interval = prefs.getUChar("cycle_int", 8);
  config.cycleIntervalSec = (interval == 0 || (interval >= 3 && interval <= 60)) ? interval : 8;
  config.cycleIndex = prefs.getUChar("cycle_idx", 0) % VIEW_COUNT;
  config.lastSync = prefs.getULong64("last_sync", 0);
  config.lastDday = prefs.getInt("last_dday", 0);
  int y, m, d;
  if (!parseDate(config.target, y, m, d)) config.target = "2026-11-19";
  if (migrateToV3 || migrateToV4 || migrateToV5 || migrateToV6 || migrateToV7 || migrateToV8) {
    if (saveGeneralConfig()) {
      logLine(String("configuration migrated from schema ") + String(version) + " to " + String(CONFIG_VERSION));
    } else {
      logLine("configuration migration could not be persisted");
    }
  }
  return true;
}

