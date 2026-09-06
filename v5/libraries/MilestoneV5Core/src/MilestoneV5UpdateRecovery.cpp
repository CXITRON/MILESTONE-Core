#include "MilestoneV5UpdateRecovery.h"

#include <stddef.h>

namespace {
bool validSha256(const char *value) {
  if (value == nullptr)
    return false;
  for (size_t i = 0; i < 64; ++i) {
    const char ch = value[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
          (ch >= 'A' && ch <= 'F')))
      return false;
  }
  return value[64] == '\0';
}
} // namespace

namespace MilestoneV5 {

bool validUpdateAsset(const UpdateAssetMetadata &asset,
                      UpdateTarget expectedTarget,
                      uint8_t currentPeerProtocol) {
  return asset.target == expectedTarget && asset.byteSize != 0 &&
         validSha256(asset.sha256Hex) && asset.signatureVerified &&
         asset.minimumPeerProtocol <= asset.maximumPeerProtocol &&
         currentPeerProtocol >= asset.minimumPeerProtocol &&
         currentPeerProtocol <= asset.maximumPeerProtocol;
}

OrderedChunkTracker::OrderedChunkTracker(uint32_t totalBytes)
    : totalBytes_(totalBytes), receivedBytes_(0) {}
bool OrderedChunkTracker::accept(uint32_t offset, uint16_t length) {
  if (length == 0 || offset != receivedBytes_ || receivedBytes_ > totalBytes_ ||
      static_cast<uint32_t>(length) > totalBytes_ - receivedBytes_)
    return false;
  receivedBytes_ += length;
  return true;
}
bool OrderedChunkTracker::complete() const {
  return totalBytes_ != 0 && receivedBytes_ == totalBytes_;
}
uint32_t OrderedChunkTracker::receivedBytes() const { return receivedBytes_; }
uint32_t OrderedChunkTracker::totalBytes() const { return totalBytes_; }

CompanionUpdateController::CompanionUpdateController()
    : stage_(UpdateStage::kIdle), failureAction_(UpdateFailureAction::kNone),
      zeroChanged_(false) {}

bool CompanionUpdateController::begin(bool zeroChanged) {
  if (stage_ != UpdateStage::kIdle)
    return false;
  zeroChanged_ = zeroChanged;
  failureAction_ = UpdateFailureAction::kNone;
  stage_ = UpdateStage::kDownloadingBundle;
  return true;
}
void CompanionUpdateController::fail(UpdateFailureAction action) {
  stage_ = UpdateStage::kFailed;
  failureAction_ = action;
}
bool CompanionUpdateController::downloadFinished(bool success) {
  if (stage_ != UpdateStage::kDownloadingBundle)
    return false;
  if (success)
    stage_ = UpdateStage::kVerifyingBundle;
  else
    fail(UpdateFailureAction::kKeepCurrent);
  return true;
}
bool CompanionUpdateController::bundleVerified(bool success) {
  if (stage_ != UpdateStage::kVerifyingBundle)
    return false;
  if (success)
    stage_ = UpdateStage::kInstallingMain;
  else
    fail(UpdateFailureAction::kKeepCurrent);
  return true;
}
bool CompanionUpdateController::mainInstallFinished(bool success) {
  if (stage_ != UpdateStage::kInstallingMain)
    return false;
  if (success)
    stage_ = UpdateStage::kAwaitingMainSelfTest;
  else
    fail(UpdateFailureAction::kKeepCurrent);
  return true;
}
bool CompanionUpdateController::mainSelfTestFinished(bool success) {
  if (stage_ != UpdateStage::kAwaitingMainSelfTest)
    return false;
  if (!success)
    fail(UpdateFailureAction::kRollbackMain);
  else
    stage_ = zeroChanged_ ? UpdateStage::kTransferringZero
                          : UpdateStage::kPromotingStable;
  return true;
}
bool CompanionUpdateController::zeroTransferFinished(bool success) {
  if (stage_ != UpdateStage::kTransferringZero)
    return false;
  if (success)
    stage_ = UpdateStage::kInstallingZero;
  else
    fail(UpdateFailureAction::kRollbackZeroKeepMain);
  return true;
}
bool CompanionUpdateController::zeroInstallFinished(bool success) {
  if (stage_ != UpdateStage::kInstallingZero)
    return false;
  if (success)
    stage_ = UpdateStage::kAwaitingZeroSelfTest;
  else
    fail(UpdateFailureAction::kRollbackZeroKeepMain);
  return true;
}
bool CompanionUpdateController::zeroSelfTestFinished(bool success) {
  if (stage_ != UpdateStage::kAwaitingZeroSelfTest)
    return false;
  if (success)
    stage_ = UpdateStage::kPromotingStable;
  else
    fail(UpdateFailureAction::kRollbackZeroKeepMain);
  return true;
}
bool CompanionUpdateController::stablePromotionFinished(bool success) {
  if (stage_ != UpdateStage::kPromotingStable)
    return false;
  if (success)
    stage_ = UpdateStage::kComplete;
  else
    fail(UpdateFailureAction::kKeepCurrent);
  return true;
}
void CompanionUpdateController::reset() {
  stage_ = UpdateStage::kIdle;
  failureAction_ = UpdateFailureAction::kNone;
  zeroChanged_ = false;
}
UpdateStage CompanionUpdateController::stage() const { return stage_; }
UpdateFailureAction CompanionUpdateController::failureAction() const {
  return failureAction_;
}
bool CompanionUpdateController::zeroChanged() const { return zeroChanged_; }

RecoverySource selectRecoverySource(const RecoveryAvailability &availability) {
  if (availability.currentValid)
    return RecoverySource::kCurrent;
  if (availability.previousOtaValid)
    return RecoverySource::kPreviousOta;
  if (availability.safeModeValid)
    return RecoverySource::kInternalSafeMode;
  if (availability.sdStableValid)
    return RecoverySource::kSdStable;
  if (availability.sdRecoveryValid)
    return RecoverySource::kSdRecovery;
  return RecoverySource::kUsb;
}

BootCandidateDecision decideBootCandidate(bool essentialSelfTestComplete,
                                          bool essentialSelfTestPassed,
                                          bool deadlineExpired) {
  if (essentialSelfTestComplete) {
    return essentialSelfTestPassed ? BootCandidateDecision::kAccept
                                   : BootCandidateDecision::kRollback;
  }
  return deadlineExpired ? BootCandidateDecision::kRollback
                         : BootCandidateDecision::kContinueTesting;
}

} // namespace MilestoneV5
