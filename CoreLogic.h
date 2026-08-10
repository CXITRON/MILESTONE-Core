#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MilestoneCoreLogic {

struct CivilDate {
  int year;
  int month;
  int day;
};

bool isLeapYear(int year);
bool parseIsoDate(const char *text, CivilDate &date, int minYear = 2024, int maxYear = 2099);
int daysBetween(const CivilDate &from, const CivilDate &to);

bool parseCycleOrder(const char *text, uint8_t *out, uint8_t expectedCount);

bool parseSemanticVersion(const char *text, uint16_t parts[3]);
int compareSemanticVersions(const char *left, const char *right);

bool validSha256(const char *value);
bool decodeJsonEscape(char escaped, char &decoded);

}  // namespace MilestoneCoreLogic
