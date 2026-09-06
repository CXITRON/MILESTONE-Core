#pragma once

#include <stdint.h>

#include "MilestoneV5Runtime.h"

namespace MilestoneV5 {

// Draft revision for the new v5 settings model. This is deliberately not the
// released v3 CONFIG_VERSION and is not persisted until migration is defined.
constexpr uint16_t kDraftSettingsRevision = 1;

struct DisplaySettings {
  uint8_t luminancePercent;
  int8_t contrastPercent;
  uint16_t coreTimeRgb565;
  uint16_t coreDateRgb565;
  uint16_t coreMessageRgb565;
  uint16_t coreEventRgb565;
};

struct EnvironmentSettings {
  bool enabled;
  bool useFahrenheit;
  bool logToSd;
  float temperatureOffsetC;
  float humidityOffsetPercent;
  float pressureOffsetHpa;
  uint32_t sampleIntervalMs;
  uint32_t logIntervalMs;
};

struct ArtworkSettings {
  uint64_t cacheLimitBytes;
  uint64_t minimumFreeBytes;
};

struct RuntimeSettings {
  uint16_t draftRevision;
  Profile lastProfile;
  DisplaySettings display;
  EnvironmentSettings environment;
  ArtworkSettings artwork;
};

RuntimeSettings defaultRuntimeSettings();
bool validRuntimeSettings(const RuntimeSettings &settings);

} // namespace MilestoneV5
