#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <Adafruit_NeoPixel.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <esp_timer.h>
#include <time.h>
#include "driver/temperature_sensor.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "esp_system.h"

#include "PortalPage.h"
#include "UpdateCertificates.h"

// TLS, HTTP parsing, hashing, display updates, and the Arduino framework all
// share loopTask during a synchronous OTA transfer. Reserve an explicit stack
// instead of relying on the board package default.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// CYTRON//MILESTONE — MILESTONE Core
// Target: Waveshare ESP32-S3-Zero / ESP32-S3 Zero
// OLED: SH1107 128x128 I2C, verified at 0x3C

namespace Milestone {

constexpr char FIRMWARE_VERSION[] = "1.5.7";
constexpr char AP_SSID[] = "MILESTONE-D1-SETUP";
constexpr char HOSTNAME[] = "milestone-d1";
constexpr char PREFS_NS[] = "milestone";
constexpr char UPDATE_MANIFEST_URL[] = "https://github.com/CXITRON/MILESTONE-Core/releases/latest/download/MILESTONE_Core.json";
constexpr char UPDATE_RELEASE_BASE_URL[] = "https://github.com/CXITRON/MILESTONE-Core/releases/download/v";
constexpr char UPDATE_ASSET_NAME[] = "MILESTONE_Core.bin";
constexpr char UPDATE_GITHUB_HOST[] = "github.com";
constexpr uint16_t CONFIG_VERSION = 6;
constexpr uint8_t VIEW_COUNT = 7;
constexpr uint8_t MAX_SAVED_NETWORKS = 8;
constexpr uint8_t NO_WIFI_INDEX = 0xFF;

constexpr uint8_t PIN_SDA = 8;
constexpr uint8_t PIN_SCL = 9;
constexpr uint8_t PIN_BOOT = 0;
constexpr uint8_t PIN_RGB_LED = 21;
constexpr uint8_t OLED_ADDR_PRIMARY = 0x3C;
constexpr uint8_t OLED_ADDR_SECONDARY = 0x3D;

constexpr uint32_t AP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20UL * 1000UL;
constexpr uint32_t NTP_TIMEOUT_MS = 18UL * 1000UL;
constexpr uint32_t PORTAL_SUCCESS_HOLD_MS = 3000UL;
constexpr uint32_t DISPLAY_REFRESH_MS = 250UL;
constexpr uint32_t BUTTON_DISPLAY_REFRESH_MS = 250UL;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30UL;
constexpr uint32_t VIEW_SAVE_DELAY_MS = 1500UL;
constexpr uint32_t RESET_CONFIRM_WINDOW_MS = 5000UL;
constexpr uint32_t RESET_CONFIRM_HOLD_MS = 3000UL;
constexpr uint32_t LED_REFRESH_MS = 20UL;
constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 12UL * 1000UL;
constexpr uint32_t TEMPERATURE_REFRESH_MS = 5UL * 1000UL;
constexpr uint32_t THERMAL_CRITICAL_HOLD_MS = 10UL * 1000UL;
constexpr uint32_t THERMAL_THROTTLE_RECOVERY_HOLD_MS = 30UL * 1000UL;
constexpr uint32_t THERMAL_RECOVERY_HOLD_MS = 60UL * 1000UL;
constexpr uint32_t TEMPERATURE_FAULT_SAFE_HOLD_MS = 60UL * 1000UL;
constexpr uint32_t UPDATE_PROMPT_MS = 15UL * 1000UL;
constexpr uint32_t UPDATE_CURRENT_HOLD_MS = 1000UL;
constexpr uint32_t BOOT_SPLASH_MS = 3000UL;
constexpr uint32_t DEVICE_INFO_PAGE_MS = 5000UL;
constexpr uint8_t DEVICE_INFO_PAGE_COUNT = 5;
constexpr uint32_t UPDATE_WEEKLY_SEC = 7UL * 24UL * 60UL * 60UL;
constexpr uint32_t UPDATE_RETRY_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t UPDATE_HTTP_TIMEOUT_MS = 15UL * 1000UL;
constexpr uint32_t UPDATE_DOWNLOAD_STALL_MS = 20UL * 1000UL;
constexpr uint32_t UPDATE_INSTALL_RESPONSE_HOLD_MS = 500UL;
constexpr uint32_t OTA_BOOT_CONFIRM_MS = 10UL * 1000UL;
constexpr size_t UPDATE_MANIFEST_MAX_BYTES = 2048;
constexpr size_t UPDATE_DOWNLOAD_BUFFER_BYTES = 2048;
constexpr uint32_t UPDATE_MIN_FREE_HEAP = 55000;
constexpr uint32_t UPDATE_MIN_LARGEST_BLOCK = 32768;
constexpr uint32_t ALLOWED_NTP_PERIODS[] = {0, 3600, 10800, 21600, 43200, 86400};
constexpr uint32_t ALLOWED_DDAY_PERIODS[] = {0, 60, 600, 1800, 3600};
constexpr uint32_t ALLOWED_RETRY_PERIODS[] = {60, 300, 900, 1800};
constexpr float THERMAL_WARNING_C = 70.0f;
constexpr float THERMAL_WARNING_CLEAR_C = 65.0f;
constexpr float THERMAL_THROTTLE_C = 80.0f;
constexpr float THERMAL_CRITICAL_C = 90.0f;
constexpr float THERMAL_THROTTLE_RECOVERY_C = 75.0f;
constexpr float THERMAL_SAFE_RECOVERY_C = 70.0f;
constexpr float TEMPERATURE_HIGH_RANGE_ENTER_C = 85.0f;
constexpr float TEMPERATURE_HIGH_RANGE_EXIT_C = 65.0f;
constexpr uint32_t THERMAL_THROTTLE_CPU_MHZ = 80;
constexpr uint8_t TEMPERATURE_FAULT_SAMPLE_COUNT = 3;
constexpr time_t MIN_VALID_EPOCH = 1704067200;  // 2024-01-01 UTC
constexpr char TZ_INFO[] = "KST-9";

enum class View : uint8_t {
  DDAY_TIME = 0,
  DDAY_MESSAGE = 1,
  MESSAGE_ONLY = 2,
  CLOCK_ONLY = 3,
  MESSAGE_CLOCK = 4,
  DASHBOARD = 5,
  DEVICE_INFO = 6
};

enum class TopMode : uint8_t {
  DDAY_TIME = 0,
  DDAY_MESSAGE = 1,
  MESSAGE_ONLY = 2,
  CLOCK_ONLY = 3,
  MESSAGE_CLOCK = 4,
  DASHBOARD = 5,
  SELECTED_CYCLE = 6,
  DEVICE_INFO = 7
};

enum class RuntimeState : uint8_t {
  BOOTING,
  UNPROVISIONED,
  SETUP_AP,
  CONNECTING,
  TIME_SYNCING,
  RUNNING_ONLINE,
  RUNNING_OFFLINE,
  WIFI_SLEEP,
  ERROR_DISPLAY
};

enum class WifiTestState : uint8_t {
  IDLE,
  CONNECTING,
  TIME_SYNCING,
  SUCCESS,
  FAILED
};

enum class UpdateState : uint8_t {
  IDLE,
  CHECKING,
  AVAILABLE,
  DOWNLOADING,
  VERIFYING,
  READY_TO_REBOOT,
  CURRENT,
  ERROR_STATE
};

enum class UpdateCheckReason : uint8_t {
  NONE,
  BOOT,
  WEEKLY,
  MANUAL
};

enum class LedState : uint8_t {
  BOOTING,
  SETUP,
  CONNECTING,
  TIME_SYNCING,
  ONLINE,
  WIFI_SLEEP,
  WIFI_ERROR,
  NTP_ERROR,
  DISPLAY_ERROR,
  THERMAL_WARNING,
  TEMPERATURE_SENSOR_ERROR,
  THERMAL_CRITICAL,
  UPDATE_CHECKING,
  UPDATE_AVAILABLE,
  UPDATE_DOWNLOADING,
  UPDATE_ERROR,
  BUTTON_HOLD,
  RESET_WARNING
};

struct SavedNetwork {
  String ssid;
  String password;
};

struct Config {
  uint16_t version = CONFIG_VERSION;
  SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
  uint8_t savedNetworkCount = 0;
  TopMode mode = TopMode::DDAY_TIME;
  View lastView = View::DDAY_TIME;
  String title = "2027 수능";
  String target = "2026-11-19";
  String message = "오늘도 한 칸 앞으로";
  bool ddayTextStyle = false;
  bool afterComplete = false;
  bool messageLeft = false;
  bool messageScroll = true;
  uint8_t scrollSpeed = 24;
  bool hour24 = true;
  bool showSeconds = false;
  bool showChipTemperature = true;
  bool bootSync = true;
  uint32_t ntpPeriodSec = 21600;
  uint32_t ddayPeriodSec = 0;  // 0 = recalculate at local midnight.
  uint32_t retryPeriodSec = 300;
  bool wifiSleep = false;
  uint8_t brightness = 180;
  uint8_t nightLevel = 45;
  bool ledEnabled = true;
  uint8_t ledBrightness = 24;
  uint8_t ledNightLevel = 6;
  uint16_t nightStartMin = 1320;  // 22:00
  uint16_t nightEndMin = 420;     // 07:00
  bool burninShift = true;
  uint16_t screenOffMin = 0;
  uint8_t cycleMask = 0x7F;
  uint8_t cycleOrder[VIEW_COUNT] = {0, 1, 2, 3, 4, 5, 6};
  uint8_t cycleIntervalSec = 8;
  uint8_t cycleIndex = 0;
  uint64_t lastSync = 0;
  int32_t lastDday = 0;
};

Preferences prefs;
Config config;
// This generic 1.5-inch module uses SH1107 column offset 0.  The plain
// SH1107_128X128 profile applies a 96-pixel offset and wraps the leftmost
// 32 pixels onto the right edge on this panel.
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
Adafruit_NeoPixel statusLed(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
WebServer server(80);
DNSServer dnsServer;

RuntimeState runtimeState = RuntimeState::BOOTING;
WifiTestState wifiTestState = WifiTestState::IDLE;
String wifiTestError;
String pendingSsid;
String pendingPass;
String apPassword;
String sessionToken;
View currentView = View::DDAY_TIME;
uint8_t currentCycleIndex = 0;
uint8_t oledAddress = 0;
bool oledReady = false;
bool portalActive = false;
bool mdnsActive = false;
bool ntpRequestActive = false;
bool displaySleeping = false;
bool portalClosingAfterSuccess = false;
bool initialStationAttempt = true;
bool internetVerified = false;
bool ntpFailed = false;
volatile bool ntpSyncEvent = false;
bool savedWifiScanActive = false;
bool savedWifiScanCompleted = false;
bool portalWifiScanActive = false;
bool savedWifiPreserveAp = false;
uint8_t activeWifiIndex = NO_WIFI_INDEX;
uint8_t wifiCandidateOrder[MAX_SAVED_NETWORKS] = {};
uint8_t wifiCandidateCount = 0;
uint8_t wifiCandidatePosition = 0;

UpdateState updateState = UpdateState::IDLE;
UpdateCheckReason pendingUpdateCheckReason = UpdateCheckReason::NONE;
String latestFirmwareVersion;
String latestFirmwareSha256;
String latestFirmwareNotes;
String updateError;
String lastOtaResult;
uint32_t latestFirmwareSize = 0;
uint32_t updateDownloadedBytes = 0;
uint32_t updatePromptStartedMs = 0;
uint32_t updateStateStartedMs = 0;
uint32_t updateCurrentVisibleMs = 0;
uint32_t nextUpdateRetryMs = 0;
uint64_t lastUpdateCheckEpoch = 0;
bool bootUpdateCheckPending = true;
bool updateCheckAfterNetworkReady = false;
bool updatePromptVisible = false;
bool updateInstallRequested = false;
uint32_t updateInstallNotBeforeMs = 0;
bool wifiSleepDeferredForUpdate = false;
bool otaBootConfirmationPending = false;
uint32_t otaBootConfirmationStartedMs = 0;

// Keep the OTA transfer buffer out of loopTask's limited stack. TLS, HTTPClient,
// SHA-256 and String locals already consume a substantial part of that stack.
uint8_t updateDownloadBuffer[UPDATE_DOWNLOAD_BUFFER_BYTES];

uint32_t stateStartedMs = 0;
uint32_t bootSplashStartedMs = 0;
uint32_t deviceInfoStartedMs = 0;
uint32_t portalStartedMs = 0;
uint32_t portalSuccessMs = 0;
uint32_t wifiDeadlineMs = 0;
uint32_t ntpDeadlineMs = 0;
uint32_t nextRetryMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastCycleMs = 0;
uint32_t lastInteractionMs = 0;
uint32_t scrollStartedMs = 0;
uint32_t lastDdayCalcMs = 0;
uint32_t lastLedMs = 0;
uint32_t savedWifiScanDeadlineMs = 0;
uint32_t portalWifiScanDeadlineMs = 0;
uint32_t lastTemperatureReadMs = 0;
uint32_t temperatureSampleSequence = 0;
uint32_t thermalProcessedSequence = 0;
temperature_sensor_handle_t chipTemperatureSensor = nullptr;
float chipTemperatureC = NAN;
bool thermalWarning = false;
bool thermalThrottled = false;
bool thermalSafeMode = false;
bool thermalSafeModeFromSensorFault = false;
bool temperatureSensorHighRange = false;
bool temperatureSensorFault = false;
uint8_t temperatureReadFailureCount = 0;
uint32_t normalCpuFrequencyMhz = 240;
uint32_t thermalCriticalStartedMs = 0;
uint32_t thermalRecoveryStartedMs = 0;
uint32_t temperatureFaultStartedMs = 0;
int lastDdayYear = -1;
int lastDdayYearDay = -1;

bool buttonRawPressed = false;
bool buttonStablePressed = false;
uint32_t buttonRawChangedMs = 0;
uint32_t buttonPressedMs = 0;
bool viewSavePending = false;
uint32_t viewSaveDueMs = 0;
bool resetConfirmation = false;
uint32_t resetConfirmStartedMs = 0;
bool resetConfirmPressEligible = false;
uint64_t cachedNightMinute = UINT64_MAX;
bool cachedNightMode = false;
int16_t appliedDisplayContrast = -1;
uint32_t appliedLedColor = UINT32_MAX;

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) out += ' ';
        else out += c;
    }
  }
  return out;
}

