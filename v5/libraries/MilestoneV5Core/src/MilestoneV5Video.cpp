#include "MilestoneV5Video.h"
#include <string.h>
namespace MilestoneV5 {
uint32_t readVideoU32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
bool decodeVideoHeader(const uint8_t *p, size_t n, VideoInfo &info) {
  if (!p || n != 16 || memcmp(p, "MVJ1", 4) || p[10] || p[11])
    return false;
  VideoInfo v{uint16_t(p[4] | uint16_t(p[5]) << 8),
              uint16_t(p[6] | uint16_t(p[7]) << 8),
              uint16_t(p[8] | uint16_t(p[9]) << 8), readVideoU32(p + 12)};
  if (v.width != 128 || v.height != 128 || v.fps < 1 || v.fps > 30 ||
      !v.frames || v.frames > 1000000)
    return false;
  info = v;
  return true;
}
} // namespace MilestoneV5
