#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <Adafruit_NeoPixel.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "driver/temperature_sensor.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_sntp.h"
#include "esp_system.h"
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#define MILESTONE_HAS_MODERN_EAP 1
#else
#include "esp_wpa2.h"
#define MILESTONE_HAS_MODERN_EAP 0
#endif
#endif

#include "PortalPage.h"
#include "UpdateCertificates.h"
#include "CoreLogic.h"
#include "CoreDiagnostics.h"
#include "CoreMedia.h"

// TLS, HTTP parsing, hashing, display updates, and the Arduino framework all
// share loopTask during a synchronous OTA transfer. Reserve an explicit stack
// instead of relying on the board package default.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// CYTRON//MILESTONE — MILESTONE Core
// Target: Waveshare ESP32-S3-Zero / ESP32-S3 Zero
// OLED: SH1107 128x128 I2C, verified at 0x3C

namespace Milestone {

constexpr char FIRMWARE_VERSION[] = "1.10.2";
constexpr char AP_SSID[] = "MILESTONE-D1-SETUP";
constexpr char HOSTNAME[] = "milestone-d1";
constexpr char PREFS_NS[] = "milestone";
constexpr char DIAGNOSTICS_PREFS_NS[] = "milestone_diag";
constexpr char UPDATE_MANIFEST_URL[] = "https://github.com/CXITRON/MILESTONE-Core/releases/latest/download/MILESTONE_Core.json";
constexpr char UPDATE_RELEASE_BASE_URL[] = "https://github.com/CXITRON/MILESTONE-Core/releases/download/v";
constexpr char UPDATE_ASSET_NAME[] = "MILESTONE_Core.bin";
constexpr uint16_t CONFIG_VERSION = 9;
constexpr uint8_t VIEW_COUNT = 8;
constexpr uint8_t MAX_SAVED_NETWORKS = 8;
constexpr uint8_t NO_WIFI_INDEX = 0xFF;

constexpr uint8_t PIN_SDA = 8;
constexpr uint8_t PIN_SCL = 9;
constexpr uint8_t PIN_BOOT = 0;
constexpr uint8_t PIN_RGB_LED = 21;
constexpr uint8_t OLED_ADDR_PRIMARY = 0x3C;
constexpr uint8_t OLED_ADDR_SECONDARY = 0x3D;

constexpr uint32_t AP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30UL * 1000UL;
constexpr uint32_t WIFI_DRIVER_SETTLE_MS = 100UL;
constexpr uint32_t WIFI_IP_STABLE_MS = 750UL;
constexpr uint32_t WIFI_QUICK_RETRY_MS = 15UL * 1000UL;
// SNTP may wait about 15 seconds before falling back from an unresponsive
// server. Keep the request alive long enough to use all configured servers;
// the OLED leaves the boot splash independently, so this does not extend the
// visible boot sequence.
constexpr uint32_t NTP_TIMEOUT_MS = 60UL * 1000UL;
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
constexpr uint32_t UPDATE_NETWORK_SETTLE_MS = 2000UL;
constexpr uint32_t BOOT_SPLASH_MS = 3000UL;
constexpr uint32_t DEVICE_INFO_PAGE_MS = 5000UL;
constexpr uint8_t DEVICE_INFO_PAGE_COUNT = 5;
constexpr uint32_t UPDATE_WEEKLY_SEC = 7UL * 24UL * 60UL * 60UL;
constexpr uint32_t UPDATE_CHECK_TRANSIENT_RETRY_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t UPDATE_FAILURE_RETRY_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t UPDATE_HTTP_CONNECT_TIMEOUT_MS = 10UL * 1000UL;
constexpr uint32_t UPDATE_HTTP_TIMEOUT_MS = 12UL * 1000UL;
constexpr uint32_t UPDATE_CHECK_ATTEMPT_BACKOFF_MS = 1500UL;
constexpr uint8_t UPDATE_CHECK_MAX_ATTEMPTS = 3;
constexpr uint8_t UPDATE_REDIRECT_LIMIT = 5;
constexpr uint8_t UPDATE_TLS_HANDSHAKE_TIMEOUT_SEC = 10;
constexpr uint32_t UPDATE_DOWNLOAD_STALL_MS = 20UL * 1000UL;
// Give the captive-portal browser enough time to receive the HTTP 202 response
// and render the accepted state before the synchronous OTA download occupies
// loopTask and temporarily pauses portal polling.
constexpr uint32_t UPDATE_INSTALL_RESPONSE_HOLD_MS = 1500UL;
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
  DEVICE_INFO = 6,
  CUSTOM_MEDIA = 7
};

