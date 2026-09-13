#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace MilestoneV5 {
constexpr size_t kRuntimeDetailsBytes = 56;
struct RuntimeDetails {
  char firmware[18]{};
  uint32_t uptime = 0, minimumHeap = 0, largestHeap = 0, stackFree = 0;
  uint32_t validFrames = 0, invalidFrames = 0, maxLoopMs = 0;
  uint16_t cpuMHz = 0;
  uint8_t reset = 0, bootState = 255;
  int8_t rssi = -127;
};
inline void encodeRuntimeDetails(const RuntimeDetails &d, uint8_t *p) {
  memset(p, 0, kRuntimeDetailsBytes);
  p[0] = 35;
  p[1] = 1;
  memcpy(p + 2, d.firmware, sizeof(d.firmware));
  const uint32_t fields[] = {d.uptime,    d.minimumHeap, d.largestHeap,
                             d.stackFree, d.validFrames, d.invalidFrames,
                             d.maxLoopMs};
  for (unsigned i = 0; i < 7; ++i)
    for (unsigned j = 0; j < 4; ++j)
      p[20 + i * 4 + j] = fields[i] >> (8 * j);
  p[48] = d.cpuMHz;
  p[49] = d.cpuMHz >> 8;
  p[50] = d.reset;
  p[51] = uint8_t(d.rssi);
  p[52] = d.bootState;
}
inline bool decodeRuntimeDetails(const uint8_t *p, size_t n,
                                 RuntimeDetails &d) {
  if (!p || n != kRuntimeDetailsBytes || p[0] != 35 || p[1] != 1 ||
      !memchr(p + 2, 0, 18))
    return false;
  for (unsigned i = 2; i < 20 && p[i]; ++i)
    if ((p[i] < '0' || p[i] > '9') && p[i] != '.')
      return false;
  RuntimeDetails value;
  memcpy(value.firmware, p + 2, 18);
  uint32_t *fields[] = {&value.uptime,      &value.minimumHeap,
                        &value.largestHeap, &value.stackFree,
                        &value.validFrames, &value.invalidFrames,
                        &value.maxLoopMs};
  for (unsigned i = 0; i < 7; ++i)
    for (unsigned j = 0; j < 4; ++j)
      *fields[i] |= uint32_t(p[20 + i * 4 + j]) << (8 * j);
  value.cpuMHz = uint16_t(p[48]) | (uint16_t(p[49]) << 8);
  value.reset = p[50];
  value.rssi = int8_t(p[51]);
  value.bootState = p[52];
  d = value;
  return true;
}
} // namespace MilestoneV5
