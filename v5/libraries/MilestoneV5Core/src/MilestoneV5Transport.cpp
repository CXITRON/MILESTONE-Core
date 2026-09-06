#include "MilestoneV5Transport.h"

#include <string.h>

namespace MilestoneV5 {

bool encodeSpiSlot(const FrameFields &fields, const uint8_t *payload,
                   uint16_t payloadLength, uint8_t *slot, size_t slotCapacity) {
  if (slot == nullptr || slotCapacity < kSpiSlotSize ||
      payloadLength > kMaxSpiPayloadSize) {
    return false;
  }
  memset(slot, 0, kSpiSlotSize);
  size_t frameLength = 0;
  if (!encodeFrame(fields, payload, payloadLength, slot + kSpiEnvelopeSize,
                   kMaxSpiFrameSize, frameLength)) {
    return false;
  }
  const uint16_t boundedLength = static_cast<uint16_t>(frameLength);
  const uint16_t guard = static_cast<uint16_t>(~boundedLength);
  slot[0] = static_cast<uint8_t>(boundedLength);
  slot[1] = static_cast<uint8_t>(boundedLength >> 8);
  slot[2] = static_cast<uint8_t>(guard);
  slot[3] = static_cast<uint8_t>(guard >> 8);
  return true;
}

SlotStatus decodeSpiSlot(const uint8_t *slot, size_t slotLength,
                         DecodedFrame &decoded, DecodeStatus &frameStatus) {
  decoded = {};
  frameStatus = DecodeStatus::kTooShort;
  if (slot == nullptr)
    return SlotStatus::kNullArgument;
  if (slotLength != kSpiSlotSize)
    return SlotStatus::kBadPaddingLength;
  const uint16_t frameLength =
      static_cast<uint16_t>(slot[0]) | static_cast<uint16_t>(slot[1]) << 8;
  const uint16_t guard =
      static_cast<uint16_t>(slot[2]) | static_cast<uint16_t>(slot[3]) << 8;
  if (static_cast<uint16_t>(frameLength ^ guard) != 0xFFFFU) {
    return SlotStatus::kBadLengthGuard;
  }
  if (frameLength < kFrameHeaderSize || frameLength > kMaxSpiFrameSize) {
    return SlotStatus::kFrameTooLarge;
  }
  frameStatus = decodeFrame(slot + kSpiEnvelopeSize, frameLength, decoded);
  return frameStatus == DecodeStatus::kOk ? SlotStatus::kOk
                                          : SlotStatus::kBadFrame;
}

} // namespace MilestoneV5
