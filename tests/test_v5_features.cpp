#include "MilestoneV5Features.h"
#include "MilestoneV5Settings.h"
#include "MilestoneV5UpdateRecovery.h"

#include <cmath>
#include <iostream>

namespace {
int failures = 0;
#define EXPECT_TRUE(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " #value "\n"; ++failures; } } while (0)
#define EXPECT_EQ(actual, expected) do { if (!((actual) == (expected))) { std::cerr << "FAIL " << __LINE__ << ": " #actual " != " #expected "\n"; ++failures; } } while (0)
#define EXPECT_NEAR(actual, expected, tolerance) do { if (std::fabs((actual) - (expected)) > (tolerance)) { std::cerr << "FAIL " << __LINE__ << ": " #actual " not near " #expected "\n"; ++failures; } } while (0)

void testEnvironment() {
  const auto aht = MilestoneV5::detectEnvironmentSensor(0x38);
  EXPECT_EQ(aht.model, MilestoneV5::EnvironmentModel::kAht20);
  EXPECT_TRUE(aht.temperature);
  EXPECT_TRUE(aht.humidity);
  EXPECT_TRUE(!aht.pressure);
  MilestoneV5::EnvironmentTracker ahtTracker(5000, {1.0f, -2.0f, 99.0f});
  EXPECT_TRUE(ahtTracker.accept({20.0f, 50.0f, 0.0f, true}, aht, 100));
  EXPECT_NEAR(ahtTracker.filtered().temperatureC, 21.0f, 0.001f);
  EXPECT_NEAR(ahtTracker.filtered().humidityPercent, 48.0f, 0.001f);
  EXPECT_NEAR(ahtTracker.filtered().pressureHpa, 0.0f, 0.001f);

  const auto bme = MilestoneV5::detectEnvironmentSensor(0x60);
  EXPECT_EQ(bme.model, MilestoneV5::EnvironmentModel::kBme280);
  EXPECT_TRUE(bme.humidity);
  const auto bmp = MilestoneV5::detectEnvironmentSensor(0x58);
  EXPECT_TRUE(!bmp.humidity);

  MilestoneV5::EnvironmentTracker tracker(5000, {1.0f, -2.0f, 3.0f});
  EXPECT_TRUE(tracker.accept({20.0f, 50.0f, 1000.0f, true}, bme, 100));
  EXPECT_NEAR(tracker.filtered().temperatureC, 21.0f, 0.001f);
  EXPECT_NEAR(tracker.filtered().humidityPercent, 48.0f, 0.001f);
  EXPECT_TRUE(tracker.accept({24.0f, 54.0f, 1004.0f, true}, bme, 200));
  EXPECT_NEAR(tracker.filtered().temperatureC, 22.0f, 0.001f);
  EXPECT_TRUE(!tracker.stale(5200));
  EXPECT_TRUE(tracker.stale(5201));
  EXPECT_TRUE(!tracker.accept({200.0f, 50.0f, 1000.0f, true}, bme, 300));
  EXPECT_EQ(tracker.errorCount(), 1U);
}

void testMenuAndMedia() {
  EXPECT_EQ(MilestoneV5::centeredTextX(128, 28), 50);
  EXPECT_EQ(MilestoneV5::centeredTextX(128, 27), 50);
  EXPECT_EQ(MilestoneV5::centeredTextX(128, 28, 1), 51);
  EXPECT_EQ(MilestoneV5::centeredTextX(128, 200), 0);

  MilestoneV5::ModeMenu menu;
  menu.open(MilestoneV5::Profile::kMedia);
  EXPECT_EQ(menu.selected(), MilestoneV5::ModeMenuItem::kMedia);
  menu.move(-1);
  EXPECT_EQ(menu.selected(), MilestoneV5::ModeMenuItem::kCore);
  menu.move(-1);
  EXPECT_EQ(menu.selected(), MilestoneV5::ModeMenuItem::kExit);
  menu.move(1);
  EXPECT_EQ(menu.selected(), MilestoneV5::ModeMenuItem::kCore);
  menu.move(1);
  menu.move(1);
  menu.move(1);
  EXPECT_EQ(menu.selected(), MilestoneV5::ModeMenuItem::kSetup);

  MilestoneV5::MediaBrowser media;
  media.setItemCount(3);
  EXPECT_TRUE(media.move(-1));
  EXPECT_EQ(media.selectedIndex(), 2U);
  EXPECT_TRUE(media.playSelected());
  EXPECT_TRUE(media.togglePause());
  EXPECT_EQ(media.state(), MilestoneV5::MediaState::kPaused);
  media.isolateCurrentCorruptItem();
  EXPECT_EQ(media.itemCount(), 2U);
  EXPECT_EQ(media.selectedIndex(), 0U);
  EXPECT_EQ(media.state(), MilestoneV5::MediaState::kBrowsing);
}

void testArtworkAndAtomicStorage() {
  const MilestoneV5::ArtworkCacheEntry entries[] = {
      {3, 100, 20, MilestoneV5::ArtworkState::kAuto},
      {2, 100, 10, MilestoneV5::ArtworkState::kCustom},
      {1, 100, 20, MilestoneV5::ArtworkState::kAuto},
      {4, 100, 5, MilestoneV5::ArtworkState::kBlocked},
  };
  EXPECT_EQ(MilestoneV5::selectArtworkLruEviction(entries, 4), 2);
  EXPECT_TRUE(!MilestoneV5::artworkMayBeEvicted(MilestoneV5::ArtworkState::kCustom));
  EXPECT_TRUE(!MilestoneV5::artworkMayUseServer(MilestoneV5::ArtworkState::kBlocked));
  EXPECT_TRUE(MilestoneV5::safeStorageLeafName("album-01.jpg"));
  EXPECT_TRUE(!MilestoneV5::safeStorageLeafName("../firmware.bin"));
  EXPECT_TRUE(!MilestoneV5::safeStorageLeafName("dir/file.jpg"));

  MilestoneV5::AtomicWriteController write;
  EXPECT_TRUE(write.begin());
  EXPECT_TRUE(write.temporaryWriteFinished(true));
  EXPECT_TRUE(write.validationFinished(true));
  EXPECT_TRUE(write.commitFinished(true));
  EXPECT_EQ(write.state(), MilestoneV5::AtomicWriteState::kCommitted);
  write.reset();
  EXPECT_TRUE(write.begin());
  EXPECT_TRUE(write.temporaryWriteFinished(false));
  EXPECT_EQ(write.state(), MilestoneV5::AtomicWriteState::kFailed);
}

void testUpdateAndRecovery() {
  const char sha[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  EXPECT_TRUE(MilestoneV5::validUpdateAsset(
      {MilestoneV5::UpdateTarget::kMain, 1024, sha, true, 1, 2},
      MilestoneV5::UpdateTarget::kMain, 1));
  EXPECT_TRUE(!MilestoneV5::validUpdateAsset(
      {MilestoneV5::UpdateTarget::kZero, 1024, sha, true, 1, 2},
      MilestoneV5::UpdateTarget::kMain, 1));
  MilestoneV5::OrderedChunkTracker chunks(1000);
  EXPECT_TRUE(chunks.accept(0, 476));
  EXPECT_TRUE(!chunks.accept(477, 100));
  EXPECT_TRUE(chunks.accept(476, 476));
  EXPECT_TRUE(chunks.accept(952, 48));
  EXPECT_TRUE(chunks.complete());

  MilestoneV5::CompanionUpdateController update;
  EXPECT_TRUE(update.begin(true));
  EXPECT_TRUE(update.downloadFinished(true));
  EXPECT_TRUE(update.bundleVerified(true));
  EXPECT_TRUE(update.mainInstallFinished(true));
  EXPECT_TRUE(update.mainSelfTestFinished(true));
  EXPECT_EQ(update.stage(), MilestoneV5::UpdateStage::kTransferringZero);
  EXPECT_TRUE(update.zeroTransferFinished(true));
  EXPECT_TRUE(update.zeroInstallFinished(true));
  EXPECT_TRUE(update.zeroSelfTestFinished(false));
  EXPECT_EQ(update.failureAction(), MilestoneV5::UpdateFailureAction::kRollbackZeroKeepMain);

  update.reset();
  EXPECT_TRUE(update.begin(false));
  EXPECT_TRUE(update.downloadFinished(true));
  EXPECT_TRUE(update.bundleVerified(true));
  EXPECT_TRUE(update.mainInstallFinished(true));
  EXPECT_TRUE(update.mainSelfTestFinished(true));
  EXPECT_EQ(update.stage(), MilestoneV5::UpdateStage::kPromotingStable);
  EXPECT_TRUE(update.stablePromotionFinished(true));
  EXPECT_EQ(update.stage(), MilestoneV5::UpdateStage::kComplete);

  EXPECT_EQ(MilestoneV5::selectRecoverySource({false, false, true, true, true}),
            MilestoneV5::RecoverySource::kInternalSafeMode);
  EXPECT_EQ(MilestoneV5::selectRecoverySource({false, false, false, false, false}),
            MilestoneV5::RecoverySource::kUsb);
  EXPECT_EQ(MilestoneV5::decideBootCandidate(false, false, false),
            MilestoneV5::BootCandidateDecision::kContinueTesting);
  EXPECT_EQ(MilestoneV5::decideBootCandidate(false, false, true),
            MilestoneV5::BootCandidateDecision::kRollback);
}

void testSettings() {
  auto settings = MilestoneV5::defaultRuntimeSettings();
  EXPECT_TRUE(MilestoneV5::validRuntimeSettings(settings));
  settings.display.luminancePercent = 49;
  EXPECT_TRUE(!MilestoneV5::validRuntimeSettings(settings));
  settings = MilestoneV5::defaultRuntimeSettings();
  settings.environment.logIntervalMs = 1000;
  EXPECT_TRUE(!MilestoneV5::validRuntimeSettings(settings));
}
}  // namespace

int main() {
  testEnvironment();
  testMenuAndMedia();
  testArtworkAndAtomicStorage();
  testUpdateAndRecovery();
  testSettings();
  if (failures != 0) return 1;
  std::cout << "MILESTONE v5 feature tests passed\n";
  return 0;
}
