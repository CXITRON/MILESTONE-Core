#pragma once
#include <WiFi.h>
namespace MilestoneV5 {
constexpr unsigned kWifiWireBytes = 112;
struct WifiCredentials {
  uint8_t id = 0;
};
struct WifiStore {
  bool load(WifiCredentials &, unsigned index = 0) { return index == 0; }
  bool save(const WifiCredentials &) { return true; }
};
inline bool decodeWifi(const uint8_t *p, size_t n, WifiCredentials &v) {
  if (!n)
    return false;
  v.id = *p;
  return true;
}
inline void encodeWifi(const WifiCredentials &v, uint8_t *p) {
  memset(p, 0, kWifiWireBytes);
  p[0] = v.id;
}
inline bool connectWifi(const WifiCredentials &) {
  ++WiFi.attempts;
  return true;
}
} // namespace MilestoneV5
