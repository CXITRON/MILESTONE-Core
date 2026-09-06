#pragma once
#include <Arduino.h>
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Video.h>
#include <Preferences.h>
namespace MilestoneV5 {
class SystemSettings {
public:
  uint8_t luminance = 92;
  int8_t contrast = 8;
  bool monochrome = false, fixedAp = false, ledEnabled = true,
       wifiSleep = false, bootSync = true;
  String apPassword;
  uint8_t ledDay = 24, ledNight = 6;
  uint16_t nightStart = 1320, nightEnd = 420;
  uint32_t ntpSeconds = 21600, retrySeconds = 300;
  bool loaded = false;
  void begin() {
    Preferences p;
    uint8_t b[96];
    bool ok = p.begin("v5_system", true) &&
              p.getBytesLength("record") == sizeof(b) &&
              p.getBytes("record", b, sizeof(b)) == sizeof(b);
    p.end();
    if (ok)
      apply(b);
  }
  bool apply(const uint8_t *b) {
    if (crc32(b, 92) != readVideoU32(b + 92) || b[0] < 50 || b[0] > 100 ||
        b[1] > 40 || b[2] > 1 || b[3] > 1 || b[68] || b[69] > 1 || b[84] > 1 ||
        b[85] > 1)
      return false;
    String password = reinterpret_cast<const char *>(b + 4);
    uint16_t a = uint16_t(b[72]) | uint16_t(b[73]) << 8,
             z = uint16_t(b[74]) | uint16_t(b[75]) << 8;
    uint32_t ntp = readVideoU32(b + 76), retry = readVideoU32(b + 80);
    if ((password.length() && password.length() < 8) ||
        password.length() > 63 || a >= 1440 || z >= 1440 || ntp > 604800 ||
        retry < 15 || retry > 86400)
      return false;
    luminance = b[0];
    contrast = int(b[1]) - 20;
    monochrome = b[2];
    fixedAp = b[3];
    apPassword = password;
    ledEnabled = b[69];
    ledDay = b[70];
    ledNight = b[71];
    nightStart = a;
    nightEnd = z;
    ntpSeconds = ntp;
    retrySeconds = retry;
    wifiSleep = b[84];
    bootSync = b[85];
    loaded = true;
    return true;
  }
  void encode(uint8_t *b) const {
    memset(b, 0, 96);
    b[0] = luminance;
    b[1] = contrast + 20;
    b[2] = monochrome;
    b[3] = fixedAp;
    apPassword.toCharArray(reinterpret_cast<char *>(b + 4), 65);
    b[69] = ledEnabled;
    b[70] = ledDay;
    b[71] = ledNight;
    b[72] = nightStart;
    b[73] = nightStart >> 8;
    b[74] = nightEnd;
    b[75] = nightEnd >> 8;
    put(b + 76, ntpSeconds);
    put(b + 80, retrySeconds);
    b[84] = wifiSleep;
    b[85] = bootSync;
    put(b + 92, crc32(b, 92));
  }
  bool save() {
    uint8_t b[96];
    encode(b);
    SystemSettings check;
    if (!check.apply(b))
      return false;
    Preferences p;
    uint8_t readback[96];
    bool ok =
        p.begin("v5_system", false) &&
        p.putBytes("record", b, sizeof(b)) == sizeof(b) &&
        p.getBytes("record", readback, sizeof(readback)) == sizeof(readback) &&
        !memcmp(b, readback, sizeof(b));
    p.end();
    if (ok)
      loaded = true;
    return ok;
  }
  bool importLegacy(const uint8_t *snapshot) {
    if (crc32(snapshot, 348) != readVideoU32(snapshot + 348))
      return false;
    uint8_t b[96];
    memcpy(b, snapshot + 256, 96);
    put(b + 92, crc32(b, 92));
    SystemSettings next;
    if (!next.apply(b) || !next.save())
      return false;
    *this = next;
    return true;
  }

private:
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (8 * i);
  }
};
} // namespace MilestoneV5
