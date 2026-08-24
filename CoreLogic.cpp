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

const char *latinAsciiReplacement(uint32_t codepoint) {
  switch (codepoint) {
    case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
    case 0x0100: case 0x0102: case 0x0104: return "A";
    case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
    case 0x0101: case 0x0103: case 0x0105: return "a";
    case 0x00C6: return "AE";
    case 0x00E6: return "ae";
    case 0x00C7: case 0x0106: case 0x0108: case 0x010A: case 0x010C: return "C";
    case 0x00E7: case 0x0107: case 0x0109: case 0x010B: case 0x010D: return "c";
    case 0x00D0: case 0x010E: case 0x0110: return "D";
    case 0x00F0: case 0x010F: case 0x0111: return "d";
    case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
    case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A: return "E";
    case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
    case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B: return "e";
    case 0x011C: case 0x011E: case 0x0120: case 0x0122: return "G";
    case 0x011D: case 0x011F: case 0x0121: case 0x0123: return "g";
    case 0x0124: case 0x0126: return "H";
    case 0x0125: case 0x0127: return "h";
    case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
    case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130: return "I";
    case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
    case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131: return "i";
    case 0x0134: return "J";
    case 0x0135: return "j";
    case 0x0136: return "K";
    case 0x0137: return "k";
    case 0x0139: case 0x013B: case 0x013D: case 0x013F: case 0x0141: return "L";
    case 0x013A: case 0x013C: case 0x013E: case 0x0140: case 0x0142: return "l";
    case 0x00D1: case 0x0143: case 0x0145: case 0x0147: return "N";
    case 0x00F1: case 0x0144: case 0x0146: case 0x0148: return "n";
    case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
    case 0x014C: case 0x014E: case 0x0150: return "O";
    case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
    case 0x014D: case 0x014F: case 0x0151: return "o";
    case 0x0152: return "OE";
    case 0x0153: return "oe";
    case 0x0154: case 0x0156: case 0x0158: return "R";
    case 0x0155: case 0x0157: case 0x0159: return "r";
    case 0x015A: case 0x015C: case 0x015E: case 0x0160: return "S";
    case 0x015B: case 0x015D: case 0x015F: case 0x0161: return "s";
    case 0x00DF: return "ss";
    case 0x0162: case 0x0164: case 0x0166: return "T";
    case 0x0163: case 0x0165: case 0x0167: return "t";
    case 0x00DE: return "TH";
    case 0x00FE: return "th";
    case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
    case 0x0168: case 0x016A: case 0x016C: case 0x016E: case 0x0170: case 0x0172: return "U";
    case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
    case 0x0169: case 0x016B: case 0x016D: case 0x016F: case 0x0171: case 0x0173: return "u";
    case 0x0174: return "W";
    case 0x0175: return "w";
    case 0x00DD: case 0x0176: case 0x0178: return "Y";
    case 0x00FD: case 0x00FF: case 0x0177: return "y";
    case 0x0179: case 0x017B: case 0x017D: return "Z";
    case 0x017A: case 0x017C: case 0x017E: return "z";
    default: return nullptr;
  }
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

bool utf8ContainsHangul(const char *text) {
  if (text == nullptr) return false;
  const uint8_t *cursor = reinterpret_cast<const uint8_t *>(text);
  while (*cursor != 0) {
    uint32_t codepoint = 0;
    size_t continuation = 0;
    if (*cursor < 0x80U) {
      codepoint = *cursor++;
    } else if ((*cursor & 0xE0U) == 0xC0U) {
      codepoint = *cursor++ & 0x1FU;
      continuation = 1;
    } else if ((*cursor & 0xF0U) == 0xE0U) {
      codepoint = *cursor++ & 0x0FU;
      continuation = 2;
    } else if ((*cursor & 0xF8U) == 0xF0U) {
      codepoint = *cursor++ & 0x07U;
      continuation = 3;
    } else {
      ++cursor;
      continue;
    }
    bool valid = true;
    for (size_t index = 0; index < continuation; ++index) {
      if (cursor[index] == 0 || (cursor[index] & 0xC0U) != 0x80U) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6U) | (cursor[index] & 0x3FU);
    }
    if (!valid) continue;
    cursor += continuation;
    if ((codepoint >= 0x1100U && codepoint <= 0x11FFU) ||
        (codepoint >= 0x3130U && codepoint <= 0x318FU) ||
        (codepoint >= 0xAC00U && codepoint <= 0xD7AFU)) {
      return true;
    }
  }
  return false;
}

