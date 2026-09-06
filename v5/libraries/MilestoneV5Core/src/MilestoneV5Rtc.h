#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MilestoneV5 {
struct RtcTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};
bool decodeRtc(const uint8_t *registers, size_t size, uint8_t status,
               RtcTime &time);
} // namespace MilestoneV5
