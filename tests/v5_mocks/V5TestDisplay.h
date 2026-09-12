#pragma once
#include <Arduino.h>
#define MILESTONE_V5_TFT_DECLARED 1
inline uint32_t micros() { return millis() * 1000U; }
struct SimpleSt7735 {
  unsigned frames = 0;
  void rgb565(const uint16_t *, int, int) {}
  void flushRegion(int, int) { ++frames; }
  void startWrite() {}
  void endWrite() {}
  void writePixel(int, int, uint16_t) {}
};
