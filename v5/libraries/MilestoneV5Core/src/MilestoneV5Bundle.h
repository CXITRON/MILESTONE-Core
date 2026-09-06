#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MilestoneV5 {
struct SignedBundleManifest {
  uint16_t major, minor, patch;
  bool hasZero;
  uint8_t mainSha256[32], zeroSha256[32];
};
bool decodeBundleManifest(const uint8_t *text, size_t size,
                          SignedBundleManifest &out);
enum class BundleStage : uint8_t {
  MainPending = 1,
  MainVerified,
  ZeroPending,
  PromotePending,
  Complete,
  Failed
};
struct BundleJournal {
  BundleStage stage;
  bool hasZero;
  uint32_t mainAddress, mainBytes, zeroTransfer;
  uint8_t mainSha256[32], zeroSha256[32];
  char setId[17];
};
constexpr size_t kBundleJournalSize = 128, kStableIndexSize = 96;
bool validFirmwareSetId(const char *id, bool allowEmpty = false);
bool encodeBundleJournal(const BundleJournal &record, uint8_t *out,
                         size_t capacity);
bool decodeBundleJournal(const uint8_t *data, size_t size,
                         BundleJournal &record);
struct StableIndex {
  uint32_t generation;
  char mainStable[17], zeroStable[17], mainBackup[17], zeroBackup[17];
};
bool encodeStableIndex(const StableIndex &index, uint8_t *out, size_t capacity);
bool decodeStableIndex(const uint8_t *data, size_t size, StableIndex &index);
} // namespace MilestoneV5
