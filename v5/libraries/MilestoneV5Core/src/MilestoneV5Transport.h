#pragma once

#include "MilestoneV5Protocol.h"

namespace MilestoneV5 {

constexpr size_t kSpiSlotSize = 512;
constexpr size_t kSpiEnvelopeSize = 4;
constexpr size_t kMaxSpiFrameSize = kSpiSlotSize - kSpiEnvelopeSize;
constexpr size_t kMaxSpiPayloadSize = kMaxSpiFrameSize - kFrameHeaderSize;

enum class SlotStatus : uint8_t {
  kOk,
  kNullArgument,
  kFrameTooLarge,
  kBadLengthGuard,
  kBadPaddingLength,
  kBadFrame,
};

bool encodeSpiSlot(const FrameFields &fields, const uint8_t *payload,
                   uint16_t payloadLength, uint8_t *slot, size_t slotCapacity);

SlotStatus decodeSpiSlot(const uint8_t *slot, size_t slotLength,
                         DecodedFrame &decoded, DecodeStatus &frameStatus);

} // namespace MilestoneV5
