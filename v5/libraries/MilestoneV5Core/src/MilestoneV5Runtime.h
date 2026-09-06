#pragma once

#include <stdint.h>

namespace MilestoneV5 {

enum class Board : uint8_t { kNone, kMain, kZero };
enum class Profile : uint8_t { kCore, kMedia, kNow };
enum class TaskKind : uint8_t {
  kNtp,
  kUserHttp,
  kArtwork,
  kUpdateCheck,
  kOtaDownload,
  kMaintenance,
};

enum class TaskPriority : uint8_t {
  kBackground = 1,
  kArtwork = 2,
  kBluetooth = 3,
  kStorage = 4,
  kUser = 5,
  kUi = 6,
  kSafety = 7,
};

struct RadioState {
  bool mainPortalActive;
  bool mainPortalHasClient;
  bool mainStaReady;
  bool zeroBleActive;
  bool zeroStaReady;
  bool otaActive;
};

struct TaskAssignment {
  Board board;
  bool defer;
  bool suspendZeroBle;
};

TaskPriority taskPriority(TaskKind task);
TaskAssignment assignNetworkTask(TaskKind task, const RadioState &state);

class TaskLeaseController {
public:
  TaskLeaseController();
  bool acquire(uint32_t leaseId, TaskKind task, Board owner, uint32_t nowMs,
               uint32_t durationMs);
  bool renew(uint32_t leaseId, uint32_t nowMs, uint32_t durationMs);
  bool release(uint32_t leaseId);
  bool expireIfDue(uint32_t nowMs);
  bool active() const;
  uint32_t leaseId() const;
  TaskKind task() const;
  Board owner() const;

private:
  bool active_;
  uint32_t leaseId_;
  TaskKind task_;
  Board owner_;
  uint32_t deadlineMs_;
};

enum class ProfileTransitionState : uint8_t { kIdle, kQuiescing, kStarting };

class ProfileController {
public:
  explicit ProfileController(Profile initial);

  bool request(Profile target);
  void notifyQuiesced();
  void notifyStarted(bool success);

  Profile active() const;
  Profile target() const;
  ProfileTransitionState state() const;
  bool persistencePending() const;
  void acknowledgePersisted();

private:
  Profile active_;
  Profile target_;
  ProfileTransitionState state_;
  bool persistencePending_;
};

enum class OptionalFault : uint8_t {
  kSdUnavailable,
  kRtcUnavailable,
  kEnvironmentSensorUnavailable,
  kZeroLinkUnavailable,
  kWifiUnavailable,
  kBluetoothUnavailable,
  kArtworkUnavailable,
  kMediaCorrupt,
};

bool faultRequiresFirmwareRollback(OptionalFault fault);

} // namespace MilestoneV5
