#include "MilestoneV5Now.h"
#include <string.h>
namespace MilestoneV5 {
void copyNowText(char *dest, size_t cap, const char *source) {
  if (!dest || !cap)
    return;
  dest[0] = 0;
  if (!source)
    return;
  size_t n = strlen(source);
  if (n >= cap) {
    n = cap - 1;
    while (n && (uint8_t(source[n]) & 0xC0) == 0x80)
      --n;
  }
  memcpy(dest, source, n);
  dest[n] = 0;
}
bool encodeNow(const NowMetadata &v, uint8_t *b, size_t cap, size_t &size) {
  size = 0;
  const size_t a = strnlen(v.title, 145), c = strnlen(v.artist, 145),
               d = strnlen(v.album, 145);
  if (!b || a > 144 || c > 144 || d > 144 || cap < 12 + a + c + d)
    return false;
  b[0] = uint8_t(v.connected) | uint8_t(v.ready) << 1 | uint8_t(v.playing) << 2;
  b[1] = a;
  b[2] = c;
  b[3] = d;
  for (size_t i = 0; i < 4; ++i) {
    b[4 + i] = v.elapsedSeconds >> (8 * i);
    b[8 + i] = v.durationSeconds >> (8 * i);
  }
  memcpy(b + 12, v.title, a);
  memcpy(b + 12 + a, v.artist, c);
  memcpy(b + 12 + a + c, v.album, d);
  size = 12 + a + c + d;
  return true;
}
bool decodeNow(const uint8_t *b, size_t size, NowMetadata &v) {
  if (!b || size < 12 || b[0] > 7 || b[1] > 144 || b[2] > 144 || b[3] > 144 ||
      size != size_t(12) + b[1] + b[2] + b[3])
    return false;
  if (memchr(b + 12, 0, size - 12))
    return false;
  NowMetadata decoded{};
  decoded.connected = b[0] & 1;
  decoded.ready = b[0] & 2;
  decoded.playing = b[0] & 4;
  for (size_t i = 0; i < 4; ++i) {
    decoded.elapsedSeconds |= uint32_t(b[4 + i]) << (8 * i);
    decoded.durationSeconds |= uint32_t(b[8 + i]) << (8 * i);
  }
  memcpy(decoded.title, b + 12, b[1]);
  memcpy(decoded.artist, b + 12 + b[1], b[2]);
  memcpy(decoded.album, b + 12 + b[1] + b[2], b[3]);
  v = decoded;
  return true;
}
} // namespace MilestoneV5
