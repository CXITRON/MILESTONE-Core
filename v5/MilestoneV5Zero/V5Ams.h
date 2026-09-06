#pragma once
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>
#include <os/os_mbuf.h>
#ifndef CONFIG_NIMBLE_ENABLED
#error "v5 ZERO requires the NimBLE-enabled Arduino core"
#endif
#define MILESTONE_HAS_BLUETOOTH 1
#define MILESTONE_BLUETOOTH_ALWAYS_ON 1

namespace V5Ams {
struct {
  bool bluetoothNowPlaying = true;
} config;
enum class RuntimeState { CONNECTING, TIME_SYNCING, READY };
RuntimeState runtimeState = RuntimeState::READY;
bool mediaStreamActive = false;
bool bluetoothFirmwareOperationIsolated = false;
bool bluetoothInitialNetworkGate = false;
bool stagedFirmwareInstallPending = false;
uint32_t scrollStartedMs = 0;
bool elapsed(uint32_t now, uint32_t since, uint32_t period) {
  return now - since >= period;
}
bool deadlineReached(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
void logLine(const String &s) {
  if (s.length() < 240 && Serial.availableForWrite() >= int(s.length() + 2))
    Serial.println(s);
}
namespace MilestoneCoreLogic {
bool shouldIgnoreLateBluetoothEncryptionFailure(bool secured, int status,
                                                int timeout) {
  return secured && status == timeout;
}
bool shouldApplyBluetoothDisconnect(uint16_t active, uint16_t event,
                                    uint16_t none) {
  return active != none && event != none && active == event;
}
} // namespace MilestoneCoreLogic
// Snapshot of the v3.3.4 AMS implementation; keep its security/disconnect
// order.
#include "V5AmsRuntime.inc"
} // namespace V5Ams
