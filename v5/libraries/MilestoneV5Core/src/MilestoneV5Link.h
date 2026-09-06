#pragma once

#include <stdint.h>

namespace MilestoneV5 {

enum class RetryAction : uint8_t { kNone, kSend, kRetry, kFailed };

class RetryController {
public:
  RetryController(uint8_t maximumAttempts, uint32_t retryIntervalMs);

  bool start(uint32_t sequence, uint32_t nowMs);
  RetryAction poll(uint32_t nowMs);
  bool acknowledge(uint32_t ackSequence);
  void cancel();

  bool pending() const;
  uint32_t sequence() const;
  uint8_t attempts() const;

private:
  uint8_t maximumAttempts_;
  uint32_t retryIntervalMs_;
  bool pending_;
  bool initialSendPending_;
  uint32_t sequence_;
  uint32_t lastAttemptMs_;
  uint8_t attempts_;
};

class HeartbeatMonitor {
public:
  explicit HeartbeatMonitor(uint32_t staleAfterMs);

  void noteReceive(uint32_t nowMs);
  void reset();
  bool hasReceived() const;
  bool stale(uint32_t nowMs) const;

private:
  uint32_t staleAfterMs_;
  uint32_t lastReceiveMs_;
  bool received_;
};

} // namespace MilestoneV5
