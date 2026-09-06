#include "MilestoneV5Rtc.h"
namespace MilestoneV5 {
namespace {
int bcd(uint8_t v) {
  return (v & 15) > 9 || (v >> 4) > 9 ? -1 : (v >> 4) * 10 + (v & 15);
}
} // namespace
bool decodeRtc(const uint8_t *r, size_t n, uint8_t status, RtcTime &t) {
  t = {};
  if (!r || n != 7 || (status & 0x80) || (r[0] & 0x80) || (r[1] & 0x80) ||
      (r[2] & 0x80) || (r[4] & 0xC0) || (r[5] & 0x60))
    return false;
  const int s = bcd(r[0]), m = bcd(r[1]);
  int h = bcd(r[2] & ((r[2] & 0x40) ? 0x1F : 0x3F));
  if (r[2] & 0x40) {
    if (h < 1 || h > 12)
      return false;
    h = h % 12 + ((r[2] & 0x20) ? 12 : 0);
  }
  const int y = bcd(r[6]), month = bcd(r[5] & 0x1F), day = bcd(r[4]);
  if (s < 0 || s > 59 || m < 0 || m > 59 || h < 0 || h > 23 || y < 0 ||
      month < 1 || month > 12 || day < 1)
    return false;
  const int year = 2000 + y + ((r[5] & 0x80) ? 100 : 0);
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > days[month - 1] + ((month == 2 && leap) ? 1 : 0))
    return false;
  t = {static_cast<uint16_t>(year), static_cast<uint8_t>(month),
       static_cast<uint8_t>(day),   static_cast<uint8_t>(h),
       static_cast<uint8_t>(m),     static_cast<uint8_t>(s)};
  return true;
}
} // namespace MilestoneV5
