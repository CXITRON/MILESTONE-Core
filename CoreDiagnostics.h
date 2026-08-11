#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MilestoneDiagnostics {

constexpr uint32_t HISTORY_MAGIC = 0x4D444831UL;  // "MDH1"
constexpr uint16_t HISTORY_VERSION = 1;
constexpr uint8_t HISTORY_CAPACITY = 16;
constexpr uint8_t SUPPRESSION_CAPACITY = 16;
constexpr int16_t DETAIL_MAX = 32767;
constexpr int32_t VALUE_MAX = 2147483647L;

enum class Event : uint16_t {
  NONE = 0,
  BOOT = 1,
  BOOT_VALIDATED = 2,

  WIFI_CONNECTED = 10,
  WIFI_CONNECT_TIMEOUT = 11,
  WIFI_DISCONNECTED = 12,
  WIFI_TEST_FAILED = 13,

  NTP_SUCCESS = 20,
  NTP_TIMEOUT = 21,

  UPDATE_CHECK_FAILED = 30,

  OTA_START = 40,
  OTA_FAILED = 41,
  OTA_READY = 42,

  ROLLBACK_ARMED = 50,
  BOOT_CANDIDATE = 51,
  ROLLBACK_TRIGGERED = 52,
  ROLLBACK_CONFIRMED = 53,

  THERMAL_WARNING = 60,
  THERMAL_THROTTLE = 61,
  THERMAL_PROTECTION = 62,
  THERMAL_RECOVERED = 63,
  TEMPERATURE_SENSOR_FAULT = 64,
};

enum class UpdateCheckFailure : int16_t {
  TRANSIENT = 1,
  FATAL = 2,
};

enum class OtaFailure : int16_t {
  UNKNOWN = 0,
  METADATA = 1,
  TEMPERATURE = 2,
  NETWORK = 3,
  MEMORY = 4,
  PARTITION = 5,
  STATE = 6,
  HTTP = 7,
  DOWNLOAD = 8,
  WRITE = 9,
  SHA256 = 10,
  FINALIZE = 11,
  ROLLBACK = 12,
};

enum class RollbackAction : int16_t {
  APPLICATION = 1,
  BOOTLOADER = 2,
  FAILED = 3,
};

struct Record {
  uint32_t epoch = 0;
  uint32_t uptimeSec = 0;
  Event event = Event::NONE;
  int16_t detail = 0;
  int32_t value = 0;
};

struct History {
  uint32_t magic = HISTORY_MAGIC;
  uint16_t version = HISTORY_VERSION;
  uint8_t count = 0;
  uint8_t next = 0;
  uint32_t sequence = 0;
  Record records[HISTORY_CAPACITY] = {};
  uint32_t checksum = 0;
};

struct SuppressionEntry {
  Event event = Event::NONE;
  int16_t detail = 0;
  uint32_t lastWriteMs = 0;
  bool valid = false;
};

static_assert(sizeof(Record) == 16, "Diagnostic Record layout changed");
static_assert(sizeof(History) == 272, "Diagnostic History layout changed");

uint32_t checksum(const History &history);
void reset(History &history);
bool valid(const History &history);
void append(History &history, const Record &record);
bool newest(const History &history, uint8_t newestIndex, Record &record);
bool shouldSuppressDuplicate(Event event, int16_t detail, uint32_t nowMs,
                             const SuppressionEntry *entries, size_t entryCount,
                             uint32_t windowMs);
void rememberDiagnosticWrite(Event event, int16_t detail, uint32_t nowMs,
                             SuppressionEntry *entries, size_t entryCount);
const char *eventName(Event event);
bool isError(Event event);

}  // namespace MilestoneDiagnostics
