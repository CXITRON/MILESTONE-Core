#define MILESTONE_V5_RELEASE_PUBLIC_KEY "test-double-only"
#include "../v5/MilestoneV5Main/V5BundleDownload.h"
#include <SD.h>
#include <cassert>
#include <esp_ota_ops.h>
std::string hash(const std::vector<uint8_t> &data) {
  uint8_t d[32];
  mbedtls_sha256(data.data(), data.size(), d, 0);
  std::string out;
  const char *hex = "0123456789abcdef";
  for (auto c : d) {
    out += hex[c >> 4];
    out += hex[c & 15];
  }
  return out;
}
void fixture(const std::string &name, const std::string &data) {
  V5DownloadWorker::fixtures[name] =
      std::vector<uint8_t>(data.begin(), data.end());
}
void prepare() {
  std::vector<uint8_t> bin = {1, 2, 3, 4};
  auto sha = hash(bin);
  V5DownloadWorker::fixtures.clear();
  V5DownloadWorker::requests.clear();
  V5DownloadWorker::state = 0;
  fixture("v5-bundle.txt", "MILESTONE-V5 BUNDLE 5.0.0 " + sha + " NONE\n");
  V5DownloadWorker::fixtures["v5-bundle.sig"] = {42};
  fixture("v5-main-manifest.txt",
          "MILESTONE-V5 MAIN 5.0.0 4 " + sha + " 1 1\n");
  V5DownloadWorker::fixtures["v5-main-manifest.sig"] = {42};
  V5DownloadWorker::fixtures["v5-main.bin"] = bin;
}
void preparePaired(bool mismatchedZeroVersion) {
  prepare();
  std::vector<uint8_t> zeroBin = {5, 6, 7};
  const auto mainSha = hash(V5DownloadWorker::fixtures["v5-main.bin"]);
  const auto zeroSha = hash(zeroBin);
  fixture("v5-bundle.txt",
          "MILESTONE-V5 BUNDLE 5.0.0 " + mainSha + " " + zeroSha + "\n");
  fixture("v5-zero-manifest.txt",
          "MILESTONE-V5 ZERO " +
              std::string(mismatchedZeroVersion ? "5.0.1" : "5.0.0") + " 3 " +
              zeroSha + " 1 1\n");
  V5DownloadWorker::fixtures["v5-zero-manifest.sig"] = {42};
  V5DownloadWorker::fixtures["v5-zero.bin"] = zeroBin;
}
int main(int argc, char **argv) {
  (void)ESP;
  assert(argc == 2);
  FakeOta::reset();
  FakeSd::root = std::filesystem::path(argv[1]) / "download-sd";
  std::filesystem::create_directories(FakeSd::root / "firmware");
  prepare();
  V5BundleDownload job;
  assert(!job.begin("../bad", false));
  assert(job.begin("5.0.0", false));
  for (unsigned i = 0; i < 30 && job.active; ++i)
    job.service(true, true, false);
  assert(job.ready && !job.active);
  assert(SD.exists(job.directory + "/main/firmware.bin"));
  assert(FakeOta::beginCalls == 0);
  assert(V5DownloadWorker::requests.size() == 5);
  prepare();
  V5DownloadWorker::fixtures["v5-bundle.sig"] = {0};
  V5BundleDownload rejected;
  assert(rejected.begin("latest", false));
  for (unsigned i = 0; i < 30 && rejected.active; ++i)
    rejected.service(true, true, false);
  assert(!rejected.ready && !rejected.active);
  assert(!SD.exists(rejected.directory));
  assert(V5DownloadWorker::requests.size() == 2);
  prepare();
  V5DownloadWorker::fixtures["v5-main.bin"][0] ^= 1;
  V5BundleDownload corrupt;
  assert(corrupt.begin("latest", false));
  for (unsigned i = 0; i < 30 && corrupt.active; ++i)
    corrupt.service(true, true, false);
  assert(!corrupt.ready && !corrupt.active);
  preparePaired(true);
  V5BundleDownload mixedVersion;
  assert(mixedVersion.begin("latest", false));
  for (unsigned i = 0; i < 30 && mixedVersion.active; ++i)
    mixedVersion.service(true, true, false);
  assert(!mixedVersion.ready && !mixedVersion.active);
  assert(V5DownloadWorker::requests.size() == 6);
  preparePaired(false);
  V5BundleDownload paired;
  assert(paired.begin("latest", false));
  for (unsigned i = 0; i < 40 && paired.active; ++i)
    paired.service(true, true, false);
  assert(paired.ready && !paired.active);
  assert(V5DownloadWorker::requests.size() == 8);
  const String pairedDirectory = paired.directory;
  paired.discard();
  assert(!paired.ready && !SD.exists(pairedDirectory));
  prepare();
  V5BundleDownload unsafe;
  assert(unsafe.begin("latest", false));
  unsafe.service(true, false, false);
  assert(!unsafe.active && !unsafe.ready && V5DownloadWorker::cancel.load());
}
