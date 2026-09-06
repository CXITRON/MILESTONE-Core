#pragma once

#include <stdint.h>

namespace MilestoneV5 {

enum class UpdateTarget : uint8_t { kMain, kZero };

struct UpdateAssetMetadata {
  UpdateTarget target;
  uint32_t byteSize;
  const char *sha256Hex;
  bool signatureVerified;
  uint8_t minimumPeerProtocol;
  uint8_t maximumPeerProtocol;
};

bool validUpdateAsset(const UpdateAssetMetadata &asset,
                      UpdateTarget expectedTarget, uint8_t currentPeerProtocol);

class OrderedChunkTracker {
public:
  explicit OrderedChunkTracker(uint32_t totalBytes);
  bool accept(uint32_t offset, uint16_t length);
  bool complete() const;
  uint32_t receivedBytes() const;
  uint32_t totalBytes() const;

private:
  uint32_t totalBytes_;
  uint32_t receivedBytes_;
};

enum class UpdateStage : uint8_t {
  kIdle,
  kDownloadingBundle,
  kVerifyingBundle,
  kInstallingMain,
  kAwaitingMainSelfTest,
  kTransferringZero,
  kInstallingZero,
  kAwaitingZeroSelfTest,
  kPromotingStable,
  kComplete,
  kFailed,
};

enum class UpdateFailureAction : uint8_t {
  kNone,
  kKeepCurrent,
  kRollbackMain,
  kRollbackZeroKeepMain,
};

class CompanionUpdateController {
public:
  CompanionUpdateController();

  bool begin(bool zeroChanged);
  bool downloadFinished(bool success);
  bool bundleVerified(bool success);
  bool mainInstallFinished(bool success);
  bool mainSelfTestFinished(bool success);
  bool zeroTransferFinished(bool success);
  bool zeroInstallFinished(bool success);
  bool zeroSelfTestFinished(bool success);
  bool stablePromotionFinished(bool success);
  void reset();

  UpdateStage stage() const;
  UpdateFailureAction failureAction() const;
  bool zeroChanged() const;

private:
  void fail(UpdateFailureAction action);
  UpdateStage stage_;
  UpdateFailureAction failureAction_;
  bool zeroChanged_;
};

enum class RecoverySource : uint8_t {
  kCurrent,
  kPreviousOta,
  kInternalSafeMode,
  kSdStable,
  kSdRecovery,
  kUsb,
  kNone,
};

struct RecoveryAvailability {
  bool currentValid;
  bool previousOtaValid;
  bool safeModeValid;
  bool sdStableValid;
  bool sdRecoveryValid;
};

RecoverySource selectRecoverySource(const RecoveryAvailability &availability);

enum class BootCandidateDecision : uint8_t {
  kContinueTesting,
  kAccept,
  kRollback
};

BootCandidateDecision decideBootCandidate(bool essentialSelfTestComplete,
                                          bool essentialSelfTestPassed,
                                          bool deadlineExpired);

} // namespace MilestoneV5
