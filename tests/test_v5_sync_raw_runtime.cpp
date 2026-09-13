// Execute the actual raw route and SD writer. The fake WebServer supplies the
// RAW_START/WRITE/END/ABORTED ordering used by Arduino-ESP32 3.3.11 Parsing.cpp.
#include <V5TestDisplay.h>
#include "../v5/MilestoneV5Main/V5SyncMedia.h"
#include <MilestoneV5Runtime.h>
#include <cassert>
#include <functional>
#include <map>
#include <vector>

enum { HTTP_POST, RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
struct HTTPRaw { int status = RAW_START; uint8_t buf[1436]{}; size_t currentSize = 0; };
struct FakeWebServer {
  std::map<std::string, String> headers;
  std::function<void()> finish, receive;
  HTTPRaw part;
  bool stopped = false;
  void on(const char *, int, std::function<void()> f, std::function<void()> r) {
    finish = f; receive = r;
  }
  String header(const char *name) { return headers[name]; }
  HTTPRaw &raw() { return part; }
  FakeWebServer &client() { return *this; }
  void stop() { stopped = true; }
  void setTimeout(unsigned) {}
};
struct Portal {
  FakeWebServer server;
  V5SyncMedia sync;
  MilestoneV5::Profile profile = MilestoneV5::Profile::kMedia;
  bool bundleBusy = false, bundleMediaBlocked = false, downloadBusy = false,
       wifiPending = false, syncUploadRejected = false, syncUploadFinalize = false,
       closeRequested = false;
  String syncUploadError, response;
  int responseCode = 0;
  std::function<bool()> transferService = [] { ++FakeOta::now; return true; };
  bool localRequest() { return true; }
  void touch() {}
  static String jsonEscape(const String &s) { return s; }
  void sendJson(int status, const String &body) { responseCode = status; response = body; }
#include "sync_raw_methods.inc"
  void routes() {
#include "sync_raw_route.inc"
  }
  void request(const std::vector<uint8_t> &bytes, uint32_t offset, bool final,
               bool abort = false) {
    responseCode = 0; server.stopped = false;
    server.headers = {{"Content-Type", "application/octet-stream"},
                      {"Content-Length", String(bytes.size())},
                      {"X-Sync-Offset", String(offset)},
                      {"X-Sync-Final", final ? "1" : "0"}};
    server.part.status = RAW_START; server.receive();
    for (size_t at = 0; at < bytes.size() && !server.stopped;) {
      auto &p = server.part;
      p.status = RAW_WRITE; p.currentSize = min(sizeof(p.buf), bytes.size() - at);
      memcpy(p.buf, bytes.data() + at, p.currentSize);
      at += p.currentSize; server.receive();
    }
    if (!server.stopped) {
      server.part.status = abort ? RAW_ABORTED : RAW_END; server.receive();
      if (!abort && !server.stopped) server.finish();
    }
  }
};

int main(int argc, char **argv) {
  (void)ESP;
  assert(argc == 2);
  FakeSd::root = std::filesystem::path(argv[1]) / "sync-raw-sd";
  std::filesystem::create_directories(FakeSd::root / "media");
  Portal p; p.sync.begin(true); p.routes();
  std::vector<uint8_t> batch(262160, 0);
  // A completed OTA is still active during observation, but does no Flash
  // install or firmware SD copy. A first 256 KiB Sync request must be accepted.
  p.bundleBusy = true; p.bundleMediaBlocked = false;
  p.request(batch, 0, false);
  assert(p.responseCode == 200 && !p.server.stopped);
  assert(p.sync.writtenBytes == batch.size());
  // Genuine install/download/Wi-Fi conflicts return an explicit HTTP error.
  p.bundleMediaBlocked = true;
  p.request(batch, uint32_t(batch.size()), false);
  assert(p.responseCode == 409 && !p.server.stopped);
  assert(p.sync.writtenBytes == batch.size());
  assert(std::string(p.response.c_str()).find("설치") != std::string::npos);
  p.bundleBusy = p.bundleMediaBlocked = false;
  p.downloadBusy = true;
  p.request(batch, uint32_t(batch.size()), false);
  assert(p.responseCode == 409 && !p.server.stopped);
  p.downloadBusy = false;
  // A interrupted request checkpoints its prefix, then accepts just the suffix.
  p.request(std::vector<uint8_t>(12345), uint32_t(batch.size()), false, true);
  assert(p.sync.writtenBytes == batch.size() + 12345);
  p.request(std::vector<uint8_t>(100), p.sync.writtenBytes, false);
  assert(p.responseCode == 200 && p.sync.writtenBytes == batch.size() + 12445);
  const uint32_t saved = p.sync.writtenBytes;
  p.request(batch, saved + 1, false);
  assert(p.responseCode == 409 && !p.server.stopped && p.sync.writtenBytes == saved);
  FakeSd::failWrites = true;
  p.request(batch, saved, false);
  assert(p.responseCode == 409 && !p.server.stopped);
  FakeSd::failWrites = false;
  assert(p.sync.state == V5SyncMedia::State::Error);
  // Unsupported/oversize bodies still close promptly without allocating SD data.
  p.request(std::vector<uint8_t>(327681), 0, false);
  assert(p.server.stopped);
  // A complete 1 MiB MVJ1 crosses the original 0.2 MiB failure point, then
  // still goes through production frame CRC/JPEG indexing before Ready.
  std::vector<uint8_t> video(16 + 90000 * 12, 0);
  const uint8_t header[] = {'M','V','J','1',128,0,128,0,20,0,0,0,0x90,0x5f,1,0};
  memcpy(video.data(), header, 16);
  for (unsigned i = 0; i < 90000; ++i) {
    auto record = video.data() + 16 + i * 12;
    record[0] = 4;
    const uint32_t crc = MilestoneV5::crc32(record + 8, 4);
    for (unsigned j = 0; j < 4; ++j) record[4 + j] = crc >> (8 * j);
  }
  for (size_t at = 0; at < video.size();) {
    const size_t end = min(at + 262144, video.size());
    p.request(std::vector<uint8_t>(video.begin() + at, video.begin() + end),
              at, end == video.size());
    assert(p.responseCode == 200 && !p.server.stopped && p.sync.writtenBytes == end);
    at = end;
  }
  assert(p.sync.state == V5SyncMedia::State::Indexing);
  p.sync.serviceIndex(millis());
  assert(p.sync.ready() && p.sync.indexedFrames == 90000);
  p.sync.remove();
  puts("v5 Sync raw callback admission/retry/error regressions passed");
}
