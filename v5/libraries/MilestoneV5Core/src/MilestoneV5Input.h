#pragma once
#include <atomic>
#include <stdint.h>

namespace MilestoneV5 {
// One sampler owns the debounce state; loopTask alone consumes actions.
// Coalescing bounds delayed input to one pending press per physical button.
class ButtonLatch {
public:
  void begin(bool down, uint32_t now) {
    stable_ = sample_ = down;
    changed_ = now;
    pending_.store(false);
  }
  void sample(bool down, uint32_t now) {
    if (down != sample_) {
      sample_ = down;
      changed_ = now;
    }
    if (down != stable_ && uint32_t(now - changed_) >= 30) {
      stable_ = down;
      if (down)
        pending_.store(true, std::memory_order_release);
    }
  }
  bool pending() const { return pending_.load(std::memory_order_acquire); }
  bool take() { return pending_.exchange(false, std::memory_order_acq_rel); }

private:
  bool stable_ = false, sample_ = false;
  uint32_t changed_ = 0;
  std::atomic<bool> pending_{false};
};

inline bool profileAllowsBle(uint8_t profile, bool portal, bool safety,
                             bool policyKnown) {
  return policyKnown && profile == 2 && !portal && !safety;
}
} // namespace MilestoneV5
