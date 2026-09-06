#include "MilestoneV5Protocol.h"
#include "MilestoneV5Link.h"
#include "MilestoneV5Transport.h"

#include <cstring>
#include <iostream>

namespace {
int failures = 0;
#define EXPECT_TRUE(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " #value "\n"; ++failures; } } while (0)
#define EXPECT_EQ(actual, expected) do { if (!((actual) == (expected))) { std::cerr << "FAIL " << __LINE__ << ": " #actual " != " #expected "\n"; ++failures; } } while (0)

void testRoundTrip() {
  const uint8_t payload[] = {0, 1, 2, 3, 0xFE, 0xFF};
  uint8_t frame[MilestoneV5::kFrameHeaderSize + sizeof(payload)] = {};
  size_t frameLength = 0;
  const MilestoneV5::FrameFields fields = {
      MilestoneV5::MessageType::kStatus,
      MilestoneV5::kFlagAckRequired,
      0x11223344UL,
      77,
      64,
  };
  EXPECT_TRUE(MilestoneV5::encodeFrame(fields, payload, sizeof(payload), frame,
                                       sizeof(frame), frameLength));
  EXPECT_EQ(frameLength, sizeof(frame));
  MilestoneV5::DecodedFrame decoded = {};
  EXPECT_EQ(MilestoneV5::decodeFrame(frame, frameLength, decoded),
            MilestoneV5::DecodeStatus::kOk);
  EXPECT_EQ(decoded.fields.type, fields.type);
  EXPECT_EQ(decoded.fields.flags, fields.flags);
  EXPECT_EQ(decoded.fields.leaseId, fields.leaseId);
  EXPECT_EQ(decoded.fields.sequence, fields.sequence);
  EXPECT_EQ(decoded.fields.ackSequence, fields.ackSequence);
  EXPECT_EQ(decoded.payloadLength, sizeof(payload));
  EXPECT_TRUE(std::memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

void testValidation() {
  uint8_t frame[MilestoneV5::kFrameHeaderSize + 3] = {};
  const uint8_t payload[] = {1, 2, 3};
  size_t frameLength = 0;
  MilestoneV5::FrameFields fields = {MilestoneV5::MessageType::kHeartbeat, 0, 0, 1, 0};
  EXPECT_TRUE(MilestoneV5::encodeFrame(fields, payload, sizeof(payload), frame,
                                       sizeof(frame), frameLength));
  MilestoneV5::DecodedFrame decoded = {};
  EXPECT_EQ(MilestoneV5::decodeFrame(frame, frameLength - 1, decoded),
            MilestoneV5::DecodeStatus::kLengthMismatch);
  frame[0] ^= 1;
  EXPECT_EQ(MilestoneV5::decodeFrame(frame, frameLength, decoded),
            MilestoneV5::DecodeStatus::kBadMagic);
  frame[0] ^= 1;
  frame[MilestoneV5::kFrameHeaderSize + 1] ^= 1;
  EXPECT_EQ(MilestoneV5::decodeFrame(frame, frameLength, decoded),
            MilestoneV5::DecodeStatus::kBadPayloadCrc);
  EXPECT_TRUE(!MilestoneV5::encodeFrame(fields, payload, sizeof(payload), frame,
                                        sizeof(frame) - 1, frameLength));
}

void testEmptyPayloadAndSequenceWrap() {
  uint8_t frame[MilestoneV5::kFrameHeaderSize] = {};
  size_t frameLength = 0;
  MilestoneV5::FrameFields fields = {MilestoneV5::MessageType::kAck,
                                     MilestoneV5::kFlagResponse, 9, 1, 99};
  EXPECT_TRUE(MilestoneV5::encodeFrame(fields, nullptr, 0, frame, sizeof(frame), frameLength));
  MilestoneV5::DecodedFrame decoded = {};
  EXPECT_EQ(MilestoneV5::decodeFrame(frame, frameLength, decoded),
            MilestoneV5::DecodeStatus::kOk);

  MilestoneV5::SequenceTracker tracker;
  EXPECT_EQ(tracker.observe(0xFFFFFFFEUL), MilestoneV5::SequenceDisposition::kFirst);
  EXPECT_EQ(tracker.observe(0xFFFFFFFFUL), MilestoneV5::SequenceDisposition::kNext);
  EXPECT_EQ(tracker.observe(0), MilestoneV5::SequenceDisposition::kNext);
  EXPECT_EQ(tracker.observe(0), MilestoneV5::SequenceDisposition::kDuplicate);
  EXPECT_EQ(tracker.observe(5), MilestoneV5::SequenceDisposition::kGap);
  EXPECT_EQ(tracker.observe(3), MilestoneV5::SequenceDisposition::kStale);
}

void testSpiSlot() {
  uint8_t slot[MilestoneV5::kSpiSlotSize] = {};
  const uint8_t payload[] = {7, 8, 9};
  MilestoneV5::FrameFields fields = {MilestoneV5::MessageType::kTaskRequest,
                                     MilestoneV5::kFlagAckRequired, 42, 8, 7};
  EXPECT_TRUE(MilestoneV5::encodeSpiSlot(fields, payload, sizeof(payload), slot,
                                        sizeof(slot)));
  MilestoneV5::DecodedFrame decoded = {};
  MilestoneV5::DecodeStatus frameStatus = MilestoneV5::DecodeStatus::kTooShort;
  EXPECT_EQ(MilestoneV5::decodeSpiSlot(slot, sizeof(slot), decoded, frameStatus),
            MilestoneV5::SlotStatus::kOk);
  EXPECT_EQ(decoded.fields.leaseId, 42U);
  EXPECT_TRUE(std::memcmp(decoded.payload, payload, sizeof(payload)) == 0);

  slot[2] ^= 1;
  EXPECT_EQ(MilestoneV5::decodeSpiSlot(slot, sizeof(slot), decoded, frameStatus),
            MilestoneV5::SlotStatus::kBadLengthGuard);
}

void testBoundedRetryAndHeartbeat() {
  MilestoneV5::RetryController retry(3, 100);
  EXPECT_TRUE(retry.start(55, 0xFFFFFFF0UL));
  EXPECT_EQ(retry.poll(0xFFFFFFF0UL), MilestoneV5::RetryAction::kSend);
  EXPECT_EQ(retry.poll(20), MilestoneV5::RetryAction::kNone);
  EXPECT_EQ(retry.poll(100), MilestoneV5::RetryAction::kRetry);
  EXPECT_TRUE(!retry.acknowledge(54));
  EXPECT_EQ(retry.poll(200), MilestoneV5::RetryAction::kRetry);
  EXPECT_EQ(retry.poll(300), MilestoneV5::RetryAction::kFailed);
  EXPECT_TRUE(!retry.pending());

  EXPECT_TRUE(retry.start(56, 400));
  EXPECT_EQ(retry.poll(400), MilestoneV5::RetryAction::kSend);
  EXPECT_TRUE(retry.acknowledge(56));
  EXPECT_EQ(retry.poll(1000), MilestoneV5::RetryAction::kNone);

  MilestoneV5::HeartbeatMonitor heartbeat(5000);
  EXPECT_TRUE(!heartbeat.stale(9000));
  heartbeat.noteReceive(0xFFFFFF00UL);
  EXPECT_TRUE(!heartbeat.stale(1000));
  EXPECT_TRUE(heartbeat.stale(5000));
}

void testTypedPayloadsAndNegotiation() {
  uint8_t bytes[MilestoneV5::kHelloPayloadSize] = {};
  const MilestoneV5::HelloPayload hello = {
      1, 3, 2,
      MilestoneV5::kCapabilityBleAms | MilestoneV5::kCapabilityCompanionOta,
      0x12345678UL,
  };
  EXPECT_TRUE(MilestoneV5::encodeHelloPayload(hello, bytes, sizeof(bytes)));
  MilestoneV5::HelloPayload decodedHello = {};
  EXPECT_TRUE(MilestoneV5::decodeHelloPayload(bytes, sizeof(bytes), decodedHello));
  EXPECT_EQ(decodedHello.capabilities, hello.capabilities);
  EXPECT_EQ(decodedHello.bootId, hello.bootId);
  EXPECT_EQ(MilestoneV5::negotiateProtocolVersion(1, 3, 2, 4), 3);
  EXPECT_EQ(MilestoneV5::negotiateProtocolVersion(1, 1, 2, 4), 0);

  MilestoneV5::StatusPayload status = {3, -125, 123456, 654321};
  EXPECT_TRUE(MilestoneV5::encodeStatusPayload(status, bytes, sizeof(bytes)));
  MilestoneV5::StatusPayload decodedStatus = {};
  EXPECT_TRUE(MilestoneV5::decodeStatusPayload(bytes, MilestoneV5::kStatusPayloadSize,
                                               decodedStatus));
  EXPECT_EQ(decodedStatus.stateFlags, status.stateFlags);
  EXPECT_EQ(decodedStatus.temperatureCenti, status.temperatureCenti);
  EXPECT_EQ(decodedStatus.freeHeap, status.freeHeap);
  EXPECT_EQ(decodedStatus.freePsram, status.freePsram);
}
}  // namespace

int main() {
  testRoundTrip();
  testValidation();
  testEmptyPayloadAndSequenceWrap();
  testSpiSlot();
  testBoundedRetryAndHeartbeat();
  testTypedPayloadsAndNegotiation();
  if (failures != 0) return 1;
  std::cout << "MILESTONE v5 protocol tests passed\n";
  return 0;
}
