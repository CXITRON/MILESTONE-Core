#include "MilestoneV5Link.h"

namespace MilestoneV5 {

RetryController::RetryController(uint8_t maximumAttempts,
                                 uint32_t retryIntervalMs)
    : maximumAttempts_(maximumAttempts == 0 ? 1 : maximumAttempts),
      retryIntervalMs_(retryIntervalMs), pending_(false),
      initialSendPending_(false), sequence_(0), lastAttemptMs_(0),
      attempts_(0) {}

bool RetryController::start(uint32_t sequence, uint32_t nowMs) {
  if (pending_)
    return false;
  pending_ = true;
  initialSendPending_ = true;
  sequence_ = sequence;
  lastAttemptMs_ = nowMs;
  attempts_ = 0;
  return true;
}

RetryAction RetryController::poll(uint32_t nowMs) {
  if (!pending_)
    return RetryAction::kNone;
  if (initialSendPending_) {
    initialSendPending_ = false;
    lastAttemptMs_ = nowMs;
    attempts_ = 1;
    return RetryAction::kSend;
  }
  if (nowMs - lastAttemptMs_ < retryIntervalMs_)
    return RetryAction::kNone;
  if (attempts_ >= maximumAttempts_) {
    pending_ = false;
    return RetryAction::kFailed;
  }
  ++attempts_;
  lastAttemptMs_ = nowMs;
  return RetryAction::kRetry;
}

bool RetryController::acknowledge(uint32_t ackSequence) {
  if (!pending_ || initialSendPending_ || ackSequence != sequence_)
    return false;
  pending_ = false;
  initialSendPending_ = false;
  return true;
}

void RetryController::cancel() {
  pending_ = false;
  initialSendPending_ = false;
}

bool RetryController::pending() const { return pending_; }
uint32_t RetryController::sequence() const { return sequence_; }
uint8_t RetryController::attempts() const { return attempts_; }

HeartbeatMonitor::HeartbeatMonitor(uint32_t staleAfterMs)
    : staleAfterMs_(staleAfterMs), lastReceiveMs_(0), received_(false) {}

void HeartbeatMonitor::noteReceive(uint32_t nowMs) {
  lastReceiveMs_ = nowMs;
  received_ = true;
}

void HeartbeatMonitor::reset() {
  lastReceiveMs_ = 0;
  received_ = false;
}

bool HeartbeatMonitor::hasReceived() const { return received_; }

bool HeartbeatMonitor::stale(uint32_t nowMs) const {
  return received_ && nowMs - lastReceiveMs_ > staleAfterMs_;
}

} // namespace MilestoneV5
