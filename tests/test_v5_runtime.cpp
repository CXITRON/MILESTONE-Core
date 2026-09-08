#include "MilestoneV5Runtime.h"
#include "MilestoneV5ImageSize.h"
#include <cstring>

#include <iostream>

namespace {
int failures = 0;
#define EXPECT_TRUE(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " #value "\n"; ++failures; } } while (0)
#define EXPECT_EQ(actual, expected) do { if (!((actual) == (expected))) { std::cerr << "FAIL " << __LINE__ << ": " #actual " != " #expected "\n"; ++failures; } } while (0)

void testRadioBroker() {
  MilestoneV5::RadioState state = {true, true, false, false, true, false};
  auto assignment = MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kArtwork, state);
  EXPECT_EQ(assignment.board, MilestoneV5::Board::kZero);
  EXPECT_TRUE(!assignment.defer);

  state.zeroBleActive = true;
  assignment = MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kArtwork, state);
  EXPECT_TRUE(assignment.defer);
  assignment = MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kUserHttp, state);
  EXPECT_EQ(assignment.board, MilestoneV5::Board::kZero);

  state = {false, false, true, true, true, false};
  assignment = MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kNtp, state);
  EXPECT_EQ(assignment.board, MilestoneV5::Board::kMain);

  assignment = MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kOtaDownload, state);
  EXPECT_EQ(assignment.board, MilestoneV5::Board::kZero);
  EXPECT_TRUE(assignment.suspendZeroBle);
  state={true,false,true,true,true,false};
  assignment=MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kArtwork,state);
  EXPECT_EQ(assignment.board,MilestoneV5::Board::kMain);
  state.mainPortalHasClient=true;
  assignment=MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kArtwork,state);
  EXPECT_TRUE(assignment.defer);
  state.otaActive=true;
  EXPECT_TRUE(MilestoneV5::assignNetworkTask(MilestoneV5::TaskKind::kUserHttp,state).defer);
}

void testProfileController() {
  MilestoneV5::ProfileController controller(MilestoneV5::Profile::kCore);
  EXPECT_TRUE(!controller.request(MilestoneV5::Profile::kCore));
  EXPECT_TRUE(controller.request(MilestoneV5::Profile::kMedia));
  EXPECT_EQ(controller.state(), MilestoneV5::ProfileTransitionState::kQuiescing);
  EXPECT_TRUE(!controller.request(MilestoneV5::Profile::kNow));
  controller.notifyQuiesced();
  EXPECT_EQ(controller.state(), MilestoneV5::ProfileTransitionState::kStarting);
  controller.notifyStarted(true);
  EXPECT_EQ(controller.active(), MilestoneV5::Profile::kMedia);
  EXPECT_TRUE(controller.persistencePending());
  controller.acknowledgePersisted();
  EXPECT_TRUE(!controller.persistencePending());

  EXPECT_TRUE(controller.request(MilestoneV5::Profile::kNow));
  controller.notifyQuiesced();
  controller.notifyStarted(false);
  EXPECT_EQ(controller.active(), MilestoneV5::Profile::kMedia);
  EXPECT_EQ(controller.target(), MilestoneV5::Profile::kMedia);
}

void testTaskLease() {
  MilestoneV5::TaskLeaseController lease;
  EXPECT_TRUE(!lease.acquire(0, MilestoneV5::TaskKind::kNtp,
                             MilestoneV5::Board::kZero, 0, 100));
  EXPECT_TRUE(lease.acquire(7, MilestoneV5::TaskKind::kArtwork,
                            MilestoneV5::Board::kZero, 0xFFFFFFF0UL, 100));
  EXPECT_TRUE(!lease.expireIfDue(20));
  EXPECT_TRUE(lease.renew(7, 20, 200));
  EXPECT_TRUE(!lease.release(8));
  EXPECT_TRUE(!lease.expireIfDue(219));
  EXPECT_TRUE(lease.expireIfDue(220));
  EXPECT_TRUE(!lease.active());
  EXPECT_TRUE(lease.acquire(9,MilestoneV5::TaskKind::kNtp,MilestoneV5::Board::kMain,300,50));
  EXPECT_TRUE(!lease.renew(9,350,100));
  EXPECT_TRUE(!lease.active());
}

void testOptionalFaultIsolation() {
  EXPECT_TRUE(!MilestoneV5::faultRequiresFirmwareRollback(
      MilestoneV5::OptionalFault::kSdUnavailable));
  EXPECT_TRUE(!MilestoneV5::faultRequiresFirmwareRollback(
      MilestoneV5::OptionalFault::kZeroLinkUnavailable));
  EXPECT_TRUE(!MilestoneV5::faultRequiresFirmwareRollback(
      MilestoneV5::OptionalFault::kMediaCorrupt));
}

void testEnabledViewSelection() {
  EXPECT_EQ(MilestoneV5::nextEnabledIndex(0, 0x24, 7, 1), 2);
  EXPECT_EQ(MilestoneV5::nextEnabledIndex(2, 0x24, 7, 1), 5);
  EXPECT_EQ(MilestoneV5::nextEnabledIndex(2, 0x24, 7, -1), 5);
  EXPECT_EQ(MilestoneV5::nextEnabledIndex(5, 0x24, 7, -1), 2);
  EXPECT_EQ(MilestoneV5::nextEnabledIndex(3, 0x08, 7, 1), 3);
  EXPECT_EQ(MilestoneV5::nextEnabledIndex(3, 0, 7, 1), 3);
}
}  // namespace

int main() {
  uint8_t image[96] = {};
  image[0] = 0xe9; image[1] = 1; image[23] = 1; image[28] = 16;
  unsigned reads = 0;
  auto reader = [&](uint32_t offset, uint8_t *out, size_t size) {
    ++reads;
    if (offset + size > sizeof(image)) return false;
    memcpy(out, image + offset, size); return true;
  };
  EXPECT_EQ(MilestoneV5::displayImageSize(sizeof(image), reader), 96U);
  EXPECT_EQ(reads, 2U);
  image[31] = 0xff;
  EXPECT_EQ(MilestoneV5::displayImageSize(sizeof(image), reader), 0U);
  image[1] = 17;
  EXPECT_EQ(MilestoneV5::displayImageSize(sizeof(image), reader), 0U);
  testRadioBroker();
  testProfileController();
  testTaskLease();
  testOptionalFaultIsolation();
  testEnabledViewSelection();
  if (failures != 0) return 1;
  std::cout << "MILESTONE v5 runtime tests passed\n";
  return 0;
}
