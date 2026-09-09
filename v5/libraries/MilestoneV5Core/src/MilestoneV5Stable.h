#pragma once
#include "MilestoneV5Bundle.h"
#include <string.h>

namespace MilestoneV5 {
// Domain-separated administrator designation; a normal release signature is
// not authority to replace the curated stable image.
inline bool decodeStableDesignation(const uint8_t *text, size_t size,
                                    SignedBundleManifest &out) {
  if (!text || size < 20 || size > 255 ||
      memcmp(text, "MILESTONE-V5 STABLE ", 20))
    return false;
  uint8_t copy[255];
  memcpy(copy, text, size);
  memcpy(copy + 13, "BUNDLE", 6);
  return decodeBundleManifest(copy, size, out);
}
inline bool sameBundle(const SignedBundleManifest &a,
                       const SignedBundleManifest &b) {
  return a.major == b.major && a.minor == b.minor && a.patch == b.patch &&
         a.hasZero == b.hasZero && !memcmp(a.mainSha256, b.mainSha256, 32) &&
         (!a.hasZero || !memcmp(a.zeroSha256, b.zeroSha256, 32));
}
}
