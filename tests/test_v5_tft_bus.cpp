#include "../v5/MilestoneV5Main/V5Tft.h"
#include <cassert>
#include <iostream>
int main() {
  SPIClass bus;
  BusMock::pins[10] = HIGH;
  SimpleSt7735 display(bus, 47, 21, 6, 10);
  display.setTone(100, 0);
  display.begin();
  display.writeFillRect(0, 0, 128, 160, 0xf800);
  for (unsigned i = 0; i < 20; ++i) {
    // SD changes clock between consecutive partial TFT updates.
    bus.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    digitalWrite(10, LOW);
    uint8_t bytes[512]{};
    bus.writeBytes(bytes, sizeof(bytes));
    digitalWrite(10, HIGH);
    bus.endTransaction();
    display.flush();
    assert(!bus.active && BusMock::pins[47] == HIGH);
  }
  assert(display.flushBytes == 128 * 160 * 2);
  assert(bus.largestWrite == 128); // one write per changed tile, not per byte
  const auto previous = display.flushBytes;
  display.flush();
  assert(display.flushBytes == previous);
  display.flushRegion(16, 128);
  assert(display.flushBytes == previous + 32768);
  assert(bus.largestWrite == 256);
  assert(BusMock::pins[47] == HIGH);
  display.sleep(true);
  const auto paused = display.flushBytes;
  display.flushRegion(16, 128);
  assert(display.flushBytes == paused);
  display.sleep(false);
  BusMock::noMemory = true;
  SimpleSt7735 fallback(bus, 47, 21, 6, 10);
  fallback.begin();
  uint8_t mono[2048]{};
  fallback.blitMono(mono, 16,
                    0xffff); // allocation failure must not dereference null
  std::cout << "TFT shared-bus ownership, transfer batching, shadow and "
               "allocation tests passed\n";
}
