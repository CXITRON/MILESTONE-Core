// Exercise production SD playback/cache classes. JPEG/TFT and SD latency are
// test doubles; these tests do not claim a measured hardware frame rate.
#include <V5TestDisplay.h>
#include "../v5/MilestoneV5Main/V5SyncMedia.h"
#include "../v5/MilestoneV5Main/V5Artwork.h"
#include <cassert>
#include <vector>

static void put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = n >> (i * 8);
}
static void writeFile(const String &name, const std::vector<uint8_t> &bytes) {
  File f = SD.open(name, FILE_WRITE);
  assert(f && f.write(bytes.data(), bytes.size()) == bytes.size());
  f.close();
}
static void playback() {
  V5SyncMedia sync;
  SimpleSt7735 display;
  std::filesystem::create_directories(FakeSd::root / "media");
  sync.begin(true);
  std::vector<uint8_t> data(16 + 300 * 12);
  const uint8_t header[16] = {'M','V','J','1',128,0,128,0,20,0,0,0,44,1,0,0};
  memcpy(data.data(), header, 16);
  for (unsigned frame = 0; frame < 300; ++frame) {
    auto p = data.data() + 16 + frame * 12;
    put32(p, 4);
    memset(p + 8, frame & 255, 4);
    put32(p + 4, MilestoneV5::crc32(p + 8, 4));
  }
  assert(sync.beginUpload(data.size()));
  assert(sync.writeUpload(data.data(), data.size()));
  assert(sync.finishUpload());
  sync.serviceIndex(millis());
  assert(sync.ready());
  assert(sync.control(0, true, 1000));
  FakeSd::reads.clear(); FakeSd::seeks.clear();
  for (unsigned i = 0; i < 300; ++i) {
    FakeOta::now = 1000 + i * 50;
    if (i % 5 == 0) assert(sync.control(i * 50, true, millis()));
    assert(sync.servicePlayback(display, millis(), false));
    assert(sync.displayedFrame == i);
  }
  assert(display.frames == 300);
  assert((FakeSd::reads["video.idx"] == 3) && "bounded index pages replace 300 index reads");
  assert((FakeSd::seeks["video.mvj"] == 1) && "sequential playback must not repeatedly seek the SD file");
  assert(sync.control(1100, false, millis()));
  assert(sync.servicePlayback(display, millis(), false));
  assert(sync.displayedFrame == 22);
  assert(!sync.servicePlayback(display, millis() + 1000, false));
  assert(sync.control(1100, true, millis()));
  assert(sync.servicePlayback(display, millis() + 700, false));
  assert((sync.displayedFrame == 36) && "late service follows audio time instead of slowing the film");
  assert(!sync.servicePlayback(display, millis() + 2600, false));
  assert(sync.state == V5SyncMedia::State::Paused);
  sync.stopPlayback();
  assert(sync.control(6400, false, millis()));
  assert(sync.servicePlayback(display, millis(), false));
  assert((sync.displayedFrame == 128) && "reopen invalidates the old index page");
  sync.remove();
}
static void artwork() {
  std::filesystem::create_directories(FakeSd::root / "now/art-cache");
  std::vector<uint8_t> packet(MilestoneV5::kArtworkBytes, 23);
  const uint8_t header[12] = {'M','A','C','1',60,60,88,88,28,32,60,128};
  memcpy(packet.data(), header, 12);
  uint32_t crc = MilestoneV5::crc32(packet.data()+16, packet.size()-16);
  for (unsigned i=0;i<4;++i) packet[12+i] = crc >> (24-i*8);
  assert(MilestoneV5::validArtwork(packet.data(), packet.size()));
  MilestoneV5::NowMetadata track{};
  strcpy(track.title, "Stored track"); strcpy(track.artist, "Artist");
  track.ready = track.connected = true;
  V5Artwork art;
  art.observe(track, 100, true);
  const String stored = String("/now/art-cache/") + art.key + ".mac";
  writeFile(stored, packet);
  art.observe(track, 1600, true);
  assert(art.visible && art.stage == 0);
  art.invalidate();
  FakeSd::used = FakeSd::total - 50 * 1024 * 1024;
  FakeSd::spaceQueries = 0;
  for (unsigned i=0;i<20;++i) art.maintain(62000+i, true);
  assert((SD.exists(stored)) && "low SD free space alone must not delete cached art");
  assert((FakeSd::spaceQueries == 0) && "periodic cache scans must not walk SD free space");

  V5Artwork rebooted;
  rebooted.observe(track, 100, true);
  FakeSd::failReadSuffix = ".mac";
  rebooted.observe(track, 1600, true);
  assert(!rebooted.visible && !rebooted.stage && SD.exists(stored));
  FakeSd::failReadSuffix.clear();
  rebooted.observe(track, 6599, true);
  assert(!rebooted.visible);
  rebooted.observe(track, 6600, true);
  assert((rebooted.visible && !rebooted.stage) && "transient read failure must retry the preserved file offline");
  track.elapsedSeconds = 80; track.playing = true;
  rebooted.observe(track, 7000, true);
  assert((rebooted.visible && SD.exists(stored)) && "playback progress is not cache identity");
  rebooted.invalidate();
  rebooted.maintain(8000, false);
  assert((SD.exists(stored)) && "mode exit / unmounted SD must not remove the file");

  // Sparse fixture crosses the cache budget without allocating 2 GiB of RAM.
  const String pinned = String(stored.c_str()).substring(0, stored.length()-4) + ".custom";
  writeFile(pinned, {1});
  const String over = String("/now/art-cache/") + std::string(64, 'f') + ".mac";
  writeFile(over, {0});
  std::filesystem::resize_file(FakeSd::path(over), 2ULL * 1024 * 1024 * 1024 + 1);
  art.requestRecount();
  for (unsigned i=0;i<20;++i) art.maintain(130000+i, true);
  assert((!SD.exists(over) && SD.exists(stored)) && "budget eviction must preserve user-pinned images");
  FakeSd::used = 1024 * 1024; // Ample SD space in all following failure cases.
  V5Artwork downloaded;
  downloaded.cacheKnown = true;
  strcpy(track.title, "Downloaded track");
  downloaded.observe(track, 140000, true);
  downloaded.observe(track, 142000, true);
  assert(downloaded.stage == 1);
  uint8_t response[6] = {3};
  put32(response + 1, downloaded.generation);
  downloaded.result(response, sizeof(response));
  assert(downloaded.stage == 2);
  FakeSd::failWrites = true;
  FakeOta::now = 142000;
  for (size_t offset = 0; offset < packet.size();) {
    const size_t count = std::min(size_t(460), packet.size() - offset);
    uint8_t chunk[468];
    put32(chunk, downloaded.generation); put32(chunk + 4, offset);
    memcpy(chunk + 8, packet.data() + offset, count);
    assert(downloaded.chunk(chunk, count + 8));
    offset += count;
  }
  const String downloadPath = String("/now/art-cache/") + downloaded.key + ".mac";
  assert(downloaded.visible && !downloaded.persisted && !SD.exists(downloadPath));
  assert(downloaded.saveFailures == 1 && downloaded.storageStatus.length());
  FakeSd::failWrites = false;
  // Changing track before the old 60-second rescan must not silently drop an
  // image whose first write failed and whose SD is now writable again.
  strcpy(track.title, "Next track");
  downloaded.observe(track, 142100, true);
  assert((SD.exists(downloadPath)) && "retry persistence before reusing the previous image buffer");

  V5Artwork staged;
  staged.cacheKnown = true;
  strcpy(track.title, "Staged track");
  staged.observe(track, 150000, true);
  const String staging = String("/now/art-cache/") + staged.key + ".tmp";
  const String finalPath = String("/now/art-cache/") + staged.key + ".mac";
  writeFile(staging, packet);
  FakeSd::failArtworkRename = true;
  staged.observe(track, 152000, true);
  assert(staged.visible && !staged.persisted && staged.stage == 0);
  assert(SD.exists(staging) && !SD.exists(finalPath));
  assert(staged.saveFailures == 1);
  // A new instance models reboot: no new network transfer or original browser
  // is needed to recover a fully verified staging file.
  FakeSd::failArtworkRename = false;
  V5Artwork recovered;
  recovered.cacheKnown = true;
  recovered.observe(track, 100, true);
  recovered.observe(track, 1600, true);
  assert(recovered.visible && recovered.persisted && recovered.stage == 0);
  assert(SD.exists(finalPath) && !SD.exists(staging));
  free(downloaded.packet); free(staged.packet); free(recovered.packet);
  free(art.packet); free(rebooted.packet);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  FakeSd::root = std::filesystem::path(argv[1]) / "media-runtime";
  playback();
  artwork();
  puts("v5 SD playback and persistent artwork runtime tests passed");
}
