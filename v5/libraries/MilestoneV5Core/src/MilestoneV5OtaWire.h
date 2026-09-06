#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MilestoneV5 {
enum class RemoteOtaState : uint8_t {
  Idle,
  Manifest,
  Quiescing,
  Writing,
  Ready,
  Rebooting,
  Failed,
  Complete,
  BootTesting
};
constexpr size_t kRemoteOtaChunk = 460;
inline uint32_t otaU32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
inline void otaPut32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = v >> (8 * i);
}
inline bool validOtaRequest(const uint8_t *p, size_t n) {
  if (!p || n < 5 || otaU32(p + 1) == 0)
    return false;
  switch (p[0]) {
  case 1:
    return n == 9 && (unsigned(p[5]) | unsigned(p[6]) << 8) >= 90 &&
           (unsigned(p[5]) | unsigned(p[6]) << 8) <= 255 &&
           (unsigned(p[7]) | unsigned(p[8]) << 8) > 0 &&
           (unsigned(p[7]) | unsigned(p[8]) << 8) <= 512;
  case 2:
  case 3:
    return n >= 10 && n <= 9 + kRemoteOtaChunk;
  case 4:
  case 5:
  case 6:
  case 7:
    return n == 5;
  default:
    return false;
  }
}
} // namespace MilestoneV5
