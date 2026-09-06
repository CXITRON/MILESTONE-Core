#include "MilestoneV5Now.h"
#include <cassert>
#include <cstring>
int main() {
  MilestoneV5::NowMetadata a{}, b{};
  MilestoneV5::copyNowText(a.title,sizeof(a.title),"한글 日本語");
  a.connected=true;a.ready=true;a.playing=true;a.durationSeconds=300;a.elapsedSeconds=12;
  uint8_t data[444];size_t n;
  assert(MilestoneV5::encodeNow(a,data,sizeof(data),n));
  assert(MilestoneV5::decodeNow(data,n,b));
  assert(!strcmp(a.title,b.title)&&b.durationSeconds==300&&b.playing);
  assert(!MilestoneV5::decodeNow(data,n-1,b));
  data[1]=255;assert(!MilestoneV5::decodeNow(data,n,b));
  char small[5];MilestoneV5::copyNowText(small,sizeof(small),"한글");
  assert(!strcmp(small,"한"));
}
