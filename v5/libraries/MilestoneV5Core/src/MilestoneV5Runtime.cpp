#include "MilestoneV5Runtime.h"

namespace MilestoneV5 {

TaskPriority taskPriority(TaskKind task) {
  switch (task) {
  case TaskKind::kOtaDownload:
    return TaskPriority::kSafety;
  case TaskKind::kUserHttp:
    return TaskPriority::kUser;
  case TaskKind::kArtwork:
    return TaskPriority::kArtwork;
  case TaskKind::kNtp:
  case TaskKind::kUpdateCheck:
  case TaskKind::kMaintenance:
    return TaskPriority::kBackground;
  }
  return TaskPriority::kBackground;
}

TaskAssignment assignNetworkTask(TaskKind task, const RadioState &state) {
  TaskAssignment result = {Board::kNone, true, false};
  if (state.otaActive && task != TaskKind::kOtaDownload)
    return result;

  if (task == TaskKind::kOtaDownload) {
    if (state.zeroStaReady)
      return {Board::kZero, false, state.zeroBleActive};
    if (!state.mainPortalHasClient && state.mainStaReady)
      return {Board::kMain, false, false};
    return result;
  }

  if (state.mainPortalActive && state.mainPortalHasClient) {
    if (state.zeroStaReady &&
        (!state.zeroBleActive || task == TaskKind::kUserHttp)) {
      return {Board::kZero, false, false};
    }
    return result;
  }

  if (state.zeroStaReady && !state.zeroBleActive)
    return {Board::kZero, false, false};
  if (state.mainStaReady)
    return {Board::kMain, false, false};
  if (state.zeroStaReady && task == TaskKind::kUserHttp)
    return {Board::kZero, false, false};
  return result;
}

TaskLeaseController::TaskLeaseController()
    : active_(false), leaseId_(0), task_(TaskKind::kMaintenance),
      owner_(Board::kNone), deadlineMs_(0) {}

bool TaskLeaseController::acquire(uint32_t leaseId, TaskKind task, Board owner,
                                  uint32_t nowMs, uint32_t durationMs) {
  if (active_ || leaseId == 0 || owner == Board::kNone || durationMs == 0 ||
      durationMs >= 0x80000000UL)
    return false;
  active_ = true;
  leaseId_ = leaseId;
  task_ = task;
  owner_ = owner;
  deadlineMs_ = nowMs + durationMs;
  return true;
}

bool TaskLeaseController::renew(uint32_t leaseId, uint32_t nowMs,
                                uint32_t durationMs) {
  expireIfDue(nowMs);
  if (!active_ || leaseId != leaseId_ || durationMs == 0 ||
      durationMs >= 0x80000000UL) {
    return false;
  }
  deadlineMs_ = nowMs + durationMs;
  return true;
}

bool TaskLeaseController::release(uint32_t leaseId) {
  if (!active_ || leaseId != leaseId_)
    return false;
  active_ = false;
  owner_ = Board::kNone;
  return true;
}

bool TaskLeaseController::expireIfDue(uint32_t nowMs) {
  if (!active_ || static_cast<int32_t>(nowMs - deadlineMs_) < 0)
    return false;
  active_ = false;
  owner_ = Board::kNone;
  return true;
}
bool TaskLeaseController::active() const { return active_; }
uint32_t TaskLeaseController::leaseId() const { return leaseId_; }
TaskKind TaskLeaseController::task() const { return task_; }
Board TaskLeaseController::owner() const { return owner_; }

ProfileController::ProfileController(Profile initial)
    : active_(initial), target_(initial), state_(ProfileTransitionState::kIdle),
      persistencePending_(false) {}

bool ProfileController::request(Profile target) {
  if (target != Profile::kCore && target != Profile::kMedia &&
      target != Profile::kNow)
    return false;
  if (state_ != ProfileTransitionState::kIdle || target == active_)
    return false;
  target_ = target;
  state_ = ProfileTransitionState::kQuiescing;
  return true;
}

void ProfileController::notifyQuiesced() {
  if (state_ == ProfileTransitionState::kQuiescing) {
    state_ = ProfileTransitionState::kStarting;
  }
}

void ProfileController::notifyStarted(bool success) {
  if (state_ != ProfileTransitionState::kStarting)
    return;
  if (success) {
    active_ = target_;
    persistencePending_ = true;
  } else {
    target_ = active_;
  }
  state_ = ProfileTransitionState::kIdle;
}

Profile ProfileController::active() const { return active_; }
Profile ProfileController::target() const { return target_; }
ProfileTransitionState ProfileController::state() const { return state_; }
bool ProfileController::persistencePending() const {
  return persistencePending_;
}
void ProfileController::acknowledgePersisted() { persistencePending_ = false; }

bool faultRequiresFirmwareRollback(OptionalFault) { return false; }

} // namespace MilestoneV5
