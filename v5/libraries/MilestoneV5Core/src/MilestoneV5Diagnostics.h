#pragma once
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Video.h>
#include <Preferences.h>
namespace MilestoneV5 {
class Diagnostics {
public:
  struct Event {
    uint32_t code = 0, value = 0, uptime = 0, epoch = 0;
  };
  Event events[16]{};
  uint8_t count = 0, head = 0;
  void begin() {
    Preferences p;
    if (!p.begin("v5_diag", true))
      return;
    uint8_t a[276], b[276];
    bool av = p.getBytesLength("a") == sizeof(a) &&
              p.getBytes("a", a, sizeof(a)) == sizeof(a) && valid(a),
         bv = p.getBytesLength("b") == sizeof(b) &&
              p.getBytes("b", b, sizeof(b)) == sizeof(b) && valid(b);
    p.end();
    if (!av && !bv)
      return;
    activeB =
        !av || (bv && int32_t(readVideoU32(b + 4) - readVideoU32(a + 4)) > 0);
    const uint8_t *r = activeB ? b : a;
    generation = readVideoU32(r + 4);
    count = r[8];
    head = r[9];
    for (unsigned i = 0; i < 16; ++i) {
      const uint8_t *v = r + 16 + i * 16;
      events[i] = {readVideoU32(v), readVideoU32(v + 4), readVideoU32(v + 8),
                   readVideoU32(v + 12)};
    }
  }
  void note(uint32_t code, uint32_t value, uint32_t now, uint32_t epoch) {
    for (unsigned i = 0; i < count; ++i) {
      const auto &e = events[(head + 15 - i) % 16];
      if (e.code == code && e.value == value && now - e.uptime < 60000)
        return;
    }
    Event previous = events[head];
    uint8_t oldHead = head, oldCount = count;
    events[head] = {code, value, now, epoch >= 1704067200 ? epoch : 0};
    head = (head + 1) % 16;
    if (count < 16)
      ++count;
    if (!save()) {
      head = oldHead;
      count = oldCount;
      events[head] = previous;
    }
  }
  bool clear() {
    uint8_t previous = count;
    count = 0;
    if (save())
      return true;
    count = previous;
    return false;
  }

private:
  bool activeB = false;
  uint32_t generation = 0;
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (8 * i);
  }
  static bool valid(const uint8_t *r) {
    return !memcmp(r, "VD01", 4) && r[8] <= 16 && r[9] < 16 &&
           crc32(r, 272) == readVideoU32(r + 272);
  }
  bool save() {
    uint8_t r[276]{};
    memcpy(r, "VD01", 4);
    put(r + 4, generation + 1);
    r[8] = count;
    r[9] = head;
    for (unsigned i = 0; i < 16; ++i) {
      uint8_t *v = r + 16 + i * 16;
      put(v, events[i].code);
      put(v + 4, events[i].value);
      put(v + 8, events[i].uptime);
      put(v + 12, events[i].epoch);
    }
    put(r + 272, crc32(r, 272));
    uint8_t check[276];
    Preferences p;
    const char *key = activeB ? "a" : "b";
    bool ok = p.begin("v5_diag", false) &&
              p.putBytes(key, r, sizeof(r)) == sizeof(r) &&
              p.getBytes(key, check, sizeof(check)) == sizeof(check) &&
              !memcmp(r, check, sizeof(r));
    p.end();
    if (ok) {
      ++generation;
      activeB = !activeB;
    }
    return ok;
  }
};
} // namespace MilestoneV5
