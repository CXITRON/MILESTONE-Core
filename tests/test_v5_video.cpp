#include "MilestoneV5Video.h"
#include <cassert>
int main(){
  uint8_t b[]={ 'M','V','J','1',128,0,128,0,15,0,0,0,2,0,0,0 };
  MilestoneV5::VideoInfo v{};
  assert(MilestoneV5::decodeVideoHeader(b,16,v)&&v.frames==2);
  assert(MilestoneV5::videoFrameAtMs(v,0)==0);
  assert(MilestoneV5::videoFrameAtMs(v,66)==0);
  assert(MilestoneV5::videoFrameAtMs(v,67)==1);
  assert(MilestoneV5::videoFrameAtMs(v,9999)==1);
  assert(MilestoneV5::synchronizedVideoPositionMs(100,1000,1250,1000,true)==350);
  assert(MilestoneV5::synchronizedVideoPositionMs(100,1000,1250,1000,false)==100);
  assert(MilestoneV5::synchronizedVideoPositionMs(900,1000,1250,1000,true)==1000);
  assert(MilestoneV5::synchronizedVideoPositionMs(100,0xfffffff0U,0x10U,1000,true)==132);
  assert(!MilestoneV5::synchronizedVideoControlStale(1200,1000,250,false));
  assert(!MilestoneV5::synchronizedVideoControlStale(1250,1000,250,true));
  assert(MilestoneV5::synchronizedVideoControlStale(1251,1000,250,true));
  // A control received later in the loop is newer than the loop timestamp.
  // It must neither time out immediately nor jump to the final frame.
  assert(!MilestoneV5::synchronizedVideoControlStale(1000,1001,2500,true));
  assert(MilestoneV5::synchronizedVideoPositionMs(12000,1001,1000,60000,true)==12000);
  assert(!MilestoneV5::synchronizedVideoControlStale(0xfffffff0U,0x10U,2500,true));
  MilestoneV5::VideoInfo longVideo{128,128,20,432000};
  assert(MilestoneV5::videoFrameAtMs(longVideo,21599999)==431999);
  assert(MilestoneV5::videoFrameAtMs(longVideo,21600000)==431999);
  assert(!MilestoneV5::decodeVideoHeader(b,15,v));
  b[8]=0;assert(!MilestoneV5::decodeVideoHeader(b,16,v));
  b[8]=31;assert(!MilestoneV5::decodeVideoHeader(b,16,v));
  b[8]=15;b[12]=0;assert(!MilestoneV5::decodeVideoHeader(b,16,v));
  MilestoneV5::VideoInfo empty{};
  assert(MilestoneV5::videoFrameAtMs(empty,100)==0);
}
