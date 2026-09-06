#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MilestoneV5 {

constexpr uint32_t kProtocolMagic = 0x35534C4DUL; // "MLS5" on the wire.
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kFrameHeaderSize = 32;
constexpr size_t kMaxPayloadSize = 4096;

enum class MessageType : uint16_t {
  kInvalid = 0,
  kHello = 1,
  kHelloReply = 2,
  kHeartbeat = 3,
  kAck = 4,
  kStatus = 5,
  kTaskRequest = 16,
  kTaskResult = 17,
  kAmsMetadata = 32,
  kArtworkChunk = 33,
  kOtaChunk = 48,
  kOtaControl = 49,
};

enum FrameFlags : uint16_t {
  kFlagAckRequired = 1U << 0,
  kFlagResponse = 1U << 1,
  kFlagMore = 1U << 2,
  kFlagRetry = 1U << 3,
  kFlagError = 1U << 4,
};

enum class DecodeStatus : uint8_t {
  kOk,
  kNullArgument,
  kTooShort,
  kBadMagic,
  kUnsupportedVersion,
  kBadHeaderSize,
  kPayloadTooLarge,
  kLengthMismatch,
  kBadHeaderCrc,
  kBadPayloadCrc,
  kInvalidType,
};

struct FrameFields {
  MessageType type;
  uint16_t flags;
  uint32_t leaseId;
  uint32_t sequence;
  uint32_t ackSequence;
};

struct DecodedFrame {
  FrameFields fields;
  const uint8_t *payload;
  uint16_t payloadLength;
};

uint32_t crc32(const uint8_t *data, size_t length);

size_t encodedFrameSize(size_t payloadLength);

bool encodeFrame(const FrameFields &fields, const uint8_t *payload,
                 uint16_t payloadLength, uint8_t *output, size_t outputCapacity,
                 size_t &outputLength);

DecodeStatus decodeFrame(const uint8_t *frame, size_t frameLength,
                         DecodedFrame &decoded);

const char *decodeStatusName(DecodeStatus status);

enum CapabilityFlags : uint32_t {
  kCapabilityBleAms = 1UL << 0,
  kCapabilityWifiSta = 1UL << 1,
  kCapabilityInternetHttp = 1UL << 2,
  kCapabilityCompanionOta = 1UL << 3,
  kCapabilityPsram = 1UL << 4,
};

struct HelloPayload {
  uint8_t minimumVersion;
  uint8_t maximumVersion;
  uint8_t board;
  uint32_t capabilities;
  uint32_t bootId;
};

constexpr size_t kHelloPayloadSize = 12;
bool encodeHelloPayload(const HelloPayload &hello, uint8_t *output,
                        size_t capacity);
bool decodeHelloPayload(const uint8_t *payload, size_t length,
                        HelloPayload &hello);
uint8_t negotiateProtocolVersion(uint8_t localMinimum, uint8_t localMaximum,
                                 uint8_t peerMinimum, uint8_t peerMaximum);

struct StatusPayload {
  uint16_t stateFlags;
  int16_t temperatureCenti;
  uint32_t freeHeap;
  uint32_t freePsram;
};

constexpr size_t kStatusPayloadSize = 12;
bool encodeStatusPayload(const StatusPayload &status, uint8_t *output,
                         size_t capacity);
bool decodeStatusPayload(const uint8_t *payload, size_t length,
                         StatusPayload &status);

enum class SequenceDisposition : uint8_t {
  kFirst,
  kNext,
  kGap,
  kDuplicate,
  kStale,
};

class SequenceTracker {
public:
  SequenceTracker();

  SequenceDisposition observe(uint32_t sequence);
  void reset();
  bool initialized() const;
  uint32_t latest() const;

private:
  bool initialized_;
  uint32_t latest_;
};

} // namespace MilestoneV5