uint16_t utf8Codepoints(const String &value) {
  uint16_t count = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    if ((static_cast<uint8_t>(value[i]) & 0xC0) != 0x80) ++count;
  }
  return count;
}

bool elapsed(uint32_t now, uint32_t since, uint32_t period) {
  return static_cast<uint32_t>(now - since) >= period;
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

uint64_t uptimeSeconds() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
}

int clampInt(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool parseDate(const String &text, int &year, int &month, int &day) {
  if (text.length() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (uint8_t i : {0, 1, 2, 3, 5, 6, 8, 9}) {
    if (!isDigit(text[i])) return false;
  }
  year = text.substring(0, 4).toInt();
  month = text.substring(5, 7).toInt();
  day = text.substring(8, 10).toInt();
  if (year < 2024 || year > 2099 || month < 1 || month > 12) return false;
  const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int maxDay = days[month - 1] + ((month == 2 && isLeapYear(year)) ? 1 : 0);
  return day >= 1 && day <= maxDay;
}

String cycleOrderToString(const Config &cfg) {
  String result;
  for (uint8_t i = 0; i < VIEW_COUNT; ++i) {
    if (i) result += ',';
    result += String(cfg.cycleOrder[i]);
  }
  return result;
}

bool parseCycleOrderCount(const String &text, uint8_t *out, uint8_t expectedCount) {
  bool seen[VIEW_COUNT] = {};
  uint8_t count = 0;
  int start = 0;
  while (start <= static_cast<int>(text.length()) && count < expectedCount) {
    int comma = text.indexOf(',', start);
    if (comma < 0) comma = text.length();
    String part = text.substring(start, comma);
    part.trim();
    if (part.length() != 1 || part[0] < '0' || part[0] >= '0' + expectedCount) return false;
    uint8_t value = part[0] - '0';
    if (seen[value]) return false;
    seen[value] = true;
    out[count++] = value;
    start = comma + 1;
  }
  if (count != expectedCount || start <= static_cast<int>(text.length())) return false;
  for (uint8_t i = 0; i < expectedCount; ++i) if (!seen[i]) return false;
  return true;
}

template <size_t N>
bool allowedValue(uint32_t value, const uint32_t (&allowed)[N]) {
  for (uint32_t candidate : allowed) if (value == candidate) return true;
  return false;
}

const char *runtimeStateName(RuntimeState state) {
  static const char *const names[] = {"BOOTING", "UNPROVISIONED", "SETUP_AP", "CONNECTING", "TIME_SYNCING",
                                      "RUNNING_ONLINE", "RUNNING_OFFLINE", "WIFI_SLEEP", "ERROR_DISPLAY"};
  const uint8_t index = static_cast<uint8_t>(state);
  return index < sizeof(names) / sizeof(names[0]) ? names[index] : "ERROR_DISPLAY";
}

const char *wifiTestStateName(WifiTestState state) {
  static const char *const names[] = {"idle", "connecting", "time_syncing", "success", "failed"};
  const uint8_t index = static_cast<uint8_t>(state);
  return index < sizeof(names) / sizeof(names[0]) ? names[index] : "failed";
}

const char *updateStateName(UpdateState state) {
  static const char *const names[] = {"idle", "checking", "available", "downloading", "verifying",
                                      "rebooting", "current", "error"};
  const uint8_t index = static_cast<uint8_t>(state);
  return index < sizeof(names) / sizeof(names[0]) ? names[index] : "error";
}

bool firmwareTransferActive() {
  return updateState == UpdateState::DOWNLOADING || updateState == UpdateState::VERIFYING ||
         updateState == UpdateState::READY_TO_REBOOT;
}

bool firmwareUpdateBusy() {
  return updateState == UpdateState::CHECKING || firmwareTransferActive() || updateState == UpdateState::CURRENT;
}

View topModeView(TopMode mode) {
  return mode == TopMode::DEVICE_INFO
           ? View::DEVICE_INFO
           : static_cast<View>(static_cast<uint8_t>(mode));
}

bool timeIsValid() {
  return time(nullptr) >= MIN_VALID_EPOCH;
}

void logLine(const String &message) {
  Serial.print("[MILESTONE] ");
  Serial.println(message);
}

void setRuntimeState(RuntimeState next) {
  if (runtimeState == next) return;
  runtimeState = next;
  stateStartedMs = millis();
  logLine(String("state -> ") + runtimeStateName(next));
}

String savedWifiSsidKey(uint8_t index) {
  return String("wifi_ssid") + String(index);
}

String savedWifiPassKey(uint8_t index) {
  return String("wifi_pass") + String(index);
}

int findSavedNetwork(const String &ssid) {
  for (uint8_t i = 0; i < config.savedNetworkCount; ++i) {
    if (config.savedNetworks[i].ssid == ssid) return i;
  }
  return -1;
}

bool putStringVerified(const char *key, const String &value) {
  prefs.putString(key, value);
  return prefs.isKey(key) && prefs.getString(key, "") == value;
}

bool removePreferenceVerified(const char *key) {
  return !prefs.isKey(key) || prefs.remove(key);
}

bool saveWifiNetworks() {
  bool success = true;
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; ++i) {
    const String ssidKey = savedWifiSsidKey(i);
    const String passKey = savedWifiPassKey(i);
    if (i < config.savedNetworkCount) {
      if (!putStringVerified(ssidKey.c_str(), config.savedNetworks[i].ssid)) success = false;
      if (!putStringVerified(passKey.c_str(), config.savedNetworks[i].password)) success = false;
    } else {
      if (!removePreferenceVerified(ssidKey.c_str())) success = false;
      if (!removePreferenceVerified(passKey.c_str())) success = false;
    }
  }
  // Commit the count last. If power is lost during the preceding writes, the
  // loader can still compact the previously committed set instead of trusting
  // a new count whose entries may not all exist yet.
  if (!success || prefs.putUChar("wifi_count", config.savedNetworkCount) != sizeof(uint8_t)) return false;
  // Version 1-3 keys are intentionally left untouched. Schema 4 ignores them,
  // and retaining them makes a power loss during migration recoverable.
  return true;
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
  if (!saveWifiNetworks()) logLine(failure);
  return false;
}

bool upsertSavedNetwork(const String &ssid, const String &password) {
  const Config previous = config;
  const uint8_t previousActiveWifiIndex = activeWifiIndex;
  int existing = findSavedNetwork(ssid);
  SavedNetwork network;
  network.ssid = ssid;
  network.password = password;
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
  return saveWifiOrRestore(previous, previousActiveWifiIndex, "saved Wi-Fi rollback could not be persisted");
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
  return saveWifiOrRestore(previous, previousActiveWifiIndex, "preferred Wi-Fi rollback could not be persisted");
}

bool saveConfigAll() {
  bool success = saveWifiNetworks();
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
    uint8_t storedCount = prefs.getUChar("wifi_count", 0);
    if (storedCount > MAX_SAVED_NETWORKS) storedCount = MAX_SAVED_NETWORKS;
    for (uint8_t i = 0; i < storedCount; ++i) {
      String ssid = prefs.getString(savedWifiSsidKey(i).c_str(), "");
      String password = prefs.getString(savedWifiPassKey(i).c_str(), "");
      if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) continue;
      config.savedNetworks[config.savedNetworkCount].ssid = ssid;
      config.savedNetworks[config.savedNetworkCount].password = password;
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
  if (migrateToV3 || migrateToV4 || migrateToV5 || migrateToV6) {
    if (saveConfigAll()) {
      logLine(String("configuration migrated from schema ") + String(version) + " to " + String(CONFIG_VERSION));
    } else {
      logLine("configuration migration could not be persisted");
    }
  }
  return true;
}

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

bool refreshChipTemperature() {
  uint32_t now = millis();
  if (lastTemperatureReadMs != 0 && !elapsed(now, lastTemperatureReadMs, TEMPERATURE_REFRESH_MS)) return false;
  lastTemperatureReadMs = now;
  float reading = NAN;
  bool success = readChipTemperatureOnce(reading);
  if (!success) {
    const bool alternateHighRange = !temperatureSensorHighRange;
    success = switchChipTemperatureRange(alternateHighRange) && readChipTemperatureOnce(reading);
  }
  if (success && reading >= 20.0f && reading <= 125.0f) {
    chipTemperatureC = reading;
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
    display.drawCircle(x + 5, y + 5, 4);
    display.drawLine(x + 7, y + 1, x + 10, y + 1);
    display.drawLine(x + 10, y + 1, x + 10, y + 4);
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
  tm today = nowLocal;
  today.tm_hour = 0;
  today.tm_min = 0;
  today.tm_sec = 0;
  today.tm_isdst = -1;
  tm target{};
  target.tm_year = year - 1900;
  target.tm_mon = month - 1;
  target.tm_mday = day;
  target.tm_hour = 0;
  target.tm_isdst = -1;
  result = static_cast<int>(difftime(mktime(&target), mktime(&today)) / 86400.0);
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
  char pageText[8];
  snprintf(pageText, sizeof(pageText), "%u/%u", page + 1, DEVICE_INFO_PAGE_COUNT);
  display.drawStr(108 + ox, 10 + oy, pageText);
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
  display.drawStr(0, 10, "MILESTONE SETUP    AP");
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
  if (updateState == UpdateState::CHECKING) {
    drawCenteredStr("CHECKING UPDATE", 34);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("GitHub Release", 65);
    drawCenteredStr("Please wait...", 88);
  } else if (updateState == UpdateState::AVAILABLE && updatePromptVisible) {
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
  } else if (updateState == UpdateState::CURRENT) {
    drawCenteredStr("UP TO DATE", 34);
    display.setFont(u8g2_font_6x10_tf);
    String version = String("Version ") + FIRMWARE_VERSION;
    drawCenteredStr(version.c_str(), 68);
    drawCenteredStr("Latest firmware", 96);
  } else if (updateState == UpdateState::ERROR_STATE) {
    drawCenteredStr("UPDATE ERROR", 27);
    display.setFont(u8g2_font_5x8_tf);
    String line = updateError;
    if (line.length() > 23) line = line.substring(0, 23);
    drawCenteredStr(line.c_str(), 62);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredStr("Retry in 6 hours", 94);
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
  if (portalActive) {
    drawPortalScreen();
    return;
  }
  if (updateState == UpdateState::CURRENT && updateCurrentVisibleMs == 0) {
    updateCurrentVisibleMs = millis();
  }
  if (updateState == UpdateState::CHECKING || updateState == UpdateState::CURRENT ||
      (updateState == UpdateState::AVAILABLE && updatePromptVisible)) {
    drawUpdateScreen();
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
  }
  display.sendBuffer();
}

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

void cancelPortalWifiScan();

void beginNtpRequest() {
  if (WiFi.status() != WL_CONNECTED) return;
  cancelPortalWifiScan();
  ntpSyncEvent = false;
  ntpFailed = false;
  ntpRequestActive = true;
  ntpDeadlineMs = millis() + NTP_TIMEOUT_MS;
  configTzTime(TZ_INFO, "time.google.com", "pool.ntp.org", "time.cloudflare.com");
  logLine("NTP request started");
}

void markNtpSuccess() {
  ntpRequestActive = false;
  internetVerified = true;
  ntpFailed = false;
  config.lastSync = static_cast<uint64_t>(time(nullptr));
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

void beginStationConnection(const String &ssid, const String &password, bool preserveAp) {
  cancelPortalWifiScan();
  stopMdns();
  internetVerified = false;
  ntpFailed = false;
  WiFi.mode(preserveAp ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(false);  // The firmware selects among saved networks itself.
  WiFi.setSleep(true);
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), password.c_str());
  wifiDeadlineMs = millis() + WIFI_CONNECT_TIMEOUT_MS;
  if (wifiTestState == WifiTestState::IDLE) setRuntimeState(RuntimeState::CONNECTING);
  logLine("Wi-Fi connection started");
}

void beginSavedWifiConnection(uint8_t index, bool preserveAp) {
  if (index >= config.savedNetworkCount) return;
  activeWifiIndex = index;
  beginStationConnection(config.savedNetworks[index].ssid,
                         config.savedNetworks[index].password,
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

void scheduleRetry() {
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
      if (c == 'n') value += '\n';
      else if (c == 'r') value += '\r';
      else if (c == 't') value += '\t';
      else if (c == '"' || c == '\\' || c == '/') value += c;
      else return false;
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
  int start = 0;
  for (uint8_t index = 0; index < 3; ++index) {
    int end = index < 2 ? text.indexOf('.', start) : text.length();
    if (end <= start || (index < 2 && end < 0)) return false;
    uint32_t part = 0;
    for (int i = start; i < end; ++i) {
      if (!isDigit(text[i])) return false;
      part = part * 10UL + static_cast<uint8_t>(text[i] - '0');
      if (part > UINT16_MAX) return false;
    }
    parts[index] = static_cast<uint16_t>(part);
    start = end + 1;
  }
  return start == static_cast<int>(text.length()) + 1;
}

int compareSemanticVersions(const String &left, const String &right) {
  uint16_t a[3], b[3];
  if (!parseSemanticVersion(left, a) || !parseSemanticVersion(right, b)) return 0;
  for (uint8_t i = 0; i < 3; ++i) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

bool validSha256(const String &value) {
  if (value.length() != 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!isDigit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;
  }
  return true;
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
  WiFi.setSleep(true);
  if (WiFi.status() == WL_CONNECTED) startMdns();
}

void failFirmwareUpdate(const String &reason) {
  updateError = reason;
  updateInstallRequested = false;
  updateInstallNotBeforeMs = 0;
  updatePromptVisible = false;
  nextUpdateRetryMs = millis() + UPDATE_RETRY_MS;
  lastOtaResult = "failed";
  if (!putStringVerified("ota_result", lastOtaResult)) logLine("OTA failure result save failed");
  if (!clearOtaAttemptRecord()) logLine("OTA attempt record clear failed");
  restoreNetworkAfterFirmwareOperation();
  setUpdateState(UpdateState::ERROR_STATE);
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
  logLine(String("firmware installation deferred: ") + reason);
}

bool prepareUpdateNetworkRequest(const char *resource) {
  cancelPortalWifiScan();
  IPAddress address;
  if (WiFi.hostByName(UPDATE_GITHUB_HOST, address) != 1) {
    failFirmwareUpdate(String(resource) + " DNS failed");
    return false;
  }
  logLine(String(resource) + " DNS " + address.toString() +
          " heap=" + ESP.getFreeHeap() + " max=" + ESP.getMaxAllocHeap() +
          " stack=" + uxTaskGetStackHighWaterMark(nullptr));
  return true;
}

String describeHttpFailure(const char *resource, int response, NetworkClientSecure &secureClient) {
  char tlsDetails[160] = {};
  const int tlsError = secureClient.lastError(tlsDetails, sizeof(tlsDetails));
  const String httpDetails = response < 0 ? HTTPClient::errorToString(response) : String();

  String diagnostic = String(resource) + " request failed: code=" + response;
  if (httpDetails.length()) diagnostic += " (" + httpDetails + ")";
  diagnostic += " heap=" + String(ESP.getFreeHeap()) + " max=" + String(ESP.getMaxAllocHeap());
  logLine(diagnostic);
  if (tlsError != 0) {
    logLine(String(resource) + " TLS " + tlsError + ": " + tlsDetails);
    return String(resource) + " TLS failed";
  }
  if (response < 0 && httpDetails.length()) return String(resource) + " " + httpDetails;
  return String(resource) + " HTTP " + response;
}

bool readBoundedHttpBody(HTTPClient &http, String &body, size_t maximumBytes) {
  const int contentLength = http.getSize();
  if (contentLength > static_cast<int>(maximumBytes)) return false;
  NetworkClient *stream = http.getStreamPtr();
  body = "";
  if (contentLength > 0) body.reserve(contentLength + 1);
  uint32_t lastDataMs = millis();
  uint8_t buffer[256];
  while (http.connected() || stream->available()) {
    int available = stream->available();
    if (available > 0) {
      size_t wanted = min(static_cast<size_t>(available), sizeof(buffer));
      int count = stream->read(buffer, wanted);
      if (count <= 0) continue;
      if (body.length() + static_cast<size_t>(count) > maximumBytes) return false;
      for (int i = 0; i < count; ++i) body += static_cast<char>(buffer[i]);
      lastDataMs = millis();
      if (contentLength >= 0 && body.length() >= static_cast<size_t>(contentLength)) break;
    } else {
      if (elapsed(millis(), lastDataMs, UPDATE_HTTP_TIMEOUT_MS)) return false;
      delay(1);
    }
  }
  return body.length() > 0 && (contentLength < 0 || body.length() == static_cast<size_t>(contentLength));
}

void requestFirmwareUpdateCheck(UpdateCheckReason reason) {
  if (pendingUpdateCheckReason != UpdateCheckReason::NONE || firmwareUpdateBusy()) return;
  pendingUpdateCheckReason = reason;
}

bool checkFirmwareManifest(UpdateCheckReason reason) {
  if (reason == UpdateCheckReason::BOOT) bootUpdateCheckPending = false;
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    failFirmwareUpdate("temperature protection active");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    failFirmwareUpdate("Wi-Fi disconnected");
    return false;
  }
  if (!timeIsValid()) {
    failFirmwareUpdate("secure clock unavailable");
    return false;
  }
  if (ESP.getFreeHeap() < UPDATE_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < UPDATE_MIN_LARGEST_BLOCK) {
    failFirmwareUpdate("not enough internal RAM");
    return false;
  }
  if (!prepareUpdateNetworkRequest("manifest")) return false;

  setUpdateState(UpdateState::CHECKING);
  drawUpdateScreen();
  processLed();
  NetworkClientSecure secureClient;
  secureClient.setCACert(MILESTONE_UPDATE_ROOT_CA);
  secureClient.setHandshakeTimeout(12);
  HTTPClient http;
  http.setConnectTimeout(UPDATE_HTTP_TIMEOUT_MS);
  http.setTimeout(UPDATE_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(secureClient, UPDATE_MANIFEST_URL)) {
    failFirmwareUpdate("manifest request setup failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", String("MILESTONE-Core/") + FIRMWARE_VERSION);
  int response = http.GET();
  if (response != HTTP_CODE_OK) {
    const String failure = describeHttpFailure("manifest", response, secureClient);
    http.end();
    failFirmwareUpdate(failure);
    return false;
  }
  String manifest;
  bool bodyRead = readBoundedHttpBody(http, manifest, UPDATE_MANIFEST_MAX_BYTES);
  http.end();
  if (!bodyRead) {
    failFirmwareUpdate("manifest too large or timed out");
    return false;
  }

  String version, sha256, notes;
  uint32_t size = 0;
  uint16_t versionParts[3];
  if (!parseJsonStringField(manifest, "version", version) || !parseSemanticVersion(version, versionParts) ||
      !parseJsonUintField(manifest, "size", size) || size == 0 ||
      !parseJsonStringField(manifest, "sha256", sha256) || !validSha256(sha256)) {
    failFirmwareUpdate("invalid release manifest");
    return false;
  }
  parseJsonStringField(manifest, "notes", notes);
  sha256.toLowerCase();
  latestFirmwareVersion = version;
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

  if (compareSemanticVersions(FIRMWARE_VERSION, latestFirmwareVersion) < 0) {
    updatePromptVisible = reason != UpdateCheckReason::MANUAL || !portalActive;
    updatePromptStartedMs = millis();
    setUpdateState(UpdateState::AVAILABLE);
    logLine(String("firmware update available: ") + FIRMWARE_VERSION + " -> " + latestFirmwareVersion);
    return true;
  }

  updatePromptVisible = false;
  setUpdateState(UpdateState::CURRENT);
  if (portalActive) updateCurrentVisibleMs = millis();
  else if (!bootSplashActive()) {
    updateCurrentVisibleMs = millis();
    drawUpdateScreen();
  }
  logLine(String("firmware is current: ") + FIRMWARE_VERSION);
  return true;
}

bool installFirmwareUpdate() {
  if (updateState != UpdateState::AVAILABLE || latestFirmwareVersion.length() == 0 ||
      latestFirmwareSize == 0 || !validSha256(latestFirmwareSha256)) {
    failFirmwareUpdate("release metadata unavailable");
    return false;
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning ||
      (!isnan(chipTemperatureC) && chipTemperatureC >= THERMAL_THROTTLE_C)) {
    deferFirmwareInstall("temperature too high");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    deferFirmwareInstall("Wi-Fi disconnected");
    return false;
  }
  if (ESP.getFreeHeap() < UPDATE_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < UPDATE_MIN_LARGEST_BLOCK) {
    deferFirmwareInstall("not enough internal RAM");
    return false;
  }
  if (latestFirmwareSize > ESP.getFreeSketchSpace()) {
    deferFirmwareInstall("OTA partition too small");
    return false;
  }
  if (!prepareUpdateNetworkRequest("firmware")) return false;

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
  lastOtaResult = "installing";
  if (!putStringVerified("ota_result", lastOtaResult) ||
      !putStringVerified("ota_target", latestFirmwareVersion) ||
      !recordOtaStage("starting")) {
    failFirmwareUpdate("OTA state save failed");
    return false;
  }
  setUpdateState(UpdateState::DOWNLOADING);
  drawUpdateScreen();

  const String firmwareUrl = String(UPDATE_RELEASE_BASE_URL) + latestFirmwareVersion + '/' + UPDATE_ASSET_NAME;
  NetworkClientSecure secureClient;
  secureClient.setCACert(MILESTONE_UPDATE_ROOT_CA);
  secureClient.setHandshakeTimeout(12);
  HTTPClient http;
  http.setConnectTimeout(UPDATE_HTTP_TIMEOUT_MS);
  http.setTimeout(UPDATE_DOWNLOAD_STALL_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(secureClient, firmwareUrl)) {
    failFirmwareUpdate("firmware request setup failed");
    return false;
  }
  http.addHeader("Accept", "application/octet-stream");
  http.addHeader("User-Agent", String("MILESTONE-Core/") + FIRMWARE_VERSION);
  int response = http.GET();
  if (response != HTTP_CODE_OK) {
    const String failure = describeHttpFailure("firmware", response, secureClient);
    http.end();
    failFirmwareUpdate(failure);
    return false;
  }
  int responseSize = http.getSize();
  if (responseSize >= 0 && static_cast<uint32_t>(responseSize) != latestFirmwareSize) {
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
      size_t wanted = min(min(static_cast<size_t>(available), sizeof(updateDownloadBuffer)), remaining);
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
  if (!putStringVerified("ota_result", lastOtaResult) || !recordOtaStage("rebooting")) {
    const esp_err_t rollbackResult = esp_ota_set_boot_partition(esp_ota_get_running_partition());
    failFirmwareUpdate(rollbackResult == ESP_OK ? "OTA reboot state save failed" :
                                                "OTA reboot state and boot rollback failed");
    return false;
  }
  setUpdateState(UpdateState::READY_TO_REBOOT);
  drawUpdateScreen();
  processLed();
  logLine(String("firmware verified; rebooting into ") + latestFirmwareVersion);
  delay(1200);
  ESP.restart();
  return true;
}

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
  json += "\"time_valid\":" + String(timeIsValid() ? "true" : "false") + ",";
  json += "\"last_sync\":\"" + jsonEscape(formatEpoch(config.lastSync)) + "\",";
  json += "\"wifi_test\":\"" + String(wifiTestStateName(wifiTestState)) + "\",";
  json += "\"wifi_error\":\"" + jsonEscape(wifiTestError) + "\",";
  json += "\"update_state\":\"" + String(updateStateName(updateState)) + "\",";
  json += "\"latest_firmware\":\"" + jsonEscape(latestFirmwareVersion) + "\",";
  json += "\"update_available\":" + String(updateState == UpdateState::AVAILABLE ? "true" : "false") + ",";
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
  json.reserve(1024);
  String preferredSsid = config.savedNetworkCount > 0 ? config.savedNetworks[0].ssid : "";
  json += "\"wifi_ssid\":\"" + jsonEscape(preferredSsid) + "\",";
  json += "\"saved_networks\":[";
  for (uint8_t i = 0; i < config.savedNetworkCount; ++i) {
    if (i) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(config.savedNetworks[i].ssid) + "\",\"preferred\":";
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
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + WiFi.RSSI(i);
    json += ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
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
  if (!saveConfigAll()) {
    config = previous;
    if (!saveConfigAll()) logLine("configuration rollback could not be persisted");
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
  if (ssid.length() == 0 || ssid.length() > 32) return sendJson(400, "{\"error\":\"SSID를 확인하세요.\"}");
  if (password.length() > 63) return sendJson(400, "{\"error\":\"Wi-Fi 비밀번호가 너무 깁니다.\"}");
  int savedIndex = findSavedNetwork(ssid);
  if (password.length() == 0 && savedIndex >= 0) {
    password = config.savedNetworks[savedIndex].password;
  }
  pendingSsid = ssid;
  pendingPass = password;
  wifiTestError = "";
  wifiTestState = WifiTestState::CONNECTING;
  beginStationConnection(pendingSsid, pendingPass, true);
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
  if (!saveConfigAll()) {
    config = previous;
    if (!saveConfigAll()) logLine("settings reset rollback could not be persisted");
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
  if (firmwareUpdateBusy()) {
    return sendJson(409, "{\"error\":\"업데이트 작업이 이미 진행 중입니다.\"}");
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    return sendJson(409, "{\"error\":\"온도 보호 상태에서는 업데이트를 확인할 수 없습니다.\"}");
  }
  if (WiFi.status() != WL_CONNECTED) {
    return sendJson(409, "{\"error\":\"인터넷에 연결된 Wi-Fi가 필요합니다.\"}");
  }
  if (!timeIsValid()) {
    updateCheckAfterNetworkReady = true;
    beginNtpRequest();
    setRuntimeState(RuntimeState::TIME_SYNCING);
    return sendJson(202, "{\"ok\":true,\"state\":\"time_syncing\"}");
  }
  requestFirmwareUpdateCheck(UpdateCheckReason::MANUAL);
  sendJson(202, "{\"ok\":true,\"state\":\"queued\"}");
}

void handleUpdateInstall() {
  if (!authorizePortalRequest()) return;
  if (updateState != UpdateState::AVAILABLE) {
    return sendJson(409, "{\"error\":\"설치할 새 펌웨어가 확인되지 않았습니다.\"}");
  }
  if (thermalSafeMode || temperatureSensorFault || thermalWarning) {
    return sendJson(409, "{\"error\":\"온도 보호 상태에서는 업데이트할 수 없습니다.\"}");
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (config.savedNetworkCount == 0) {
      return sendJson(409, "{\"error\":\"인터넷에 연결된 Wi-Fi가 필요합니다.\"}");
    }
    updateCheckAfterNetworkReady = true;
    startSavedWifiSequence(true);
    return sendJson(202, "{\"ok\":true,\"state\":\"reconnecting\"}");
  }
  updateInstallRequested = true;
  updateInstallNotBeforeMs = millis() + UPDATE_INSTALL_RESPONSE_HOLD_MS;
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
  activeWifiIndex = 0;
  WiFi.begin(config.savedNetworks[0].ssid.c_str(), config.savedNetworks[0].password.c_str());
  logLine("restoring the previously saved Wi-Fi");
}

void failWifiTest(const String &reason);

void finishWifiTestSuccess() {
  const Config previous = config;
  if (!upsertSavedNetwork(pendingSsid, pendingPass) || !saveConfigAll()) {
    config = previous;
    if (!saveConfigAll()) logLine("tested Wi-Fi save rollback could not be persisted");
    failWifiTest("Wi-Fi 확인은 성공했지만 설정 저장에 실패했습니다.");
    return;
  }
  pendingSsid = "";
  pendingPass = "";
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
  setRuntimeState(RuntimeState::SETUP_AP);
  restoreConfiguredWifiAfterFailedTest();
  logLine(String("Wi-Fi test failed: ") + reason);
}

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
    wifiSleepDeferredForUpdate = config.wifiSleep;
    requestFirmwareUpdateCheck(UpdateCheckReason::BOOT);
  } else if (updateCheckAfterNetworkReady || firmwareUpdateCheckDue()) {
    updateCheckAfterNetworkReady = false;
    wifiSleepDeferredForUpdate = config.wifiSleep;
    requestFirmwareUpdateCheck(UpdateCheckReason::WEEKLY);
  } else if (config.wifiSleep) {
    enterWifiSleep();
  }
}

void processNetwork() {
  if (thermalSafeMode) return;
  const uint32_t now = millis();

  if (portalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (portalClosingAfterSuccess && elapsed(now, portalSuccessMs, PORTAL_SUCCESS_HOLD_MS)) {
      stopPortal();
      return;
    }
    if (!portalClosingAfterSuccess && wifiTestState != WifiTestState::CONNECTING &&
        wifiTestState != WifiTestState::TIME_SYNCING && elapsed(now, portalStartedMs, AP_TIMEOUT_MS)) {
      stopPortal();
      return;
    }
  }

  if (wifiTestState == WifiTestState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiTestState = WifiTestState::TIME_SYNCING;
      beginNtpRequest();
      setRuntimeState(RuntimeState::TIME_SYNCING);
    } else if (deadlineReached(now, wifiDeadlineMs)) {
      failWifiTest("Wi-Fi 연결 시간 초과. SSID와 비밀번호를 확인하세요.");
    }
    return;
  }

  if (wifiTestState == WifiTestState::TIME_SYNCING) {
    if (WiFi.status() != WL_CONNECTED) {
      failWifiTest("시간 확인 중 Wi-Fi 연결이 끊겼습니다.");
    } else if (ntpSyncEvent && timeIsValid()) {
      ntpSyncEvent = false;
      finishWifiTestSuccess();
    } else if (deadlineReached(now, ntpDeadlineMs)) {
      failWifiTest("인터넷 시간 확인에 실패했습니다. 인터넷 연결을 확인하세요.");
    }
    return;
  }

  if (runtimeState == RuntimeState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
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
        wifiSleepDeferredForUpdate = config.wifiSleep;
        requestFirmwareUpdateCheck(UpdateCheckReason::BOOT);
        logLine("boot-time NTP skipped; secure clock retained for update check");
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
        logLine("no reachable saved Wi-Fi network found; offline mode");
      }
    } else if (deadlineReached(now, wifiDeadlineMs)) {
      if (tryNextSavedWifiCandidate()) return;
      initialStationAttempt = false;
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      scheduleRetry();
      logLine("Wi-Fi connection timed out; offline mode");
    }
    return;
  }

  if (runtimeState == RuntimeState::TIME_SYNCING) {
    if (WiFi.status() != WL_CONNECTED) {
      ntpRequestActive = false;
      internetVerified = false;
      ntpFailed = false;
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      scheduleRetry();
    } else if (ntpSyncEvent && timeIsValid()) {
      ntpSyncEvent = false;
      finishNormalNtpSuccess();
    } else if (deadlineReached(now, ntpDeadlineMs)) {
      ntpRequestActive = false;
      internetVerified = false;
      ntpFailed = true;
      setRuntimeState(RuntimeState::RUNNING_OFFLINE);
      scheduleRetry();
      logLine("NTP timed out; last known D-day will be displayed");
    }
    return;
  }

  if (runtimeState == RuntimeState::RUNNING_ONLINE && WiFi.status() != WL_CONNECTED) {
    internetVerified = false;
    ntpFailed = false;
    setRuntimeState(RuntimeState::RUNNING_OFFLINE);
    scheduleRetry();
  }

  bool periodicNtpDue = (runtimeState == RuntimeState::RUNNING_ONLINE || runtimeState == RuntimeState::WIFI_SLEEP) &&
                         config.ntpPeriodSec > 0 && timeIsValid() && config.lastSync > 0 &&
                         static_cast<uint64_t>(time(nullptr)) >= config.lastSync + config.ntpPeriodSec;
  bool retryDue = runtimeState == RuntimeState::RUNNING_OFFLINE && deadlineReached(now, nextRetryMs);

  if (!portalActive && config.savedNetworkCount > 0) {
    if (periodicNtpDue && WiFi.status() == WL_CONNECTED) {
      beginNtpRequest();
      setRuntimeState(RuntimeState::TIME_SYNCING);
    } else if (periodicNtpDue || retryDue) {
      startSavedWifiSequence(false);
    }
  }

  const bool updateRetryDue = nextUpdateRetryMs != 0 && deadlineReached(now, nextUpdateRetryMs);
  const bool updateCanPause = updateState == UpdateState::IDLE || updateState == UpdateState::AVAILABLE;
  if (config.wifiSleep && runtimeState == RuntimeState::RUNNING_ONLINE &&
      WiFi.status() == WL_CONNECTED && !portalActive && !ntpRequestActive &&
      pendingUpdateCheckReason == UpdateCheckReason::NONE && !updateInstallRequested &&
      !updatePromptVisible && updateCanPause && !bootUpdateCheckPending &&
      !updateCheckAfterNetworkReady && !firmwareUpdateCheckDue() && !updateRetryDue) {
    wifiSleepDeferredForUpdate = false;
    enterWifiSleep();
  }
}

void processFirmwareUpdate() {
  const uint32_t now = millis();

  if (otaBootConfirmationPending && elapsed(now, otaBootConfirmationStartedMs, OTA_BOOT_CONFIRM_MS)) {
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) logLine("OTA application marked valid");
    else logLine(String("OTA rollback validation not active: ") + String(static_cast<int>(result)));
    otaBootConfirmationPending = false;
    lastOtaResult = "installed";
    if (!putStringVerified("ota_result", lastOtaResult)) logLine("OTA confirmation result save failed");
    if (!clearOtaAttemptRecord()) logLine("OTA confirmation record clear failed");
    logLine(String("OTA boot confirmed after stability hold: ") + FIRMWARE_VERSION);
  }

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
    const UpdateCheckReason reason = pendingUpdateCheckReason;
    pendingUpdateCheckReason = UpdateCheckReason::NONE;
    const bool success = checkFirmwareManifest(reason);
    if (success && WiFi.status() == WL_CONNECTED && timeIsValid()) {
      internetVerified = true;
      ntpFailed = false;
      if (runtimeState != RuntimeState::SETUP_AP) setRuntimeState(RuntimeState::RUNNING_ONLINE);
    }
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

  if (WiFi.status() == WL_CONNECTED && timeIsValid()) {
    wifiSleepDeferredForUpdate = config.wifiSleep;
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
  savedWifiScanActive = false;
  portalWifiScanActive = false;
  portalWifiScanDeadlineMs = 0;
  WiFi.scanDelete();
  if (!thermalThrottled) {
    thermalThrottled = setCpuFrequencyMhz(THERMAL_THROTTLE_CPU_MHZ);
  }
  stopMdns();
  if (portalActive) {
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
  if (frequencyRestored) logLine("chip temperature recovered; normal operation resumed");
  else logLine("chip temperature recovered; CPU frequency restore will be retried");
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
    if (temperatureFaultStartedMs == 0) temperatureFaultStartedMs = now;
    else if (!thermalSafeMode && elapsed(now, temperatureFaultStartedMs, TEMPERATURE_FAULT_SAFE_HOLD_MS)) {
      enterThermalSafeMode(true);
    }
    return;
  }
  temperatureFaultStartedMs = 0;
  if (isnan(chipTemperatureC)) return;
  thermalWarning = thermalWarning ? chipTemperatureC >= THERMAL_WARNING_CLEAR_C
                                  : chipTemperatureC >= THERMAL_WARNING_C;

  if (!thermalThrottled && chipTemperatureC >= THERMAL_THROTTLE_C) {
    thermalThrottled = setCpuFrequencyMhz(THERMAL_THROTTLE_CPU_MHZ);
    thermalRecoveryStartedMs = 0;
    if (thermalThrottled) {
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
    currentView = static_cast<View>((static_cast<uint8_t>(currentView) + 1) % VIEW_COUNT);
  }
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
  if (config.mode != TopMode::SELECTED_CYCLE || config.cycleIntervalSec == 0) return;
  if (elapsed(millis(), lastCycleMs, static_cast<uint32_t>(config.cycleIntervalSec) * 1000UL)) {
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
  if (currentView == View::DEVICE_INFO) deviceInfoStartedMs = millis();
  lastCycleMs = millis();
  scrollStartedMs = millis();
}

void setupFirmware() {
  Serial.begin(115200);
  normalCpuFrequencyMhz = getCpuFrequencyMhz();
  statusLed.begin();
  statusLed.clear();
  statusLed.show();
  writeStatusLed(40, 255, 180);
  delay(250);
  logLine(String("booting firmware ") + FIRMWARE_VERSION);
  Serial.printf("[MILESTONE] reset=%s (%d), heap=%lu, min_heap=%lu, largest=%lu\n",
                resetReasonName(esp_reset_reason()), static_cast<int>(esp_reset_reason()),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  // ESP32 requires the STA hostname to be set before Wi-Fi is started.
  WiFi.setHostname(HOSTNAME);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  buttonRawPressed = digitalRead(PIN_BOOT) == LOW;
  buttonStablePressed = buttonRawPressed;
  buttonRawChangedMs = millis();
  buttonPressedMs = buttonRawChangedMs;
  setenv("TZ", TZ_INFO, 1);
  tzset();
  sntp_set_time_sync_notification_cb(ntpTimeAvailable);

  prefs.begin(PREFS_NS, false);
  lastOtaResult = prefs.getString("ota_result", "");
  const String previousOtaStage = prefs.getString("ota_stage", "");
  const String previousOtaTarget = prefs.getString("ota_target", "");
  if (previousOtaStage.length()) {
    if (previousOtaStage == "rebooting" && previousOtaTarget == FIRMWARE_VERSION) {
      lastOtaResult = "validating";
      if (!putStringVerified("ota_result", lastOtaResult)) logLine("OTA validation state save failed");
      otaBootConfirmationPending = true;
      otaBootConfirmationStartedMs = millis();
      logLine(String("OTA boot awaiting stability confirmation: ") + FIRMWARE_VERSION);
    } else {
      lastOtaResult = String("interrupted-") + previousOtaStage;
      logLine(String("previous OTA interrupted at ") + previousOtaStage +
              " target=" + previousOtaTarget + " reset=" + resetReasonName(esp_reset_reason()));
      if (!putStringVerified("ota_result", lastOtaResult)) logLine("interrupted OTA result save failed");
      if (!clearOtaAttemptRecord()) logLine("interrupted OTA record clear failed");
    }
  }
  bool provisioned = loadConfig() && config.savedNetworkCount > 0;
  lastUpdateCheckEpoch = prefs.getULong64("ota_last_ok", 0);
  latestFirmwareVersion = prefs.getString("ota_latest", "");
  oledReady = initDisplay();
  if (oledReady) bootSplashStartedMs = millis();
  if (!oledReady) setRuntimeState(RuntimeState::ERROR_DISPLAY);
  initializeView();
  lastInteractionMs = millis();

  if (provisioned) {
    startSavedWifiSequence(false);
  } else {
    setRuntimeState(RuntimeState::UNPROVISIONED);
    startPortal();
  }
}

void loopFirmware() {
  processThermalProtection();
  processButton();
  processViewStateSave();
  processNetwork();
  processFirmwareUpdate();
  processCycle();
  processDisplay();
  processLed();
  delay(1);  // Yield to the Wi-Fi/USB stacks; all application work remains non-blocking.
}

}  // namespace Milestone

void setup() {
  Milestone::setupFirmware();
}

void loop() {
  Milestone::loopFirmware();
}
