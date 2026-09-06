#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MilestoneV5 {
// Experimental, versioned local SD format. Not a permanent compatibility
// promise.
constexpr uint32_t kVideoMaxJpeg = 32768;
struct VideoInfo {
  uint16_t width, height, fps;
  uint32_t frames;
};
bool decodeVideoHeader(const uint8_t *data, size_t size, VideoInfo &info);
uint32_t readVideoU32(const uint8_t *p);
} // namespace MilestoneV5
