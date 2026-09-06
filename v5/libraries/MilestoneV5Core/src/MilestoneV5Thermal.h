#pragma once
#include <math.h>
#include <stdint.h>
namespace MilestoneV5 {
class ThermalPolicy {
public:
  explicit ThermalPolicy(bool zero) : offset_(zero ? 5 : 0) {}
  bool valid = false, warning = false, throttled = false, stopped = false;
  void sample(float c) {
    valid = isfinite(c) && c >= -40 && c <= 125;
    if (!valid) {
      if (faults_ < 3)
        ++faults_;
      if (faults_ >= 3) {
        warning = throttled = stopped = true;
      }
      return;
    }
    faults_ = 0;
    if (c >= 70 + offset_)
      warning = true;
    else if (c < 65 + offset_)
      warning = false;
    if (c >= 80 + offset_)
      throttled = true;
    else if (c < 75 + offset_)
      throttled = false;
    if (c >= 90 + offset_)
      stopped = true;
    else if (c < 70 + offset_)
      stopped = false;
  }

private:
  uint8_t faults_ = 0, offset_;
};
} // namespace MilestoneV5
