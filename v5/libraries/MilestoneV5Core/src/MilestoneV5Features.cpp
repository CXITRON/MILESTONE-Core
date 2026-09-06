#include "MilestoneV5Features.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

namespace MilestoneV5 {

int16_t centeredTextX(int16_t containerWidth, int16_t textWidth,
                      int16_t offset) {
  if (containerWidth <= 0 || textWidth <= 0)
    return offset < 0 ? 0 : offset;
  int32_t x = (int32_t(containerWidth) - textWidth) / 2 + offset;
  if (x < 0)
    x = 0;
  if (x >= containerWidth)
    x = containerWidth - 1;
  return static_cast<int16_t>(x);
}

EnvironmentCapabilities detectEnvironmentSensor(uint8_t chipId) {
  if (chipId == 0x38)
    return {EnvironmentModel::kAht20, true, true, false};
  if (chipId == 0x60)
    return {EnvironmentModel::kBme280, true, true, true};
  if (chipId == 0x58)
    return {EnvironmentModel::kBmp280, true, false, true};
  if (chipId == 0 || chipId == 0xFF)
    return {EnvironmentModel::kNone, false, false, false};
  return {EnvironmentModel::kUnsupported, false, false, false};
}

bool validEnvironmentSample(const EnvironmentSample &sample,
                            const EnvironmentCapabilities &capabilities) {
  if (!capabilities.temperature)
    return false;
  if (!isfinite(sample.temperatureC) || sample.temperatureC < -40.0f ||
      sample.temperatureC > 85.0f)
    return false;
  if (capabilities.pressure &&
      (!isfinite(sample.pressureHpa) || sample.pressureHpa < 300.0f ||
       sample.pressureHpa > 1100.0f))
    return false;
  if (capabilities.humidity) {
    if (!sample.hasHumidity || !isfinite(sample.humidityPercent) ||
        sample.humidityPercent < 0.0f || sample.humidityPercent > 100.0f)
      return false;
  } else if (sample.hasHumidity) {
    return false;
  }
  return true;
}

EnvironmentTracker::EnvironmentTracker(uint32_t staleAfterMs,
                                       EnvironmentCalibration calibration)
    : staleAfterMs_(staleAfterMs), calibration_(calibration),
      filtered_{0, 0, 0, false}, lastValidMs_(0), errorCount_(0),
      hasValue_(false) {}

bool EnvironmentTracker::accept(const EnvironmentSample &sample,
                                const EnvironmentCapabilities &capabilities,
                                uint32_t nowMs) {
  if (!validEnvironmentSample(sample, capabilities)) {
    ++errorCount_;
    return false;
  }
  EnvironmentSample adjusted = sample;
  adjusted.temperatureC += calibration_.temperatureOffsetC;
  if (capabilities.pressure)
    adjusted.pressureHpa += calibration_.pressureOffsetHpa;
  else
    adjusted.pressureHpa = 0;
  if (adjusted.hasHumidity)
    adjusted.humidityPercent += calibration_.humidityOffsetPercent;
  if (!validEnvironmentSample(adjusted, capabilities)) {
    ++errorCount_;
    return false;
  }
  if (!hasValue_) {
    filtered_ = adjusted;
    hasValue_ = true;
  } else {
    constexpr float kNewSampleWeight = 0.25f;
    filtered_.temperatureC +=
        (adjusted.temperatureC - filtered_.temperatureC) * kNewSampleWeight;
    if (capabilities.pressure)
      filtered_.pressureHpa +=
          (adjusted.pressureHpa - filtered_.pressureHpa) * kNewSampleWeight;
    else
      filtered_.pressureHpa = 0;
    filtered_.hasHumidity = adjusted.hasHumidity;
    if (adjusted.hasHumidity) {
      filtered_.humidityPercent +=
          (adjusted.humidityPercent - filtered_.humidityPercent) *
          kNewSampleWeight;
    } else {
      filtered_.humidityPercent = 0;
    }
  }
  lastValidMs_ = nowMs;
  return true;
}

bool EnvironmentTracker::stale(uint32_t nowMs) const {
  return hasValue_ && nowMs - lastValidMs_ > staleAfterMs_;
}
bool EnvironmentTracker::hasValue() const { return hasValue_; }
const EnvironmentSample &EnvironmentTracker::filtered() const {
  return filtered_;
}
uint32_t EnvironmentTracker::lastValidMs() const { return lastValidMs_; }
uint32_t EnvironmentTracker::errorCount() const { return errorCount_; }

ModeMenu::ModeMenu() : open_(false), selected_(ModeMenuItem::kCore) {}

void ModeMenu::open(Profile current) {
  open_ = true;
  selected_ = static_cast<ModeMenuItem>(current);
}
void ModeMenu::close() { open_ = false; }
void ModeMenu::move(int8_t direction) {
  if (!open_ || direction == 0)
    return;
  int value = static_cast<int>(selected_) + (direction < 0 ? -1 : 1);
  if (value < 0)
    value = static_cast<int>(ModeMenuItem::kExit);
  if (value > static_cast<int>(ModeMenuItem::kExit))
    value = 0;
  selected_ = static_cast<ModeMenuItem>(value);
}
bool ModeMenu::isOpen() const { return open_; }
ModeMenuItem ModeMenu::selected() const { return selected_; }

MediaBrowser::MediaBrowser()
    : category_(MediaCategory::kPhoto), state_(MediaState::kBrowsing),
      itemCount_(0), selectedIndex_(0) {}
void MediaBrowser::setCategory(MediaCategory category) {
  stop();
  category_ = category;
  selectedIndex_ = 0;
}
void MediaBrowser::setItemCount(size_t itemCount) {
  itemCount_ = itemCount;
  if (itemCount_ == 0) {
    selectedIndex_ = 0;
    stop();
  } else if (selectedIndex_ >= itemCount_) {
    selectedIndex_ = itemCount_ - 1;
  }
}
bool MediaBrowser::move(int8_t direction) {
  if (state_ != MediaState::kBrowsing || itemCount_ == 0 || direction == 0)
    return false;
  if (direction < 0)
    selectedIndex_ = selectedIndex_ == 0 ? itemCount_ - 1 : selectedIndex_ - 1;
  else
    selectedIndex_ = selectedIndex_ + 1 == itemCount_ ? 0 : selectedIndex_ + 1;
  return true;
}
bool MediaBrowser::playSelected() {
  if (itemCount_ == 0 || state_ != MediaState::kBrowsing)
    return false;
  state_ = MediaState::kPlaying;
  return true;
}
bool MediaBrowser::togglePause() {
  if (state_ == MediaState::kPlaying)
    state_ = MediaState::kPaused;
  else if (state_ == MediaState::kPaused)
    state_ = MediaState::kPlaying;
  else
    return false;
  return true;
}
void MediaBrowser::stop() { state_ = MediaState::kBrowsing; }
void MediaBrowser::isolateCurrentCorruptItem() {
  if (itemCount_ == 0)
    return;
  --itemCount_;
  if (itemCount_ == 0)
    selectedIndex_ = 0;
  else if (selectedIndex_ >= itemCount_)
    selectedIndex_ = 0;
  stop();
}
MediaCategory MediaBrowser::category() const { return category_; }
MediaState MediaBrowser::state() const { return state_; }
size_t MediaBrowser::itemCount() const { return itemCount_; }
size_t MediaBrowser::selectedIndex() const { return selectedIndex_; }

bool artworkMayUseServer(ArtworkState state) {
  return state == ArtworkState::kAuto || state == ArtworkState::kMissing;
}
bool artworkMayBeEvicted(ArtworkState state) {
  return state == ArtworkState::kAuto;
}
int selectArtworkLruEviction(const ArtworkCacheEntry *entries, size_t count) {
  if (entries == nullptr)
    return -1;
  int selected = -1;
  for (size_t i = 0; i < count; ++i) {
    if (!artworkMayBeEvicted(entries[i].state))
      continue;
    if (selected < 0 ||
        entries[i].lastAccessEpoch < entries[selected].lastAccessEpoch ||
        (entries[i].lastAccessEpoch == entries[selected].lastAccessEpoch &&
         entries[i].id < entries[selected].id)) {
      selected = static_cast<int>(i);
    }
  }
  return selected;
}

bool safeStorageLeafName(const char *name) {
  if (name == nullptr)
    return false;
  const size_t length = strlen(name);
  if (length == 0 || length > 96 || strcmp(name, ".") == 0 ||
      strcmp(name, "..") == 0) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const unsigned char value = static_cast<unsigned char>(name[i]);
    if (value < 0x20 || value == 0x7F || value == '/' || value == '\\')
      return false;
  }
  return strstr(name, "..") == nullptr;
}

AtomicWriteController::AtomicWriteController()
    : state_(AtomicWriteState::kIdle) {}
bool AtomicWriteController::begin() {
  if (state_ != AtomicWriteState::kIdle)
    return false;
  state_ = AtomicWriteState::kWritingTemporary;
  return true;
}
bool AtomicWriteController::temporaryWriteFinished(bool success) {
  if (state_ != AtomicWriteState::kWritingTemporary)
    return false;
  state_ = success ? AtomicWriteState::kValidating : AtomicWriteState::kFailed;
  return true;
}
bool AtomicWriteController::validationFinished(bool success) {
  if (state_ != AtomicWriteState::kValidating)
    return false;
  state_ =
      success ? AtomicWriteState::kReadyToCommit : AtomicWriteState::kFailed;
  return true;
}
bool AtomicWriteController::commitFinished(bool success) {
  if (state_ != AtomicWriteState::kReadyToCommit)
    return false;
  state_ = success ? AtomicWriteState::kCommitted : AtomicWriteState::kFailed;
  return true;
}
void AtomicWriteController::reset() { state_ = AtomicWriteState::kIdle; }
AtomicWriteState AtomicWriteController::state() const { return state_; }

} // namespace MilestoneV5
