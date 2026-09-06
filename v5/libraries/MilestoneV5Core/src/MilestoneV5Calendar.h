#pragma once
#include <stdint.h>
namespace MilestoneV5 {
inline bool validDate(unsigned year, unsigned month, unsigned day) {
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1)
    return false;
  const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return day <= days[month - 1] + unsigned(month == 2 && year % 4 == 0);
}
// Gregorian day ordinal, valid only after validDate(). No time zone or DST
// math.
inline int32_t dayOrdinal(unsigned year, unsigned month, unsigned day) {
  int32_t result = 0;
  for (unsigned y = 2000; y < year; ++y)
    result += 365 + (y % 4 == 0);
  for (unsigned m = 1; m < month; ++m) {
    const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    result += days[m - 1] + unsigned(m == 2 && year % 4 == 0);
  }
  return result + int32_t(day) - 1;
}
} // namespace MilestoneV5