size_t foldLatinDiacriticsUtf8(const char *input, char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return 0;
  output[0] = '\0';
  if (input == nullptr) return 0;

  const uint8_t *cursor = reinterpret_cast<const uint8_t *>(input);
  size_t written = 0;
  while (*cursor != 0) {
    const uint8_t *unit = cursor;
    uint32_t codepoint = 0;
    size_t unitLength = 1;
    size_t continuation = 0;
    if (*cursor < 0x80U) {
      codepoint = *cursor;
    } else if ((*cursor & 0xE0U) == 0xC0U) {
      codepoint = *cursor & 0x1FU;
      continuation = 1;
    } else if ((*cursor & 0xF0U) == 0xE0U) {
      codepoint = *cursor & 0x0FU;
      continuation = 2;
    } else if ((*cursor & 0xF8U) == 0xF0U) {
      codepoint = *cursor & 0x07U;
      continuation = 3;
    }

    bool valid = continuation != 0;
    for (size_t index = 1; valid && index <= continuation; ++index) {
      if (cursor[index] == 0 || (cursor[index] & 0xC0U) != 0x80U) {
        valid = false;
      } else {
        codepoint = (codepoint << 6U) | (cursor[index] & 0x3FU);
      }
    }
    if (valid) unitLength += continuation;

    const char *replacement = valid ? latinAsciiReplacement(codepoint) : nullptr;
    const size_t replacementLength = replacement ? strlen(replacement) : unitLength;
    if (written + replacementLength >= capacity) break;
    if (replacement) {
      memcpy(output + written, replacement, replacementLength);
    } else {
      memcpy(output + written, unit, unitLength);
    }
    written += replacementLength;
    cursor += unitLength;
  }
  output[written] = '\0';
  return written;
}

bool shouldIgnoreLateBluetoothEncryptionFailure(bool linkAlreadySecured,
                                                int errorStatus,
                                                int timeoutStatus) {
  return linkAlreadySecured && errorStatus == timeoutStatus;
}

bool shouldRetryTransientHttpFailure(int responseCode, bool responseComplete,
                                     uint8_t attempt, uint8_t maximumAttempts) {
  if (attempt >= maximumAttempts) return false;
  return responseCode < 0 || responseCode == 429 || responseCode >= 500 ||
         (responseCode == 200 && !responseComplete);
}

void resetMusicBrainzReleaseGroupParser(MusicBrainzReleaseGroupParser &parser) {
  parser.tag[0] = '\0';
  parser.length = 0;
  parser.collecting = false;
}

bool feedMusicBrainzReleaseGroupParser(MusicBrainzReleaseGroupParser &parser,
                                      const uint8_t *data, size_t length,
                                      char mbid[37]) {
  if (data == nullptr || mbid == nullptr) return false;
  static const char prefix[] = "<release-group";
  for (size_t index = 0; index < length; ++index) {
    const char c = static_cast<char>(data[index]);
    if (!parser.collecting) {
      if (c != '<') continue;
      parser.collecting = true;
      parser.length = 0;
    }
    if (parser.length + 1U >= sizeof(parser.tag)) {
      resetMusicBrainzReleaseGroupParser(parser);
      if (c == '<') {
        parser.collecting = true;
        parser.tag[parser.length++] = c;
      }
      continue;
    }
    parser.tag[parser.length++] = c;
    if (c != '>') continue;
    parser.tag[parser.length] = '\0';

    const size_t prefixLength = sizeof(prefix) - 1U;
    const bool releaseGroupTag = parser.length > prefixLength &&
        memcmp(parser.tag, prefix, prefixLength) == 0 &&
        isAsciiSpace(parser.tag[prefixLength]);
    if (releaseGroupTag) {
      const char *cursor = parser.tag + prefixLength;
      const char *end = parser.tag + parser.length;
      while (cursor < end) {
        while (cursor < end && isAsciiSpace(*cursor)) ++cursor;
        const char *name = cursor;
        while (cursor < end && (isalnum(static_cast<unsigned char>(*cursor)) ||
                                *cursor == '-' || *cursor == ':' || *cursor == '_')) ++cursor;
        const size_t nameLength = static_cast<size_t>(cursor - name);
        while (cursor < end && isAsciiSpace(*cursor)) ++cursor;
        if (cursor >= end || *cursor != '=') {
          while (cursor < end && !isAsciiSpace(*cursor) && *cursor != '>') ++cursor;
          continue;
        }
        ++cursor;
        while (cursor < end && isAsciiSpace(*cursor)) ++cursor;
        if (cursor >= end || (*cursor != '"' && *cursor != '\'')) continue;
        const char quote = *cursor++;
        const char *value = cursor;
        while (cursor < end && *cursor != quote) ++cursor;
        const size_t valueLength = static_cast<size_t>(cursor - value);
        if (nameLength == 2U && name[0] == 'i' && name[1] == 'd' && valueLength == 36U) {
          bool valid = true;
          for (size_t i = 0; i < 36U; ++i) {
            const bool hyphen = i == 8U || i == 13U || i == 18U || i == 23U;
            if ((hyphen && value[i] != '-') ||
                (!hyphen && !isxdigit(static_cast<unsigned char>(value[i])))) {
              valid = false;
              break;
            }
          }
          if (valid) {
            memcpy(mbid, value, 36U);
            mbid[36] = '\0';
            resetMusicBrainzReleaseGroupParser(parser);
            return true;
          }
        }
        if (cursor < end) ++cursor;
      }
    }
    resetMusicBrainzReleaseGroupParser(parser);
  }
  return false;
}

