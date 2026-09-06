#include "MilestoneV5Bundle.h"
#include "MilestoneV5OtaWire.h"
#include "MilestoneV5Protocol.h"
#include <string.h>
namespace MilestoneV5 {
bool validFirmwareSetId(const char *id, bool allowEmpty) {
  if (!id)
    return false;
  if (!id[0])
    return allowEmpty;
  for (unsigned i = 0; i < 16; ++i)
    if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
      return false;
  return id[16] == 0;
}
bool encodeBundleJournal(const BundleJournal &r, uint8_t *out,
                         size_t capacity) {
  if (!out || capacity < kBundleJournalSize || uint8_t(r.stage) < 1 ||
      uint8_t(r.stage) > 6 || !validFirmwareSetId(r.setId) ||
      r.mainAddress < 0x10000 || r.mainAddress >= 0x1000000 ||
      r.mainAddress % 4096 || !r.mainBytes ||
      r.mainBytes > 0x1000000 - r.mainAddress ||
      (r.stage == BundleStage::ZeroPending && (!r.hasZero || !r.zeroTransfer)))
    return false;
  memset(out, 0, kBundleJournalSize);
  memcpy(out, "VB01", 4);
  out[4] = uint8_t(r.stage);
  out[5] = r.hasZero ? 1 : 0;
  otaPut32(out + 8, r.mainAddress);
  otaPut32(out + 12, r.mainBytes);
  otaPut32(out + 16, r.zeroTransfer);
  memcpy(out + 20, r.mainSha256, 32);
  memcpy(out + 52, r.zeroSha256, 32);
  memcpy(out + 84, r.setId, 17);
  otaPut32(out + 124, crc32(out, 124));
  return true;
}
bool decodeBundleJournal(const uint8_t *p, size_t n, BundleJournal &out) {
  if (!p || n != kBundleJournalSize || memcmp(p, "VB01", 4) || p[4] < 1 ||
      p[4] > 6 || p[5] > 1 || otaU32(p + 124) != crc32(p, 124))
    return false;
  BundleJournal r{};
  r.stage = static_cast<BundleStage>(p[4]);
  r.hasZero = p[5];
  r.mainAddress = otaU32(p + 8);
  r.mainBytes = otaU32(p + 12);
  r.zeroTransfer = otaU32(p + 16);
  memcpy(r.mainSha256, p + 20, 32);
  memcpy(r.zeroSha256, p + 52, 32);
  memcpy(r.setId, p + 84, 17);
  uint8_t check[kBundleJournalSize];
  if (!encodeBundleJournal(r, check, sizeof(check)))
    return false;
  out = r;
  return true;
}
bool encodeStableIndex(const StableIndex &i, uint8_t *out, size_t capacity) {
  if (!out || capacity < kStableIndexSize ||
      !validFirmwareSetId(i.mainStable) ||
      !validFirmwareSetId(i.zeroStable, true) ||
      !validFirmwareSetId(i.mainBackup, true) ||
      !validFirmwareSetId(i.zeroBackup, true))
    return false;
  memset(out, 0, kStableIndexSize);
  memcpy(out, "VS01", 4);
  otaPut32(out + 4, i.generation);
  memcpy(out + 8, i.mainStable, 17);
  memcpy(out + 25, i.zeroStable, 17);
  memcpy(out + 42, i.mainBackup, 17);
  memcpy(out + 59, i.zeroBackup, 17);
  otaPut32(out + 92, crc32(out, 92));
  return true;
}
bool decodeStableIndex(const uint8_t *p, size_t n, StableIndex &out) {
  if (!p || n != kStableIndexSize || memcmp(p, "VS01", 4) ||
      otaU32(p + 92) != crc32(p, 92))
    return false;
  StableIndex i{};
  i.generation = otaU32(p + 4);
  memcpy(i.mainStable, p + 8, 17);
  memcpy(i.zeroStable, p + 25, 17);
  memcpy(i.mainBackup, p + 42, 17);
  memcpy(i.zeroBackup, p + 59, 17);
  uint8_t check[kStableIndexSize];
  if (!encodeStableIndex(i, check, sizeof(check)))
    return false;
  out = i;
  return true;
}
} // namespace MilestoneV5
