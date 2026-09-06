#pragma once
#ifdef ARDUINO_ARCH_ESP32
#include <MilestoneV5Protocol.h>
#include <Preferences.h>
#include <WiFi.h>
#include <memory>
#include <new>
#include <string.h>
namespace MilestoneV5 {
constexpr size_t kWifiWireBytes = 229, kWifiMaxNetworks = 8,
                 kWifiBankBytes = 12 + kWifiMaxNetworks * kWifiWireBytes + 4;
struct WifiCredentials {
  char ssid[33]{}, password[65]{}, username[65]{}, identity[65]{};
  uint8_t security = 0;
};
inline bool validWifiCredentials(const WifiCredentials &v) {
  size_t s = strnlen(v.ssid, 33), p = strnlen(v.password, 65),
         u = strnlen(v.username, 65), i = strnlen(v.identity, 65);
  if (!s || s > 32 || p > 63 || u > 64 || i > 64 || v.security > 1)
    return false;
  for (size_t n = 0; n < s; ++n)
    if (uint8_t(v.ssid[n]) < 32)
      return false;
  if (v.security == 1)
    return u && p;
  return (!p || p >= 8) && !u && !i;
}
inline void encodeWifi(const WifiCredentials &v, uint8_t *p) {
  memcpy(p, v.ssid, 33);
  memcpy(p + 33, v.password, 65);
  memcpy(p + 98, v.username, 65);
  memcpy(p + 163, v.identity, 65);
  p[228] = v.security;
}
inline bool decodeWifi(const uint8_t *p, size_t n, WifiCredentials &v) {
  if (n != 98 && n != kWifiWireBytes)
    return false;
  v = {};
  memcpy(v.ssid, p, 33);
  memcpy(v.password, p + 33, 65);
  if (n == kWifiWireBytes) {
    memcpy(v.username, p + 98, 65);
    memcpy(v.identity, p + 163, 65);
    v.security = p[228];
  }
  return validWifiCredentials(v);
}
inline bool connectWifi(const WifiCredentials &v) {
  if (!validWifiCredentials(v))
    return false;
  if (v.security == 0) {
    WiFi.begin(v.ssid, v.password);
    return true;
  }
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
  WiFi.begin(v.ssid, WPA2_AUTH_PEAP, v.identity[0] ? v.identity : v.username,
             v.username, v.password);
  return true;
#else
  return false;
#endif
}
class WifiStore {
public:
  bool loadAll(WifiCredentials *out, uint8_t &count) {
    count = 0;
    Preferences p;
    if (!p.begin("v5_wifi", true))
      return false;
    std::unique_ptr<uint8_t[]> storage(new (std::nothrow)
                                           uint8_t[kWifiBankBytes * 2]{});
    if (!storage) {
      p.end();
      return false;
    }
    uint8_t *a = storage.get(), *b = a + kWifiBankBytes;
    bool av = read(p, "a", a), bv = read(p, "b", b);
    p.end();
    if (!av && !bv)
      return false;
    const uint8_t *r =
        (!av || (bv && int32_t(get(b + 4) - get(a + 4)) > 0)) ? b : a;
    activeB = r == b;
    generation = get(r + 4);
    count = r[8];
    for (unsigned i = 0; i < count; ++i)
      decodeWifi(r + 12 + i * kWifiWireBytes, kWifiWireBytes, out[i]);
    return true;
  }
  bool load(WifiCredentials &v, unsigned index = 0) {
    WifiCredentials list[kWifiMaxNetworks];
    uint8_t count;
    if (!loadAll(list, count) || index >= count)
      return false;
    v = list[index];
    return true;
  }
  bool save(const WifiCredentials &v) {
    if (!validWifiCredentials(v))
      return false;
    std::unique_ptr<WifiCredentials[]> storage(
        new (std::nothrow) WifiCredentials[kWifiMaxNetworks * 2]);
    if (!storage)
      return false;
    WifiCredentials *list = storage.get(), *next = list + kWifiMaxNetworks;
    uint8_t count = 0, n = 1;
    loadAll(list, count);
    next[0] = v;
    for (unsigned i = 0; i < count && n < kWifiMaxNetworks; ++i)
      if (strcmp(list[i].ssid, v.ssid))
        next[n++] = list[i];
    return write(next, n);
  }
  bool remove(const char *ssid) {
    WifiCredentials list[kWifiMaxNetworks];
    uint8_t count = 0;
    if (!loadAll(list, count))
      return false;
    uint8_t n = 0;
    for (unsigned i = 0; i < count; ++i)
      if (strcmp(list[i].ssid, ssid))
        list[n++] = list[i];
    return write(list, n);
  }

private:
  bool activeB = false;
  uint32_t generation = 0;
  static uint32_t get(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
  }
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (i * 8);
  }
  static bool read(Preferences &p, const char *key, uint8_t *r) {
    size_t n = p.getBytesLength(key);
    if (n == 110) {
      uint8_t old[110];
      WifiCredentials v;
      if (p.getBytes(key, old, 110) != 110 || memcmp(old, "VW01", 4) ||
          get(old + 106) != crc32(old, 106) || !decodeWifi(old + 8, 98, v))
        return false;
      memcpy(r, "VW02", 4);
      memcpy(r + 4, old + 4, 4);
      r[8] = 1;
      encodeWifi(v, r + 12);
      put(r + kWifiBankBytes - 4, crc32(r, kWifiBankBytes - 4));
      return true;
    }
    if (n != kWifiBankBytes || p.getBytes(key, r, n) != n ||
        memcmp(r, "VW02", 4) || get(r + n - 4) != crc32(r, n - 4) ||
        r[8] > kWifiMaxNetworks)
      return false;
    WifiCredentials v;
    for (unsigned i = 0; i < r[8]; ++i)
      if (!decodeWifi(r + 12 + i * kWifiWireBytes, kWifiWireBytes, v))
        return false;
    return true;
  }
  bool write(const WifiCredentials *list, uint8_t count) {
    std::unique_ptr<uint8_t[]> storage(new (std::nothrow)
                                           uint8_t[kWifiBankBytes * 2]{});
    if (!storage)
      return false;
    uint8_t *r = storage.get(), *check = r + kWifiBankBytes;
    memcpy(r, "VW02", 4);
    put(r + 4, generation + 1);
    r[8] = count;
    for (unsigned i = 0; i < count; ++i)
      encodeWifi(list[i], r + 12 + i * kWifiWireBytes);
    put(r + kWifiBankBytes - 4, crc32(r, kWifiBankBytes - 4));
    Preferences p;
    if (!p.begin("v5_wifi", false))
      return false;
    const char *key = activeB ? "a" : "b";
    bool ok = p.putBytes(key, r, kWifiBankBytes) == kWifiBankBytes &&
              p.getBytes(key, check, kWifiBankBytes) == kWifiBankBytes &&
              !memcmp(r, check, kWifiBankBytes);
    p.end();
    if (ok) {
      activeB = !activeB;
      ++generation;
    }
    return ok;
  }
};
} // namespace MilestoneV5
#endif
