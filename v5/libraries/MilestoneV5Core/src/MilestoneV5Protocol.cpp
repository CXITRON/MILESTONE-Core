#include "MilestoneV5Protocol.h"

#include <string.h>

namespace MilestoneV5 {
namespace {

constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kHeaderSizeOffset = 5;
constexpr size_t kTypeOffset = 6;
constexpr size_t kFlagsOffset = 8;
constexpr size_t kPayloadLengthOffset = 10;
constexpr size_t kLeaseIdOffset = 12;
constexpr size_t kSequenceOffset = 16;
constexpr size_t kAckSequenceOffset = 20;
constexpr size_t kPayloadCrcOffset = 24;
constexpr size_t kHeaderCrcOffset = 28;

void put16(uint8_t *destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t *destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
  destination[2] = static_cast<uint8_t>(value >> 16);
  destination[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t get16(const uint8_t *source) {
  return static_cast<uint16_t>(source[0]) | static_cast<uint16_t>(source[1])
                                                << 8;
}

uint32_t get32(const uint8_t *source) {
  return static_cast<uint32_t>(source[0]) |
         static_cast<uint32_t>(source[1]) << 8 |
         static_cast<uint32_t>(source[2]) << 16 |
         static_cast<uint32_t>(source[3]) << 24;
}

bool validMessageType(MessageType type) {
  switch (type) {
  case MessageType::kHello:
  case MessageType::kHelloReply:
  case MessageType::kHeartbeat:
  case MessageType::kAck:
  case MessageType::kStatus:
  case MessageType::kTaskRequest:
  case MessageType::kTaskResult:
  case MessageType::kAmsMetadata:
  case MessageType::kArtworkChunk:
  case MessageType::kOtaChunk:
  case MessageType::kOtaControl:
    return true;
  case MessageType::kInvalid:
    return false;
  }
  return false;
}

} // namespace

uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  if (data == nullptr && length != 0)
    return 0;
  while (length-- != 0) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

size_t encodedFrameSize(size_t payloadLength) {
  return payloadLength <= kMaxPayloadSize ? kFrameHeaderSize + payloadLength
                                          : 0;
}

bool encodeFrame(const FrameFields &fields, const uint8_t *payload,
                 uint16_t payloadLength, uint8_t *output, size_t outputCapacity,
                 size_t &outputLength) {
  outputLength = 0;
  const size_t required = encodedFrameSize(payloadLength);
  if (output == nullptr || required == 0 || outputCapacity < required ||
      (payload == nullptr && payloadLength != 0) ||
      !validMessageType(fields.type)) {
    return false;
  }

  memset(output, 0, kFrameHeaderSize);
  put32(output + kMagicOffset, kProtocolMagic);
  output[kVersionOffset] = kProtocolVersion;
  output[kHeaderSizeOffset] = static_cast<uint8_t>(kFrameHeaderSize);
  put16(output + kTypeOffset, static_cast<uint16_t>(fields.type));
  put16(output + kFlagsOffset, fields.flags);
  put16(output + kPayloadLengthOffset, payloadLength);
  put32(output + kLeaseIdOffset, fields.leaseId);
  put32(output + kSequenceOffset, fields.sequence);
  put32(output + kAckSequenceOffset, fields.ackSequence);
  put32(output + kPayloadCrcOffset, crc32(payload, payloadLength));
  put32(output + kHeaderCrcOffset, crc32(output, kHeaderCrcOffset));
  if (payloadLength != 0)
    memcpy(output + kFrameHeaderSize, payload, payloadLength);
  outputLength = required;
  return true;
}

DecodeStatus decodeFrame(const uint8_t *frame, size_t frameLength,
                         DecodedFrame &decoded) {
  decoded = {};
  if (frame == nullptr)
    return DecodeStatus::kNullArgument;
  if (frameLength < kFrameHeaderSize)
    return DecodeStatus::kTooShort;
  if (get32(frame + kMagicOffset) != kProtocolMagic)
    return DecodeStatus::kBadMagic;
  if (frame[kVersionOffset] != kProtocolVersion)
    return DecodeStatus::kUnsupportedVersion;
  if (frame[kHeaderSizeOffset] != kFrameHeaderSize)
    return DecodeStatus::kBadHeaderSize;

  const uint16_t payloadLength = get16(frame + kPayloadLengthOffset);
  if (payloadLength > kMaxPayloadSize)
    return DecodeStatus::kPayloadTooLarge;
  if (frameLength != encodedFrameSize(payloadLength))
    return DecodeStatus::kLengthMismatch;
  if (get32(frame + kHeaderCrcOffset) != crc32(frame, kHeaderCrcOffset)) {
    return DecodeStatus::kBadHeaderCrc;
  }
  if (get32(frame + kPayloadCrcOffset) !=
      crc32(frame + kFrameHeaderSize, payloadLength)) {
    return DecodeStatus::kBadPayloadCrc;
  }

  const MessageType type = static_cast<MessageType>(get16(frame + kTypeOffset));
  if (!validMessageType(type))
    return DecodeStatus::kInvalidType;
  decoded.fields.type = type;
  decoded.fields.flags = get16(frame + kFlagsOffset);
  decoded.fields.leaseId = get32(frame + kLeaseIdOffset);
  decoded.fields.sequence = get32(frame + kSequenceOffset);
  decoded.fields.ackSequence = get32(frame + kAckSequenceOffset);
  decoded.payload = frame + kFrameHeaderSize;
  decoded.payloadLength = payloadLength;
  return DecodeStatus::kOk;
}

const char *decodeStatusName(DecodeStatus status) {
  switch (status) {
  case DecodeStatus::kOk:
    return "ok";
  case DecodeStatus::kNullArgument:
    return "null-argument";
  case DecodeStatus::kTooShort:
    return "too-short";
  case DecodeStatus::kBadMagic:
    return "bad-magic";
  case DecodeStatus::kUnsupportedVersion:
    return "unsupported-version";
  case DecodeStatus::kBadHeaderSize:
    return "bad-header-size";
  case DecodeStatus::kPayloadTooLarge:
    return "payload-too-large";
  case DecodeStatus::kLengthMismatch:
    return "length-mismatch";
  case DecodeStatus::kBadHeaderCrc:
    return "bad-header-crc";
  case DecodeStatus::kBadPayloadCrc:
    return "bad-payload-crc";
  case DecodeStatus::kInvalidType:
    return "invalid-type";
  }
  return "unknown";
}

bool encodeHelloPayload(const HelloPayload &hello, uint8_t *output,
                        size_t capacity) {
  if (output == nullptr || capacity < kHelloPayloadSize ||
      hello.minimumVersion == 0 ||
      hello.minimumVersion > hello.maximumVersion || hello.board == 0)
    return false;
  memset(output, 0, kHelloPayloadSize);
  output[0] = hello.minimumVersion;
  output[1] = hello.maximumVersion;
  output[2] = hello.board;
  put32(output + 4, hello.capabilities);
  put32(output + 8, hello.bootId);
  return true;
}

bool decodeHelloPayload(const uint8_t *payload, size_t length,
                        HelloPayload &hello) {
  hello = {};
  if (payload == nullptr || length != kHelloPayloadSize || payload[0] == 0 ||
      payload[0] > payload[1] || payload[2] == 0)
    return false;
  hello.minimumVersion = payload[0];
  hello.maximumVersion = payload[1];
  hello.board = payload[2];
  hello.capabilities = get32(payload + 4);
  hello.bootId = get32(payload + 8);
  return true;
}

uint8_t negotiateProtocolVersion(uint8_t localMinimum, uint8_t localMaximum,
                                 uint8_t peerMinimum, uint8_t peerMaximum) {
  if (localMinimum == 0 || peerMinimum == 0 || localMinimum > localMaximum ||
      peerMinimum > peerMaximum)
    return 0;
  const uint8_t highestCommon =
      localMaximum < peerMaximum ? localMaximum : peerMaximum;
  const uint8_t lowestCommon =
      localMinimum > peerMinimum ? localMinimum : peerMinimum;
  return highestCommon >= lowestCommon ? highestCommon : 0;
}

bool encodeStatusPayload(const StatusPayload &status, uint8_t *output,
                         size_t capacity) {
  if (output == nullptr || capacity < kStatusPayloadSize)
    return false;
  put16(output, status.stateFlags);
  put16(output + 2, static_cast<uint16_t>(status.temperatureCenti));
  put32(output + 4, status.freeHeap);
  put32(output + 8, status.freePsram);
  return true;
}

bool decodeStatusPayload(const uint8_t *payload, size_t length,
                         StatusPayload &status) {
  status = {};
  if (payload == nullptr || length != kStatusPayloadSize)
    return false;
  status.stateFlags = get16(payload);
  status.temperatureCenti = static_cast<int16_t>(get16(payload + 2));
  status.freeHeap = get32(payload + 4);
  status.freePsram = get32(payload + 8);
  return true;
}

SequenceTracker::SequenceTracker() : initialized_(false), latest_(0) {}

SequenceDisposition SequenceTracker::observe(uint32_t sequence) {
  if (!initialized_) {
    initialized_ = true;
    latest_ = sequence;
    return SequenceDisposition::kFirst;
  }
  const int32_t distance = static_cast<int32_t>(sequence - latest_);
  if (distance == 0)
    return SequenceDisposition::kDuplicate;
  if (distance < 0)
    return SequenceDisposition::kStale;
  latest_ = sequence;
  return distance == 1 ? SequenceDisposition::kNext : SequenceDisposition::kGap;
}

void SequenceTracker::reset() {
  initialized_ = false;
  latest_ = 0;
}

bool SequenceTracker::initialized() const { return initialized_; }
uint32_t SequenceTracker::latest() const { return latest_; }

} // namespace MilestoneV5