void resetAppleArtworkUrlParser(AppleArtworkUrlParser &parser) {
  parser.value[0] = '\0';
  parser.length = 0;
  parser.keyMatched = 0;
  parser.state = 0;
  parser.escaped = false;
}

bool feedAppleArtworkUrlParser(AppleArtworkUrlParser &parser,
                               const uint8_t *data, size_t length) {
  if (data == nullptr) return false;
  static const char key[] = "\"artworkUrl100\"";
  for (size_t index = 0; index < length; ++index) {
    const char c = static_cast<char>(data[index]);
    if (parser.state == 0) {
      if (c == key[parser.keyMatched]) {
        if (++parser.keyMatched == sizeof(key) - 1U) parser.state = 1;
      } else parser.keyMatched = c == key[0] ? 1U : 0U;
      continue;
    }
    if (parser.state == 1) {
      if (c == ':') parser.state = 2;
      else if (!isspace(static_cast<unsigned char>(c))) resetAppleArtworkUrlParser(parser);
      continue;
    }
    if (parser.state == 2) {
      if (c == '"') parser.state = 3;
      else if (!isspace(static_cast<unsigned char>(c))) resetAppleArtworkUrlParser(parser);
      continue;
    }
    if (parser.escaped) {
      if (c != '/' && c != '\\' && c != '"') {
        resetAppleArtworkUrlParser(parser);
        continue;
      }
      if (parser.length + 1U >= sizeof(parser.value)) {
        resetAppleArtworkUrlParser(parser);
        continue;
      }
      parser.value[parser.length++] = c;
      parser.escaped = false;
    } else if (c == '\\') parser.escaped = true;
    else if (c == '"') {
      parser.value[parser.length] = '\0';
      return parser.length > 0;
    } else if (parser.length + 1U < sizeof(parser.value)) {
      parser.value[parser.length++] = c;
    } else resetAppleArtworkUrlParser(parser);
  }
  return false;
}

uint32_t artworkBitmapCrc32(const uint8_t *data, size_t size) {
  if (data == nullptr && size != 0) return 0;
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

bool validArtworkBitmapPacket(const uint8_t *data, size_t size) {
  if (data == nullptr || size != ARTWORK_BITMAP_PACKET_BYTES) return false;
  if (memcmp(data, "MAB1", 4) != 0 || data[4] != 60 || data[5] != 60 ||
      data[6] != 88 || data[7] != 88) return false;
  const uint16_t smallBytes = (static_cast<uint16_t>(data[8]) << 8U) | data[9];
  const uint16_t largeBytes = (static_cast<uint16_t>(data[10]) << 8U) | data[11];
  if (smallBytes != ARTWORK_BITMAP_SMALL_BYTES ||
      largeBytes != ARTWORK_BITMAP_LARGE_BYTES) return false;
  const uint32_t expected = (static_cast<uint32_t>(data[12]) << 24U) |
      (static_cast<uint32_t>(data[13]) << 16U) |
      (static_cast<uint32_t>(data[14]) << 8U) | data[15];
  return expected == artworkBitmapCrc32(
      data + ARTWORK_BITMAP_HEADER_BYTES,
      ARTWORK_BITMAP_SMALL_BYTES + ARTWORK_BITMAP_LARGE_BYTES);
}

}  // namespace MilestoneCoreLogic
