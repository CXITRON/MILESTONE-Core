#pragma once
#include "MilestoneV5Protocol.h"
#include <string.h>
namespace MilestoneV5 {
constexpr size_t kArtworkBytes = 16 + 60 * 60 * 2 + 88 * 88 * 2;
inline bool validArtwork(const uint8_t *p, size_t n) {
  if (!p || n != kArtworkBytes || memcmp(p, "MAC1", 4) || p[4] != 60 ||
      p[5] != 60 || p[6] != 88 || p[7] != 88 ||
      (unsigned(p[8]) * 256 + p[9]) != 7200 ||
      (unsigned(p[10]) * 256 + p[11]) != 15488)
    return false;
  const uint32_t expected = uint32_t(p[12]) << 24 | uint32_t(p[13]) << 16 |
                            uint32_t(p[14]) << 8 | p[15];
  return crc32(p + 16, n - 16) == expected;
}
} // namespace MilestoneV5
