#pragma once
#include "MilestoneV5Calendar.h"
#include "MilestoneV5Protocol.h"
#include <string.h>
namespace MilestoneV5 {
constexpr size_t kLogRecordSize = 152;
inline bool validLogRecord(const uint8_t *p, size_t n) {
  if (!p || n != kLogRecordSize || memcmp(p, "VL01", 4) ||
      !validDate(unsigned(p[4]) | unsigned(p[5]) << 8, p[6], p[7]) ||
      p[12] == 0 || p[12] > 127 || p[13 + p[12] - 1] != '\n')
    return false;
  uint32_t crc = 0;
  for (unsigned i = 0; i < 4; ++i)
    crc |= uint32_t(p[148 + i]) << (8 * i);
  return crc32(p, 148) == crc;
}
inline bool encodeLogRecord(unsigned year, unsigned month, unsigned day,
                            uint32_t offset, const char *row, size_t length,
                            uint8_t *p, size_t capacity) {
  if (!p || capacity < kLogRecordSize || !row || !length || length > 127 ||
      row[length - 1] != '\n' || !validDate(year, month, day))
    return false;
  memset(p, 0, kLogRecordSize);
  memcpy(p, "VL01", 4);
  p[4] = year;
  p[5] = year >> 8;
  p[6] = month;
  p[7] = day;
  for (unsigned i = 0; i < 4; ++i)
    p[8 + i] = offset >> (8 * i);
  p[12] = length;
  memcpy(p + 13, row, length);
  uint32_t crc = crc32(p, 148);
  for (unsigned i = 0; i < 4; ++i)
    p[148 + i] = crc >> (8 * i);
  return true;
}
} // namespace MilestoneV5
