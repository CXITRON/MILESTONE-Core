#include <cassert>
#include <cstdint>
#include <cstdio>
namespace MilestoneV5 { enum class Profile { kCore, kMedia, kNow }; }
struct Profiles {
  MilestoneV5::Profile value = MilestoneV5::Profile::kCore;
  MilestoneV5::Profile active() const { return value; }
} profiles;
struct Menu { bool open = false; bool isOpen() const { return open; } } modeMenu;
struct Busy { bool value = false; bool busy() const { return value; } } stableChannel;
struct { bool active = false; } bundleDownload;
struct { bool active = false, downloadRequested = false; } portal;
struct { bool ready = true; } nowMetadata;
struct { bool sdMounted = true; } hardware;
struct { bool critical() const { return false; } } bundleUpdate;
struct {
  uint8_t stage = 0;
  bool settle = true, allowed = false;
  unsigned calls = 0;
  bool settled(uint32_t) const { return settle; }
  void maintain(uint32_t, bool mounted, bool permit) { ++calls; allowed = mounted && permit; }
} artwork;
uint32_t now = 10000, lastInteractionMs = 0;
bool safeModeActive = false, mediaTimingCritical = false;
static bool admission() {
  artwork.allowed = false;
#include "main_artwork_admission.inc"
  return artwork.allowed;
}
int main() {
  assert(!admission());
  profiles.value = MilestoneV5::Profile::kMedia;
  assert(!admission());
  profiles.value = MilestoneV5::Profile::kNow;
  assert(admission());
  nowMetadata.ready = false; assert(!admission()); nowMetadata.ready = true;
  artwork.settle = false; assert(!admission()); artwork.settle = true;
  artwork.stage = 1; assert(!admission()); artwork.stage = 2; assert(!admission()); artwork.stage = 0;
  modeMenu.open = true; assert(!admission()); modeMenu.open = false;
  lastInteractionMs = now - 1499; assert(!admission()); lastInteractionMs = 0;
  bundleDownload.active = true; assert(!admission()); bundleDownload.active = false;
  stableChannel.value = true; assert(!admission()); stableChannel.value = false;
  portal.downloadRequested = true; assert(!admission()); portal.downloadRequested = false;
  mediaTimingCritical = true; assert(!admission()); mediaTimingCritical = false;
  safeModeActive = true; assert(!admission()); safeModeActive = false;
  profiles.value = MilestoneV5::Profile::kCore;
  portal.active = true; assert(admission());
  now = 1000; lastInteractionMs = UINT32_MAX - 1000; assert(admission());
  puts("v5 production artwork maintenance admission passed");
}
