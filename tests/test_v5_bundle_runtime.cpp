#define MILESTONE_V5_RELEASE_PUBLIC_KEY "test-double-only"
#include <Preferences.h>
#include <SD.h>
#include <esp_ota_ops.h>
struct V5Hardware {
  bool sdMounted = true;
};
#define MILESTONE_V5_HARDWARE_DECLARED 1
#include "../v5/MilestoneV5Main/V5BundleUpdate.h"
#include <cassert>
#include <vector>

std::string hash(const std::vector<uint8_t> &data) {
  uint8_t digest[32];
  mbedtls_sha256(data.data(), data.size(), digest, 0);
  const char *hex = "0123456789abcdef";
  std::string result;
  for (auto c : digest) {
    result += hex[c >> 4];
    result += hex[c & 15];
  }
  return result;
}
void write(const std::string &path, const std::vector<uint8_t> &data) {
  auto p = FakeSd::root / std::filesystem::path(path).relative_path();
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(data.data()), data.size());
}
void text(const std::string &path, const std::string &data) {
  write(path, std::vector<uint8_t>(data.begin(), data.end()));
}
void prepare(bool zero) {
  const std::vector<uint8_t> mainImage = {1, 2, 3, 4, 5, 6},
                             zeroImage = {7, 8, 9, 10};
  const std::string mainHash = hash(mainImage), zeroHash = hash(zeroImage);
  text("/firmware/incoming/bundle.txt", "MILESTONE-V5 BUNDLE 5.0.0 " +
                                            mainHash + " " +
                                            (zero ? zeroHash : "NONE") + "\n");
  write("/firmware/incoming/bundle.sig", {42});
  text("/firmware/incoming/main/manifest.txt",
       "MILESTONE-V5 MAIN 5.0.0 6 " + mainHash + " 1 1\n");
  write("/firmware/incoming/main/manifest.sig", {42});
  write("/firmware/incoming/main/firmware.bin", mainImage);
  if (zero) {
    text("/firmware/incoming/zero/manifest.txt",
         "MILESTONE-V5 ZERO 5.0.0 4 " + zeroHash + " 1 1\n");
    write("/firmware/incoming/zero/manifest.sig", {42});
    write("/firmware/incoming/zero/firmware.bin", zeroImage);
  }
}
void installMain(V5BundleUpdate &bundle, V5SdUpdate &main) {
  for (unsigned i = 0; i < 100 && main.state != V5SdUpdate::State::Ready; ++i) {
    bundle.service(millis(), true, true, true);
    main.service();
    ++FakeOta::now;
  }
  assert(main.state == V5SdUpdate::State::Ready);
  assert(FakeOta::selected == 0x20000);
}
int main(int argc, char **argv) {
  (void)ESP;
  assert(argc == 2);
  FakeSd::root = std::filesystem::path(argv[1]) / "bundle-runtime-sd";
  std::filesystem::create_directories(FakeSd::root);
  FakeOta::reset();
  FakeNvs::records.clear();
  prepare(true);
  MilestoneV5::StableIndex previous{};
  previous.generation = 5;
  strcpy(previous.mainStable, "aaaaaaaaaaaaaaaa");
  strcpy(previous.zeroStable, "bbbbbbbbbbbbbbbb");
  uint8_t previousBytes[MilestoneV5::kStableIndexSize];
  assert(MilestoneV5::encodeStableIndex(previous, previousBytes,
                                        sizeof(previousBytes)));
  write("/firmware/index-a",
        std::vector<uint8_t>(previousBytes,
                             previousBytes + sizeof(previousBytes)));
  V5Hardware hardware;
  V5SdUpdate main;
  V5ZeroUpdate zero;
  V5BundleUpdate bundle;
  bundle.beginBoot(hardware, main, zero);
  assert(bundle.start());
  assert(FakeOta::beginCalls == 0);
  assert(zero.state == V5ZeroUpdate::State::Idle);
  installMain(bundle, main);
  assert(zero.state == V5ZeroUpdate::State::Idle);
  // Restart into the new MAIN. ZERO must remain untouched until MAIN
  // acceptance.
  FakeOta::running = 1;
  V5SdUpdate resumedMain;
  V5ZeroUpdate resumedZero;
  V5BundleUpdate resumed;
  resumed.beginBoot(hardware, resumedMain, resumedZero);
  assert(!resumed.candidateReady);
  for (unsigned i = 0; i < 5; ++i) {
    resumed.service(millis(), true, false, true);
    ++FakeOta::now;
  }
  assert(resumed.candidateReady);
  assert(resumedZero.state == V5ZeroUpdate::State::Idle);
  resumed.service(millis(), true, true, true);
  resumed.service(millis(), true, true, true);
  assert(resumed.phase == V5BundleUpdate::Phase::UpdatingZero);
  assert(resumedZero.state == V5ZeroUpdate::State::Hashing);
  // A receiver-confirmed completion is the coordinator's external event.
  resumedZero.state = V5ZeroUpdate::State::Done;
  resumed.service(millis(), true, true, true);
  assert(resumed.phase == V5BundleUpdate::Phase::StabilityHold);
  assert(!SD.exists("/firmware/index-b"));
  assert(SD.exists("/firmware/index-a"));
  // Missing/unhealthy ZERO resets the hold, even after the nominal interval.
  FakeOta::now += 600001;
  resumed.service(millis(), true, true, false);
  assert(resumed.phase == V5BundleUpdate::Phase::StabilityHold);
  assert(!SD.exists("/firmware/index-b"));
  FakeOta::now += 599999;
  resumed.service(millis(), true, true, true);
  assert(resumed.phase == V5BundleUpdate::Phase::StabilityHold);
  assert(!SD.exists("/firmware/index-b"));
  FakeOta::now += 2;
  FakeSd::failIndexRename = true;
  resumed.service(millis(), true, true, true);
  assert(resumed.phase == V5BundleUpdate::Phase::StabilityHold);
  assert(!SD.exists("/firmware/index-b"));
  FakeSd::failIndexRename = false;
  FakeOta::now += 60001;
  resumed.service(millis(), true, true, true);
  assert(resumed.phase == V5BundleUpdate::Phase::Complete);
  assert(SD.exists("/firmware/index-b"));
  File indexFile = SD.open("/firmware/index-b", FILE_READ);
  uint8_t published[MilestoneV5::kStableIndexSize];
  assert(indexFile.read(published, sizeof(published)) == sizeof(published));
  indexFile.close();
  MilestoneV5::StableIndex current{};
  assert(MilestoneV5::decodeStableIndex(published, sizeof(published), current));
  assert(!strcmp(current.mainBackup, "aaaaaaaaaaaaaaaa") &&
         !strcmp(current.zeroBackup, "bbbbbbbbbbbbbbbb"));
  auto mainStable = std::string(resumed.stablePath(false).c_str());
  auto zeroStable = std::string(resumed.stablePath(true).c_str());
  assert(mainStable.find("/firmware/sets/") == 0 &&
         zeroStable.find("/firmware/sets/") == 0);
  // A bad bundle is refused before a new flash transaction.
  const unsigned begins = FakeOta::beginCalls;
  write("/firmware/incoming/bundle.sig", {0});
  assert(!resumed.start());
  assert(FakeOta::beginCalls == begins);
  // Power loss before MAIN activation leaves the old MAIN and does not start
  // ZERO.
  FakeNvs::records.clear();
  FakeOta::reset();
  prepare(true);
  V5SdUpdate pendingMain;
  V5ZeroUpdate pendingZero;
  V5BundleUpdate pending;
  pending.beginBoot(hardware, pendingMain, pendingZero);
  assert(pending.start());
  for (unsigned i = 0;
       i < 100 && pending.phase != V5BundleUpdate::Phase::InstallingMain; ++i)
    pending.service(millis(), true, true, true);
  assert(pending.phase == V5BundleUpdate::Phase::InstallingMain);
  V5SdUpdate rollbackMain;
  V5ZeroUpdate rollbackZero;
  V5BundleUpdate rollback;
  rollback.beginBoot(hardware, rollbackMain, rollbackZero);
  assert(rollback.phase == V5BundleUpdate::Phase::Failed);
  assert(rollbackZero.state == V5ZeroUpdate::State::Idle);
  assert(FakeOta::selected == 0);
  // MAIN-only bundles do not acquire a ZERO transaction.
  FakeSd::root = std::filesystem::path(argv[1]) / "bundle-main-only";
  std::filesystem::create_directories(FakeSd::root);
  FakeOta::reset();
  FakeNvs::records.clear();
  prepare(false);
  V5SdUpdate soloMain;
  V5ZeroUpdate soloZero;
  V5BundleUpdate solo;
  solo.beginBoot(hardware, soloMain, soloZero);
  assert(solo.start());
  installMain(solo, soloMain);
  FakeOta::running = 1;
  V5SdUpdate soloResumedMain;
  V5ZeroUpdate soloResumedZero;
  V5BundleUpdate soloResumed;
  soloResumed.beginBoot(hardware, soloResumedMain, soloResumedZero);
  for (unsigned i = 0; i < 10; ++i)
    soloResumed.service(millis(), true, true, false);
  assert(soloResumed.phase == V5BundleUpdate::Phase::StabilityHold);
  assert(soloResumedZero.state == V5ZeroUpdate::State::Idle);

  // Reset during the first SD staging copy is journaled before either Flash
  // transaction. Rebooting the old MAIN records a failed update and never
  // starts ZERO or publishes the incomplete set.
  FakeSd::root = std::filesystem::path(argv[1]) / "bundle-copy-reset";
  std::filesystem::create_directories(FakeSd::root);
  FakeOta::reset();
  FakeNvs::records.clear();
  prepare(true);
  V5SdUpdate copyingMain;
  V5ZeroUpdate copyingZero;
  V5BundleUpdate copying;
  copying.beginBoot(hardware, copyingMain, copyingZero);
  assert(copying.start());
  assert(copying.phase == V5BundleUpdate::Phase::Copying);
  V5SdUpdate interruptedMain;
  V5ZeroUpdate interruptedZero;
  V5BundleUpdate interrupted;
  interrupted.beginBoot(hardware, interruptedMain, interruptedZero);
  assert(interrupted.phase == V5BundleUpdate::Phase::Failed);
  assert(interruptedZero.state == V5ZeroUpdate::State::Idle);

  // A signed peer manifest from a different release cannot be mixed into the
  // bundle even when its exact image hash appears in the bundle descriptor.
  FakeSd::root = std::filesystem::path(argv[1]) / "bundle-mixed-version";
  std::filesystem::create_directories(FakeSd::root);
  FakeOta::reset();
  FakeNvs::records.clear();
  prepare(true);
  const std::vector<uint8_t> zeroImage = {7, 8, 9, 10};
  text("/firmware/incoming/zero/manifest.txt",
       "MILESTONE-V5 ZERO 5.0.1 4 " + hash(zeroImage) + " 1 1\n");
  V5SdUpdate mixedMain;
  V5ZeroUpdate mixedZero;
  V5BundleUpdate mixed;
  mixed.beginBoot(hardware, mixedMain, mixedZero);
  assert(!mixed.start());
  assert(FakeOta::beginCalls == 0);
  assert(mixedZero.state == V5ZeroUpdate::State::Idle);
}
