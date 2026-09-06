#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MilestoneV5 {
enum class ManifestTarget : uint8_t { Main, Zero };
struct SignedImageManifest {
  ManifestTarget target;
  uint16_t major, minor, patch;
  uint32_t bytes;
  uint8_t minimumPeerProtocol, maximumPeerProtocol;
  uint8_t sha256[32];
};
// Exact canonical text, bounded input; parsing does NOT verify its signature.
bool decodeImageManifest(const uint8_t *text, size_t size,
                         SignedImageManifest &out);
} // namespace MilestoneV5
