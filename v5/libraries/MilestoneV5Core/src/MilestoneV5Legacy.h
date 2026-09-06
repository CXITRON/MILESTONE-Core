#pragma once
#include <MilestoneV5Calendar.h>
#include <MilestoneV5Now.h>
#include <MilestoneV5WifiStore.h>
#include <Preferences.h>
namespace MilestoneV5 {
constexpr size_t kLegacySnapshotBytes = 352;
inline void legacyPut(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = v >> (i * 8);
}
inline bool legacySnapshot(uint8_t *b) {
  Preferences p;
  if (!p.begin("milestone", true))
    return false;
  uint16_t version = p.getUShort("cfg_ver", 0);
  if (!version || version > 12) {
    p.end();
    return false;
  }
  memset(b, 0, kLegacySnapshotBytes);
  memcpy(b, "VC02", 4);
  uint8_t mode = p.getUChar("mode", 0), view = mode == 6
                                                   ? p.getUChar("last_view", 0)
                                               : mode == 7 ? 6
                                                           : mode;
  b[4] = view <= 6 ? view : 0;
  unsigned y = 2027, m = 1, d = 1;
  int used = 0;
  String target = p.getString("target", "");
  bool date = sscanf(target.c_str(), "%u-%u-%u%n", &y, &m, &d, &used) == 3 &&
              used == int(target.length()) && validDate(y, m, d);
  if (!date) {
    y = 2027;
    m = 1;
    d = 1;
  }
  b[6] = y;
  b[7] = y >> 8;
  b[8] = m;
  b[9] = d;
  b[228] = date;
  copyNowText(reinterpret_cast<char *>(b + 10), 145,
              p.getString("message", "Make every day count").c_str());
  copyNowText(reinterpret_cast<char *>(b + 155), 65,
              p.getString("title", "MILESTONE").c_str());
  const char *keys[] = {"col_time", "col_date",  "col_msg",
                        "col_dday", "col_title", "col_info"};
  const uint32_t defaults[] = {0xFFFFFF, 0x7FDBFF, 0xFFD166,
                               0xFF5D8F, 0x36D9FF, 0xB8C4D0};
  for (unsigned i = 0; i < 6; ++i) {
    uint32_t c = p.getUInt(keys[i], defaults[i]);
    uint16_t rgb = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 31);
    unsigned at = i < 4 ? 220 + i * 2 : 240 + (i - 4) * 2;
    b[at] = rgb;
    b[at + 1] = rgb >> 8;
  }
  b[229] = (p.getBool("hour24", true) ? 1 : 0) |
           (p.getBool("seconds", false) ? 2 : 0) |
           (p.getBool("msg_scroll", true) ? 4 : 0) |
           (p.getBool("msg_left", false) ? 8 : 0) |
           (p.getBool("dday_text", false) ? 16 : 0) |
           (p.getBool("after_done", false) ? 32 : 0) | (mode == 6 ? 64 : 0) |
           (p.getBool("burnin", true) ? 128 : 0);
  b[230] = constrain(p.getUChar("scroll_spd", 24), 5, 80);
  b[231] = p.getUChar("cycle_mask", 127) & 127;
  if (!b[231])
    b[231] = 127;
  b[232] = constrain(p.getUChar("cycle_int", 8), 3, 60);
  for (unsigned i = 0; i < 7; ++i)
    b[233 + i] = i;
  String order = p.getString("cycle_ord", "");
  uint8_t parsed[7], count = 0, mask = 0;
  bool orderValid = true;
  for (unsigned i = 0; i < order.length(); ++i) {
    char c = order[i];
    if (c == ',' || c == ' ')
      continue;
    if (c < '0' || c > '7') {
      orderValid = false;
      break;
    }
    if (c == '7')
      continue;
    unsigned v = c - '0';
    if (mask & (1U << v)) {
      orderValid = false;
      break;
    }
    if (count < 7)
      parsed[count++] = v;
    mask |= 1U << v;
  }
  if (orderValid && count == 7)
    memcpy(b + 233, parsed, 7);
  b[245] = min(p.getUChar("now_layout", 1), uint8_t(3));
  uint16_t off = min(p.getUShort("screen_off", 0), uint16_t(1440));
  b[246] = off;
  b[247] = off >> 8;
  legacyPut(b + 252, crc32(b, 252));
  b[256] = constrain(p.getUChar("tone_lum", 92), 50, 100);
  b[257] = constrain(p.getChar("tone_ctr", 8), -20, 20) + 20;
  b[258] = p.getBool("media_mono", false);
  b[259] = p.getBool("ap_fixed", false);
  String password = p.getString("ap_pass", "");
  if (password.length() && password.length() < 8)
    b[259] = 0;
  password.toCharArray(reinterpret_cast<char *>(b + 260), 65);
  b[325] = p.getBool("led_en", true);
  b[326] = p.getUChar("led_lvl", 24);
  b[327] = p.getUChar("led_night", 6);
  uint16_t start = p.getUShort("night_start", 1320),
           end = p.getUShort("night_end", 420);
  b[328] = start;
  b[329] = start >> 8;
  b[330] = end;
  b[331] = end >> 8;
  legacyPut(b + 332, p.getUInt("ntp_sec", 21600));
  legacyPut(b + 336, p.getUInt("retry_sec", 300));
  b[340] = p.getBool("wifi_sleep", false);
  b[341] = p.getBool("boot_sync", true);
  legacyPut(b + 348, crc32(b, 348));
  p.end();
  return true;
}
inline void importLegacyNetworks() {
  WifiStore store;
  WifiCredentials list[kWifiMaxNetworks];
  uint8_t present;
  if (store.loadAll(list, present))
    return;
  Preferences p;
  if (!p.begin("milestone", true))
    return;
  unsigned version = p.getUShort("cfg_ver", 0);
  if (!version || version > 12) {
    p.end();
    return;
  }
  bool bank = p.getUChar("wifi_bank", 0) == 1;
  uint8_t count = min(p.getUChar(bank ? "wfb_count" : "wifi_count", 0),
                      uint8_t(kWifiMaxNetworks));
  if (!count && p.isKey("wifi_ssid")) {
    WifiCredentials v;
    p.getString("wifi_ssid", "").toCharArray(v.ssid, 33);
    p.getString("wifi_pass", "").toCharArray(v.password, 65);
    if (validWifiCredentials(v))
      store.save(v);
  }
  for (int i = count - 1; i >= 0; --i) {
    WifiCredentials v;
    String prefix = bank ? "wfb_" : "wifi_", suffix = String(i);
    p.getString((prefix + "ssid" + suffix).c_str(), "").toCharArray(v.ssid, 33);
    p.getString((prefix + "pass" + suffix).c_str(), "")
        .toCharArray(v.password, 65);
    v.security = p.getUChar((prefix + "sec" + suffix).c_str(), 0);
    if (v.security) {
      p.getString((prefix + "user" + suffix).c_str(), "")
          .toCharArray(v.username, 65);
      p.getString((prefix + "id" + suffix).c_str(), "")
          .toCharArray(v.identity, 65);
    }
    if (validWifiCredentials(v) && !store.save(v))
      break;
  }
  p.end();
}
} // namespace MilestoneV5
