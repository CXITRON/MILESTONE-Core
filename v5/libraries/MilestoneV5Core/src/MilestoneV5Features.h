#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MilestoneV5Runtime.h"

namespace MilestoneV5 {

int16_t centeredTextX(int16_t containerWidth, int16_t textWidth,
                      int16_t offset = 0);

enum class EnvironmentModel : uint8_t {
  kNone,
  kAht20,
  kBmp280,
  kBme280,
  kUnsupported
};

struct EnvironmentCapabilities {
  EnvironmentModel model;
  bool temperature;
  bool humidity;
  bool pressure;
};

EnvironmentCapabilities detectEnvironmentSensor(uint8_t chipId);

struct EnvironmentSample {
  float temperatureC;
  float humidityPercent;
  float pressureHpa;
  bool hasHumidity;
};

struct EnvironmentCalibration {
  float temperatureOffsetC;
  float humidityOffsetPercent;
  float pressureOffsetHpa;
};

bool validEnvironmentSample(const EnvironmentSample &sample,
                            const EnvironmentCapabilities &capabilities);

class EnvironmentTracker {
public:
  EnvironmentTracker(uint32_t staleAfterMs, EnvironmentCalibration calibration);

  bool accept(const EnvironmentSample &sample,
              const EnvironmentCapabilities &capabilities, uint32_t nowMs);
  bool stale(uint32_t nowMs) const;
  bool hasValue() const;
  const EnvironmentSample &filtered() const;
  uint32_t lastValidMs() const;
  uint32_t errorCount() const;

private:
  uint32_t staleAfterMs_;
  EnvironmentCalibration calibration_;
  EnvironmentSample filtered_;
  uint32_t lastValidMs_;
  uint32_t errorCount_;
  bool hasValue_;
};

enum class ModeMenuItem : uint8_t {
  kCore,
  kMedia,
  kNow,
  kSetup,
  kRestart,
  kSafeMode,
  kExit,
};

class ModeMenu {
public:
  ModeMenu();
  void open(Profile current);
  void close();
  void move(int8_t direction);
  bool isOpen() const;
  ModeMenuItem selected() const;

private:
  bool open_;
  ModeMenuItem selected_;
};

enum class MediaCategory : uint8_t { kPhoto, kVideo };
enum class MediaState : uint8_t { kBrowsing, kPlaying, kPaused };

class MediaBrowser {
public:
  MediaBrowser();
  void setCategory(MediaCategory category);
  void setItemCount(size_t itemCount);
  bool move(int8_t direction);
  bool playSelected();
  bool togglePause();
  void stop();
  void isolateCurrentCorruptItem();

  MediaCategory category() const;
  MediaState state() const;
  size_t itemCount() const;
  size_t selectedIndex() const;

private:
  MediaCategory category_;
  MediaState state_;
  size_t itemCount_;
  size_t selectedIndex_;
};

enum class ArtworkState : uint8_t { kAuto, kCustom, kBlocked, kMissing };

struct ArtworkCacheEntry {
  uint32_t id;
  uint32_t byteSize;
  uint64_t lastAccessEpoch;
  ArtworkState state;
};

int selectArtworkLruEviction(const ArtworkCacheEntry *entries, size_t count);
bool artworkMayUseServer(ArtworkState state);
bool artworkMayBeEvicted(ArtworkState state);

bool safeStorageLeafName(const char *name);

enum class AtomicWriteState : uint8_t {
  kIdle,
  kWritingTemporary,
  kValidating,
  kReadyToCommit,
  kCommitted,
  kFailed,
};

class AtomicWriteController {
public:
  AtomicWriteController();
  bool begin();
  bool temporaryWriteFinished(bool success);
  bool validationFinished(bool success);
  bool commitFinished(bool success);
  void reset();
  AtomicWriteState state() const;

private:
  AtomicWriteState state_;
};

} // namespace MilestoneV5
