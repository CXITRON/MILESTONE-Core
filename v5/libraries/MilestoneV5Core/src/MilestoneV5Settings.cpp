#include "MilestoneV5Settings.h"

#include <math.h>

namespace MilestoneV5 {

RuntimeSettings defaultRuntimeSettings() {
  RuntimeSettings settings = {};
  settings.draftRevision = kDraftSettingsRevision;
  settings.lastProfile = Profile::kCore;
  settings.display = {92, 8, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
  settings.environment = {true, false, false, 0, 0, 0, 5000, 60000};
  settings.artwork = {2ULL * 1024 * 1024 * 1024, 1ULL * 1024 * 1024 * 1024};
  return settings;
}

bool validRuntimeSettings(const RuntimeSettings &settings) {
  if (settings.draftRevision != kDraftSettingsRevision)
    return false;
  if (settings.lastProfile != Profile::kCore &&
      settings.lastProfile != Profile::kMedia &&
      settings.lastProfile != Profile::kNow)
    return false;
  if (settings.display.luminancePercent < 50 ||
      settings.display.luminancePercent > 100 ||
      settings.display.contrastPercent < -20 ||
      settings.display.contrastPercent > 20) {
    return false;
  }
  const EnvironmentSettings &environment = settings.environment;
  if (!isfinite(environment.temperatureOffsetC) ||
      !isfinite(environment.humidityOffsetPercent) ||
      !isfinite(environment.pressureOffsetHpa) ||
      environment.temperatureOffsetC < -20 ||
      environment.temperatureOffsetC > 20 ||
      environment.humidityOffsetPercent < -50 ||
      environment.humidityOffsetPercent > 50 ||
      environment.pressureOffsetHpa < -100 ||
      environment.pressureOffsetHpa > 100 ||
      environment.sampleIntervalMs < 1000 ||
      environment.sampleIntervalMs > 3600000 ||
      environment.logIntervalMs < environment.sampleIntervalMs)
    return false;
  if (settings.artwork.cacheLimitBytes == 0 ||
      settings.artwork.minimumFreeBytes == 0) {
    return false;
  }
  return true;
}

} // namespace MilestoneV5
