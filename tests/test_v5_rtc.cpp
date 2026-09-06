#include "MilestoneV5Rtc.h"
#include <cassert>
int main() {
  uint8_t r[] = {0x59, 0x58, 0x71, 1, 0x29, 0x02, 0x24};
  MilestoneV5::RtcTime t;
  assert(MilestoneV5::decodeRtc(r, 7, 0, t) && t.hour == 23);
  r[2] = 0x52; assert(MilestoneV5::decodeRtc(r, 7, 0, t) && t.hour == 0);
  r[6] = 0x25; assert(!MilestoneV5::decodeRtc(r, 7, 0, t));
  r[4] = 0x28; assert(MilestoneV5::decodeRtc(r, 7, 0, t));
  assert(!MilestoneV5::decodeRtc(r, 7, 0x80, t));
  r[0] = 0x6A; assert(!MilestoneV5::decodeRtc(r, 7, 0, t));
}
