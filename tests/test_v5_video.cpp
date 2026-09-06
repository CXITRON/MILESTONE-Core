#include "MilestoneV5Video.h"
#include <cassert>
int main(){
  uint8_t b[]={ 'M','V','J','1',128,0,128,0,15,0,0,0,2,0,0,0 };
  MilestoneV5::VideoInfo v{};
  assert(MilestoneV5::decodeVideoHeader(b,16,v)&&v.frames==2);
  assert(!MilestoneV5::decodeVideoHeader(b,15,v));
  b[8]=0;assert(!MilestoneV5::decodeVideoHeader(b,16,v));
  b[8]=31;assert(!MilestoneV5::decodeVideoHeader(b,16,v));
  b[8]=15;b[12]=0;assert(!MilestoneV5::decodeVideoHeader(b,16,v));
}
