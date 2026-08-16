#include "CoreDiagnostics.h"

#include <stddef.h>
#include <string.h>

namespace MilestoneDiagnostics {
namespace {

uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t value = 2166136261UL;
  for (size_t i = 0; i < length; ++i) {
    value ^= data[i];
    value *= 16777619UL;
  }
  return value;
}

}  // namespace

uint32_t checksum(const History &history) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&history), offsetof(History, checksum));
}

void reset(History &history) {
  history = History();
  history.checksum = checksum(history);
}

bool valid(const History &history) {
  if (history.magic != HISTORY_MAGIC || history.version != HISTORY_VERSION) return false;
  if (history.count > HISTORY_CAPACITY || history.next >= HISTORY_CAPACITY) return false;
  return history.checksum == checksum(history);
}

void append(History &history, const Record &record) {
  if (!valid(history)) reset(history);
  history.records[history.next] = record;
  history.next = static_cast<uint8_t>((history.next + 1U) % HISTORY_CAPACITY);
  if (history.count < HISTORY_CAPACITY) ++history.count;
  ++history.sequence;
  history.checksum = checksum(history);
}

bool newest(const History &history, uint8_t newestIndex, Record &record) {
  if (!valid(history) || newestIndex >= history.count) return false;
  const uint8_t newestSlot = static_cast<uint8_t>((history.next + HISTORY_CAPACITY - 1U) % HISTORY_CAPACITY);
  const uint8_t slot = static_cast<uint8_t>((newestSlot + HISTORY_CAPACITY - newestIndex) % HISTORY_CAPACITY);
  record = history.records[slot];
  return true;
}

bool shouldSuppressDuplicate(Event event, int16_t detail, uint32_t nowMs,
                             const SuppressionEntry *entries, size_t entryCount,
                             uint32_t windowMs) {
  if (!entries) return false;
  for (size_t i = 0; i < entryCount; ++i) {
    const SuppressionEntry &entry = entries[i];
    if (entry.valid && entry.event == event && entry.detail == detail) {
      return static_cast<uint32_t>(nowMs - entry.lastWriteMs) < windowMs;
    }
  }
  return false;
}

void rememberDiagnosticWrite(Event event, int16_t detail, uint32_t nowMs,
                             SuppressionEntry *entries, size_t entryCount) {
  if (!entries || entryCount == 0) return;

  size_t destination = entryCount;
  uint32_t oldestAge = 0;
  for (size_t i = 0; i < entryCount; ++i) {
    SuppressionEntry &entry = entries[i];
    if (entry.valid && entry.event == event && entry.detail == detail) {
      destination = i;
      break;
    }
    if (!entry.valid) {
      destination = i;
      break;
    }
    const uint32_t age = static_cast<uint32_t>(nowMs - entry.lastWriteMs);
    if (destination == entryCount || age > oldestAge) {
      destination = i;
      oldestAge = age;
    }
  }

  SuppressionEntry &entry = entries[destination];
  entry.event = event;
  entry.detail = detail;
  entry.lastWriteMs = nowMs;
  entry.valid = true;
}

const char *eventName(Event event) {
  switch (event) {
    case Event::BOOT: return "boot";
    case Event::BOOT_VALIDATED: return "boot_validated";
    case Event::WIFI_CONNECTED: return "wifi_connected";
    case Event::WIFI_CONNECT_TIMEOUT: return "wifi_connect_timeout";
    case Event::WIFI_DISCONNECTED: return "wifi_disconnected";
    case Event::WIFI_TEST_FAILED: return "wifi_test_failed";
    case Event::NTP_SUCCESS: return "ntp_success";
    case Event::NTP_TIMEOUT: return "ntp_timeout";
    case Event::UPDATE_CHECK_FAILED: return "update_check_failed";
    case Event::OTA_START: return "ota_start";
    case Event::OTA_FAILED: return "ota_failed";
    case Event::OTA_READY: return "ota_ready";
    case Event::ROLLBACK_ARMED: return "rollback_armed";
    case Event::BOOT_CANDIDATE: return "boot_candidate";
    case Event::ROLLBACK_TRIGGERED: return "rollback_triggered";
    case Event::ROLLBACK_CONFIRMED: return "rollback_confirmed";
    case Event::THERMAL_WARNING: return "thermal_warning";
    case Event::THERMAL_THROTTLE: return "thermal_throttle";
    case Event::THERMAL_PROTECTION: return "thermal_protection";
    case Event::THERMAL_RECOVERED: return "thermal_recovered";
    case Event::TEMPERATURE_SENSOR_FAULT: return "temperature_sensor_fault";
    case Event::MEDIA_FAILED: return "media_failed";
    default: return "unknown";
  }
}

bool isError(Event event) {
  switch (event) {
    case Event::WIFI_CONNECT_TIMEOUT:
    case Event::WIFI_DISCONNECTED:
    case Event::WIFI_TEST_FAILED:
    case Event::NTP_TIMEOUT:
    case Event::UPDATE_CHECK_FAILED:
    case Event::OTA_FAILED:
    case Event::ROLLBACK_TRIGGERED:
    case Event::THERMAL_WARNING:
    case Event::THERMAL_THROTTLE:
    case Event::THERMAL_PROTECTION:
    case Event::TEMPERATURE_SENSOR_FAULT:
    case Event::MEDIA_FAILED:
      return true;
    default:
      return false;
  }
}

}  // namespace MilestoneDiagnostics