enum class TopMode : uint8_t {
  DDAY_TIME = 0,
  DDAY_MESSAGE = 1,
  MESSAGE_ONLY = 2,
  CLOCK_ONLY = 3,
  MESSAGE_CLOCK = 4,
  DASHBOARD = 5,
  SELECTED_CYCLE = 6,
  DEVICE_INFO = 7,
  CUSTOM_MEDIA = 8
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

enum class WifiSecurityType : uint8_t {
  PERSONAL = 0,
  ENTERPRISE_PEAP = 1
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

enum class UpdateCheckAttemptResult : uint8_t {
  SUCCESS,
  RETRYABLE_FAILURE,
  FATAL_FAILURE
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
  WifiSecurityType security = WifiSecurityType::PERSONAL;
  String username;
  String identity;
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
  uint8_t cycleOrder[VIEW_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint8_t cycleIntervalSec = 8;
  uint8_t cycleIndex = 0;
  uint64_t lastSync = 0;
  int32_t lastDday = 0;
};

Preferences prefs;
Preferences diagnosticsPrefs;
bool prefsReady = false;
bool diagnosticsPrefsReady = false;
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
WifiSecurityType pendingWifiSecurity = WifiSecurityType::PERSONAL;
String pendingWifiUsername;
String pendingWifiIdentity;
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
bool freshNtpSinceBoot = false;
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
uint32_t updateCheckNotBeforeMs = 0;
uint32_t nextUpdateRetryMs = 0;
uint8_t updateCheckAttempt = 0;
uint64_t lastUpdateCheckEpoch = 0;
bool bootUpdateCheckPending = true;
bool updateCheckAfterNetworkReady = false;
bool updatePromptVisible = false;
bool updateInstallRequested = false;
uint32_t updateInstallNotBeforeMs = 0;
bool updateCheckIndicatorRendered = false;
bool wifiSleepDeferredForUpdate = false;
bool otaBootConfirmationPending = false;
uint32_t otaBootConfirmationStartedMs = 0;
bool otaRollbackArmed = false;
uint8_t otaRollbackBootAttempts = 0;
String otaRollbackPreviousLabel;
String otaRollbackPreviousVersion;
String otaRollbackTargetVersion;
String otaRollbackLastAction;
String otaRollbackLastReason;

MilestoneDiagnostics::History diagnosticHistory;
MilestoneDiagnostics::SuppressionEntry diagnosticSuppression[MilestoneDiagnostics::SUPPRESSION_CAPACITY];
bool diagnosticBootValidationPending = false;
bool diagnosticBootValidated = false;
uint32_t diagnosticBootValidationStartedMs = 0;
uint32_t wifiConnectionStartedMs = 0;
volatile uint16_t lastWifiDisconnectReason = 0;
uint32_t ntpRequestStartedMs = 0;
float highestChipTemperatureC = NAN;

// Keep the OTA transfer buffer out of loopTask's limited stack. TLS, HTTPClient,
// SHA-256 and String locals already consume a substantial part of that stack.
uint8_t updateDownloadBuffer[UPDATE_DOWNLOAD_BUFFER_BYTES];

uint32_t stateStartedMs = 0;
uint32_t bootSplashStartedMs = 0;
uint32_t deviceInfoStartedMs = 0;
uint32_t portalStartedMs = 0;
uint32_t portalSuccessMs = 0;
uint32_t wifiDeadlineMs = 0;
uint32_t wifiNetworkReadySinceMs = 0;
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

bool parseDate(const String &text, int &year, int &month, int &day) {
  MilestoneCoreLogic::CivilDate parsed;
  if (!MilestoneCoreLogic::parseIsoDate(text.c_str(), parsed)) return false;
  year = parsed.year;
  month = parsed.month;
  day = parsed.day;
  return true;
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
  return expectedCount <= VIEW_COUNT && MilestoneCoreLogic::parseCycleOrder(text.c_str(), out, expectedCount);
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
  return updateState == UpdateState::CHECKING || firmwareTransferActive();
}

View topModeView(TopMode mode) {
  if (mode == TopMode::DEVICE_INFO) return View::DEVICE_INFO;
  if (mode == TopMode::CUSTOM_MEDIA) return View::CUSTOM_MEDIA;
  return static_cast<View>(static_cast<uint8_t>(mode));
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

// CoreRollback.inc needs this helper before its implementation appears in
// CoreDisplay.inc. Arduino's automatic prototype generation does not cross
// this mechanically split .inc boundary reliably, so declare it explicitly.
const char *resetReasonName(esp_reset_reason_t reason);
void drawCenteredStr(const char *text, int baseline, int8_t offsetX);
bool bootSplashActive();
void recordDiagnostic(MilestoneDiagnostics::Event event, int16_t detail,
                      int32_t value, bool force);

#include "CoreConfig.inc"
#include "CoreMedia.inc"
#include "CoreDiagnostics.inc"
#include "CoreRollback.inc"
#include "CoreDisplay.inc"
#include "CoreNetwork.inc"
#include "CoreUpdate.inc"
#include "CorePortal.inc"
#include "CoreRuntime.inc"

}  // namespace Milestone

void setup() {
  Milestone::setupFirmware();
}

void loop() {
  Milestone::loopFirmware();
}
