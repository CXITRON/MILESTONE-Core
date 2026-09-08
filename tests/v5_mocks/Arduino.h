#pragma once
#include <SD.h>
#include <esp_ota_ops.h>

struct FakeSerialPort {
  void println(const String &) {}
};
static FakeSerialPort Serial __attribute__((unused));
static FakeSerialPort Serial0 __attribute__((unused));
