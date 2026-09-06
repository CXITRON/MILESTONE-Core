#include <MilestoneV5Bundle.h>
#include <cassert>
#include <string>
#include <cstring>
int main(){
  using namespace MilestoneV5;SignedBundleManifest bundle{};const std::string hash(64,'a');
  auto parse=[&](std::string text){return decodeBundleManifest(reinterpret_cast<const uint8_t*>(text.data()),text.size(),bundle);};
  assert(parse("MILESTONE-V5 BUNDLE 5.0.0 "+hash+" NONE\n"));assert(!bundle.hasZero);
  assert(parse("MILESTONE-V5 BUNDLE 5.0.0 "+hash+" "+hash+"\n"));assert(bundle.hasZero);
  assert(!parse("MILESTONE-V5 BUNDLE 05.0.0 "+hash+" NONE\n"));
  assert(!parse("MILESTONE-V5 BUNDLE 5.0.0 "+hash+" NONE\nextra"));
  assert(!validFirmwareSetId("../../anything"));assert(!validFirmwareSetId(""));assert(validFirmwareSetId("",true));
  BundleJournal record{};record.stage=BundleStage::MainPending;record.mainAddress=0x310000;record.mainBytes=123456;strcpy(record.setId,"0123456789abcdef");
  uint8_t bytes[kBundleJournalSize];assert(encodeBundleJournal(record,bytes,sizeof(bytes)));BundleJournal decoded{};assert(decodeBundleJournal(bytes,sizeof(bytes),decoded));assert(decoded.mainBytes==123456);
  bytes[20]^=1;assert(!decodeBundleJournal(bytes,sizeof(bytes),decoded));
  record.stage=BundleStage::ZeroPending;assert(!encodeBundleJournal(record,bytes,sizeof(bytes)));record.hasZero=true;record.zeroTransfer=123;assert(encodeBundleJournal(record,bytes,sizeof(bytes)));
  StableIndex index{};index.generation=42;strcpy(index.mainStable,"0123456789abcdef");uint8_t saved[kStableIndexSize];assert(encodeStableIndex(index,saved,sizeof(saved)));StableIndex read{};assert(decodeStableIndex(saved,sizeof(saved),read));assert(read.generation==42&&read.zeroStable[0]==0);
  saved[25]^=1;assert(!decodeStableIndex(saved,sizeof(saved),read));
}
