#define MILESTONE_V5_RELEASE_PUBLIC_KEY "test-double-only"
#include <Preferences.h>
#include <SD.h>
#include <esp_ota_ops.h>
struct V5Hardware { bool sdMounted = true; };
#define MILESTONE_V5_HARDWARE_DECLARED 1
#include "../v5/MilestoneV5Main/V5StableChannel.h"
#include <cassert>

std::string hash(const std::vector<uint8_t> &data) {
  uint8_t result[32];
  mbedtls_sha256(data.data(), data.size(), result, 0);
  std::string text;
  for (auto byte : result) {
    text += "0123456789abcdef"[byte >> 4];
    text += "0123456789abcdef"[byte & 15];
  }
  return text;
}
void fixture(const char *name, const std::string &text) {
  V5DownloadWorker::fixtures[name] = std::vector<uint8_t>(text.begin(), text.end());
}
int main(int argc, char **argv) {
  assert(argc == 2);
  FakeSd::root = std::filesystem::path(argv[1]) / "stable-channel";
  std::filesystem::create_directories(FakeSd::root);
  FakeOta::reset();
  FakeNvs::records.clear();
  const std::vector<uint8_t> mainBin{1,2,3}, zeroBin{4,5,6};
  const std::string hashes = hash(mainBin) + " " + hash(zeroBin) + "\n";
  fixture("v5-stable.txt", "MILESTONE-V5 STABLE 5.0.0 " + hashes);
  fixture("v5-bundle.txt", "MILESTONE-V5 BUNDLE 5.0.0 " + hashes);
  fixture("v5-main-manifest.txt", "MILESTONE-V5 MAIN 5.0.0 3 " + hash(mainBin) + " 1 1\n");
  fixture("v5-zero-manifest.txt", "MILESTONE-V5 ZERO 5.0.0 3 " + hash(zeroBin) + " 1 1\n");
  for (const char *name : {"v5-stable.sig", "v5-bundle.sig", "v5-main-manifest.sig", "v5-zero-manifest.sig"})
    V5DownloadWorker::fixtures[name] = {42};
  V5DownloadWorker::fixtures["v5-main.bin"] = mainBin;
  V5DownloadWorker::fixtures["v5-zero.bin"] = zeroBin;
  V5Hardware hardware;
  V5SdUpdate mainUpdate;
  V5ZeroUpdate zeroUpdate;
  V5BundleUpdate archive;
  archive.beginBoot(hardware, mainUpdate, zeroUpdate);
  const auto nvsBefore = FakeNvs::records;
  V5StableChannel channel;
  FakeOta::now = 45000;
  auto tick = [&] {
    channel.download.service(true, true, false);
    archive.service(millis(), true, true, false);
    channel.service(millis(), true, false, archive);
    ++FakeOta::now;
  };
  tick();
  for (unsigned i = 0; i < 300 && channel.busy(); ++i) tick();
  assert(!channel.busy());
  assert(channel.status == String("SD 안정 버전 저장 완료"));
  assert(FakeOta::beginCalls == 0 && FakeNvs::records == nvsBefore);
  assert(SD.exists(archive.stablePath(false) + "/firmware.bin"));
  assert(SD.exists(archive.stablePath(true) + "/firmware.bin"));
  const auto before = V5DownloadWorker::requests.size();
  FakeOta::now += 3600001;
  tick();
  for (unsigned i = 0; i < 50 && channel.busy(); ++i) tick();
  assert(channel.status == String("SD 안정 버전 최신"));
  assert(V5DownloadWorker::requests.size() == before + 2);
  const auto stablePath = archive.stablePath(false);
  V5DownloadWorker::fixtures["v5-stable.sig"] = {0};
  FakeOta::now += 3600001;
  tick();
  for (unsigned i = 0; i < 50 && channel.busy(); ++i) tick();
  assert(!channel.busy() && archive.stablePath(false) == stablePath);
  assert(FakeOta::beginCalls == 0 && FakeNvs::records == nvsBefore);
}
