#pragma once
namespace MilestoneV5 {
struct SystemSettings {
  bool loaded = true, bootSync = true, wifiSleep = false;
  uint32_t retrySeconds = 60, ntpSeconds = 60;
  void begin() {}
  void importLegacy(const uint8_t *) {}
};
} // namespace MilestoneV5
