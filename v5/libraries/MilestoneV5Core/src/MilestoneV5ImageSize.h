#pragma once
#include <stdint.h>
#include <stddef.h>

namespace MilestoneV5 {
// Display metadata only; never a substitute for OTA signature verification.
// At most 17 small reads, independent of image length.
template <typename Reader>
uint32_t displayImageSize(uint32_t capacity, Reader read) {
  uint8_t header[24];
  if (capacity < sizeof(header) || !read(0, header, sizeof(header)) ||
      header[0] != 0xe9 || !header[1] || header[1] > 16 || header[23] > 1)
    return 0;
  uint32_t offset = sizeof(header);
  for (unsigned i = 0; i < header[1]; ++i) {
    uint8_t segment[8];
    if (offset > capacity || capacity - offset < sizeof(segment) ||
        !read(offset, segment, sizeof(segment)))
      return 0;
    offset += sizeof(segment);
    const uint32_t length = uint32_t(segment[4]) | uint32_t(segment[5]) << 8 |
                            uint32_t(segment[6]) << 16 | uint32_t(segment[7]) << 24;
    if (length > capacity - offset)
      return 0;
    offset += length;
  }
  const uint64_t result = ((uint64_t(offset) + 16) & ~uint64_t(15)) +
                          (header[23] ? 32 : 0);
  return result <= capacity ? uint32_t(result) : 0;
}
}
