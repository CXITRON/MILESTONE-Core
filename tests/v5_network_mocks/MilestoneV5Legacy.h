#pragma once
#include <stdint.h>
namespace MilestoneV5 {
constexpr unsigned kLegacySnapshotBytes = 416;
inline void importLegacyNetworks() {}
inline bool legacySnapshot(uint8_t *) { return false; }
} // namespace MilestoneV5
