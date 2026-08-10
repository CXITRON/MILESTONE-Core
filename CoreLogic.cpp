#include "CoreLogic.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

namespace MilestoneCoreLogic {
namespace {

bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int64_t daysBeforeYear(int year) {
  const int64_t y = static_cast<int64_t>(year) - 1;
  return 365 * y + y / 4 - y / 100 + y / 400;
}

int dayOfYear(const CivilDate &date) {
  static const uint16_t daysBeforeMonth[] = {
      0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  return daysBeforeMonth[date.month] + date.day +
         ((date.month > 2 && isLeapYear(date.year)) ? 1 : 0);
}

int64_t civilOrdinal(const CivilDate &date) {
  return daysBeforeYear(date.year) + dayOfYear(date);
}

}  // namespace

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool parseIsoDate(const char *text, CivilDate &date, int minYear, int maxYear) {
  if (text == nullptr || strlen(text) != 10 || text[4] != '-' || text[7] != '-') return false;
  constexpr uint8_t digitPositions[] = {0, 1, 2, 3, 5, 6, 8, 9};
  for (uint8_t position : digitPositions) {
    if (!isAsciiDigit(text[position])) return false;
  }

  CivilDate parsed;
  parsed.year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
  parsed.month = (text[5] - '0') * 10 + (text[6] - '0');
  parsed.day = (text[8] - '0') * 10 + (text[9] - '0');
  if (parsed.year < minYear || parsed.year > maxYear || parsed.month < 1 || parsed.month > 12) return false;

  static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const int maxDay = monthDays[parsed.month - 1] + ((parsed.month == 2 && isLeapYear(parsed.year)) ? 1 : 0);
  if (parsed.day < 1 || parsed.day > maxDay) return false;

  date = parsed;
  return true;
}

int daysBetween(const CivilDate &from, const CivilDate &to) {
  const int64_t delta = civilOrdinal(to) - civilOrdinal(from);
  if (delta > INT_MAX) return INT_MAX;
  if (delta < INT_MIN) return INT_MIN;
  return static_cast<int>(delta);
}

bool parseCycleOrder(const char *text, uint8_t *out, uint8_t expectedCount) {
  if (text == nullptr || out == nullptr || expectedCount == 0 || expectedCount > 10) return false;

  bool seen[10] = {};
  uint8_t count = 0;
  const char *cursor = text;

  while (true) {
    while (isAsciiSpace(*cursor)) ++cursor;
    if (!isAsciiDigit(*cursor)) return false;

    const uint8_t value = static_cast<uint8_t>(*cursor - '0');
    ++cursor;
    while (isAsciiSpace(*cursor)) ++cursor;

    if (value >= expectedCount || seen[value] || count >= expectedCount) return false;
    seen[value] = true;
    out[count++] = value;

    if (*cursor == '\0') break;
    if (*cursor != ',') return false;
    ++cursor;
  }

  if (count != expectedCount) return false;
  for (uint8_t i = 0; i < expectedCount; ++i) {
    if (!seen[i]) return false;
  }
  return true;
}

bool parseSemanticVersion(const char *text, uint16_t parts[3]) {
  if (text == nullptr || parts == nullptr) return false;

  const char *cursor = text;
  for (uint8_t index = 0; index < 3; ++index) {
    if (!isAsciiDigit(*cursor)) return false;

    uint32_t part = 0;
    while (isAsciiDigit(*cursor)) {
      part = part * 10UL + static_cast<uint8_t>(*cursor - '0');
      if (part > UINT16_MAX) return false;
      ++cursor;
    }
    parts[index] = static_cast<uint16_t>(part);

    if (index < 2) {
      if (*cursor != '.') return false;
      ++cursor;
    }
  }
  return *cursor == '\0';
}

int compareSemanticVersions(const char *left, const char *right) {
  uint16_t a[3] = {};
  uint16_t b[3] = {};
  if (!parseSemanticVersion(left, a) || !parseSemanticVersion(right, b)) return 0;
  for (uint8_t i = 0; i < 3; ++i) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

bool validSha256(const char *value) {
  if (value == nullptr || strlen(value) != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    const char c = value[i];
    if (!isAsciiDigit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;
  }
  return true;
}

bool decodeJsonEscape(char escaped, char &decoded) {
  switch (escaped) {
    case 'b': decoded = '\b'; return true;
    case 'f': decoded = '\f'; return true;
    case 'n': decoded = '\n'; return true;
    case 'r': decoded = '\r'; return true;
    case 't': decoded = '\t'; return true;
    case '"': decoded = '"'; return true;
    case '\\': decoded = '\\'; return true;
    case '/': decoded = '/'; return true;
    default: return false;
  }
}

}  // namespace MilestoneCoreLogic
