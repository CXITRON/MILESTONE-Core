#include "MilestoneV5Manifest.h"
#include "MilestoneV5Bundle.h"
#include <string.h>
namespace MilestoneV5 {
namespace {
bool number(const uint8_t *p, size_t n, size_t &cursor, uint32_t maximum,
            uint32_t &value, uint8_t separator) {
  size_t start = cursor;
  value = 0;
  while (cursor < n && p[cursor] >= '0' && p[cursor] <= '9') {
    uint32_t digit = p[cursor++] - '0';
    if (value > (maximum - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  if (cursor == start || cursor >= n || p[cursor++] != separator ||
      (cursor - start > 2 && p[start] == '0'))
    return false;
  return true;
}
int hex(uint8_t c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                : -1;
}
} // namespace
bool decodeImageManifest(const uint8_t *text, size_t size,
                         SignedImageManifest &out) {
  if (!text || size < 90 || size > 255 || memcmp(text, "MILESTONE-V5 ", 13))
    return false;
  SignedImageManifest m{};
  size_t cursor = 13;
  if (!memcmp(text + cursor, "MAIN ", 5))
    m.target = ManifestTarget::Main;
  else if (!memcmp(text + cursor, "ZERO ", 5))
    m.target = ManifestTarget::Zero;
  else
    return false;
  cursor += 5;
  uint32_t major, minor, patch;
  if (!number(text, size, cursor, 65535, major, '.') ||
      !number(text, size, cursor, 65535, minor, '.') ||
      !number(text, size, cursor, 65535, patch, ' ') ||
      !number(text, size, cursor, UINT32_MAX, m.bytes, ' ') || !m.bytes ||
      size - cursor < 69 || text[cursor + 64] != ' ')
    return false;
  m.major = major;
  m.minor = minor;
  m.patch = patch;
  for (unsigned i = 0; i < 32; ++i) {
    int a = hex(text[cursor + i * 2]), b = hex(text[cursor + i * 2 + 1]);
    if (a < 0 || b < 0)
      return false;
    m.sha256[i] = uint8_t(a * 16 + b);
  }
  cursor += 65;
  uint32_t minimum, maximum;
  if (!number(text, size, cursor, 255, minimum, ' ') ||
      !number(text, size, cursor, 255, maximum, '\n') || !minimum ||
      minimum > maximum || cursor != size)
    return false;
  m.minimumPeerProtocol = minimum;
  m.maximumPeerProtocol = maximum;
  out = m;
  return true;
}
bool decodeBundleManifest(const uint8_t *text, size_t size,
                          SignedBundleManifest &out) {
  if (!text || size < 96 || size > 255 ||
      memcmp(text, "MILESTONE-V5 BUNDLE ", 20))
    return false;
  SignedBundleManifest b{};
  size_t cursor = 20;
  uint32_t major, minor, patch;
  if (!number(text, size, cursor, 65535, major, '.') ||
      !number(text, size, cursor, 65535, minor, '.') ||
      !number(text, size, cursor, 65535, patch, ' ') || size - cursor < 70 ||
      text[cursor + 64] != ' ')
    return false;
  b.major = major;
  b.minor = minor;
  b.patch = patch;
  for (unsigned i = 0; i < 32; ++i) {
    int a = hex(text[cursor + i * 2]), c = hex(text[cursor + i * 2 + 1]);
    if (a < 0 || c < 0)
      return false;
    b.mainSha256[i] = a * 16 + c;
  }
  cursor += 65;
  if (size - cursor == 5 && !memcmp(text + cursor, "NONE\n", 5)) {
    out = b;
    return true;
  }
  if (size - cursor != 65 || text[size - 1] != '\n')
    return false;
  for (unsigned i = 0; i < 32; ++i) {
    int a = hex(text[cursor + i * 2]), c = hex(text[cursor + i * 2 + 1]);
    if (a < 0 || c < 0)
      return false;
    b.zeroSha256[i] = a * 16 + c;
  }
  b.hasZero = true;
  out = b;
  return true;
}
} // namespace MilestoneV5
