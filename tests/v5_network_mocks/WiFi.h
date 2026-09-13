#pragma once
#include "../v5_mocks/Arduino.h"
#include <ctime>
constexpr int WL_CONNECTED = 3, WIFI_OFF = 0, WIFI_STA = 1;
struct FakeNetworkWifi {
  bool connected = false;
  unsigned attempts = 0, disconnects = 0, modeChanges = 0;
  int status() const { return connected ? WL_CONNECTED : 0; }
  uint32_t localIP() const { return connected ? 1 : 0; }
  void disconnect(bool, bool) {
    connected = false;
    ++disconnects;
  }
  void mode(int) { ++modeChanges; }
  void setSleep(bool) {}
};
static FakeNetworkWifi WiFi;
