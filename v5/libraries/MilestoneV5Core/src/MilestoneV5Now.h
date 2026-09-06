#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MilestoneV5 {
struct NowMetadata {
  char title[145], artist[145], album[145];
  uint32_t elapsedSeconds, durationSeconds;
  bool connected, ready, playing;
};
// Header: flags, three string lengths, elapsed and duration LE32; UTF-8 bytes.
bool encodeNow(const NowMetadata &value, uint8_t *bytes, size_t capacity,
               size_t &size);
bool decodeNow(const uint8_t *bytes, size_t size, NowMetadata &value);
void copyNowText(char *dest, size_t capacity, const char *source);
} // namespace MilestoneV5
