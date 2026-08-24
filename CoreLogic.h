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
bool utf8ContainsHangul(const char *text);
bool shouldIgnoreLateBluetoothEncryptionFailure(bool linkAlreadySecured,
                                                int errorStatus,
                                                int timeoutStatus);
bool shouldRetryTransientHttpFailure(int responseCode, bool responseComplete,
                                     uint8_t attempt, uint8_t maximumAttempts);

constexpr size_t ARTWORK_BITMAP_HEADER_BYTES = 16;
constexpr size_t ARTWORK_BITMAP_SMALL_BYTES = 480;
constexpr size_t ARTWORK_BITMAP_LARGE_BYTES = 968;
constexpr size_t ARTWORK_BITMAP_PACKET_BYTES = ARTWORK_BITMAP_HEADER_BYTES +
    ARTWORK_BITMAP_SMALL_BYTES + ARTWORK_BITMAP_LARGE_BYTES;
uint32_t artworkBitmapCrc32(const uint8_t *data, size_t size);
bool validArtworkBitmapPacket(const uint8_t *data, size_t size);

struct MusicBrainzReleaseGroupParser {
  char tag[256];
  uint16_t length;
  bool collecting;
};

void resetMusicBrainzReleaseGroupParser(MusicBrainzReleaseGroupParser &parser);
bool feedMusicBrainzReleaseGroupParser(MusicBrainzReleaseGroupParser &parser,
                                      const uint8_t *data, size_t length,
                                      char mbid[37]);

struct AppleArtworkUrlParser {
  char value[512];
  uint16_t length;
  uint8_t keyMatched;
  uint8_t state;
  bool escaped;
};

void resetAppleArtworkUrlParser(AppleArtworkUrlParser &parser);
bool feedAppleArtworkUrlParser(AppleArtworkUrlParser &parser,
                               const uint8_t *data, size_t length);

}  // namespace MilestoneCoreLogic
