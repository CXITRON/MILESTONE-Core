#include <MilestoneV5Artwork.h>
#include <MilestoneV5Calendar.h>
#include <MilestoneV5Thermal.h>
#include <MilestoneV5LogRecord.h>
#include <MilestoneV5OtaWire.h>
#include <cassert>
#include <vector>
#include <limits>
int main(){
  using namespace MilestoneV5;
  assert(validDate(2024,2,29));assert(!validDate(2025,2,29));assert(!validDate(2100,1,1));
  assert(!validDate(2026,0,1));assert(!validDate(2026,4,31));assert(!validDate(2026,1,0));
  assert(dayOrdinal(2000,1,1)==0);assert(dayOrdinal(2025,1,1)-dayOrdinal(2024,1,1)==366);
  assert(dayOrdinal(2026,3,1)-dayOrdinal(2026,2,28)==1);
  ThermalPolicy main(false),zero(true);
  main.sample(80);assert(main.throttled&&!main.stopped);main.sample(76);assert(main.throttled);
  main.sample(74);assert(!main.throttled);main.sample(90);assert(main.stopped);
  main.sample(71);assert(main.stopped);main.sample(69);assert(!main.stopped);
  zero.sample(90);assert(!zero.stopped);zero.sample(95);assert(zero.stopped);
  zero.sample(74);assert(!zero.stopped);
  for(int i=0;i<2;++i)zero.sample(std::numeric_limits<float>::quiet_NaN());
  assert(!zero.valid&&!zero.stopped);zero.sample(126);assert(zero.stopped);
  zero.sample(60);assert(zero.valid&&!zero.stopped);
  std::vector<uint8_t> packet(kArtworkBytes,0);memcpy(packet.data(),"MAC1",4);
  packet[4]=packet[5]=60;packet[6]=packet[7]=88;packet[8]=7200>>8;packet[9]=7200&255;
  packet[10]=15488>>8;packet[11]=15488&255;
  const uint32_t crc=crc32(packet.data()+16,packet.size()-16);
  for(unsigned i=0;i<4;++i)packet[12+i]=crc>>(24-i*8);
  assert(validArtwork(packet.data(),packet.size()));assert(!validArtwork(nullptr,packet.size()));
  assert(!validArtwork(packet.data(),packet.size()-1));packet.back()^=1;assert(!validArtwork(packet.data(),packet.size()));
  packet.back()^=1;packet[4]=61;assert(!validArtwork(packet.data(),packet.size()));
  uint8_t log[kLogRecordSize];const char row[]="12:00:00,23,50,1013,1\n";
  assert(encodeLogRecord(2026,9,5,123,row,strlen(row),log,sizeof(log)));
  assert(validLogRecord(log,sizeof(log)));assert(!validLogRecord(log,sizeof(log)-1));
  log[14]^=1;assert(!validLogRecord(log,sizeof(log)));
  assert(!encodeLogRecord(2026,2,29,0,row,strlen(row),log,sizeof(log)));
  assert(!encodeLogRecord(2026,9,5,0,row,0,log,sizeof(log)));
  uint8_t request[469]{};request[0]=1;otaPut32(request+1,42);request[5]=100;request[7]=64;
  assert(validOtaRequest(request,9));assert(!validOtaRequest(request,8));
  request[0]=2;assert(validOtaRequest(request,469));assert(!validOtaRequest(request,470));assert(!validOtaRequest(request,9));
  request[0]=5;assert(validOtaRequest(request,5));assert(!validOtaRequest(request,6));
  otaPut32(request+1,0);assert(!validOtaRequest(request,5));
  assert(!validOtaRequest(nullptr,0));
}
