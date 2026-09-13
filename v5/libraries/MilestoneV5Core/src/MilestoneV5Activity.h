#pragma once
#include <stdint.h>
namespace MilestoneV5 {
enum class Activity : uint8_t {
  Idle = 0,
  Ntp = 1,
  Online = 2,
  Ap = 3,
  Check = 4,
  Install = 5,
  Fault = 6,
  Connecting = 7,
  Artwork = 8,
  Download = 9,
  Storage = 10,
  Advertising = 11,
  Ble = 12,
  Safe = 13
};
inline const char *activityName(Activity s) {
  const char *names[] = {"IDLE",      "NTP SYNC",    "ONLINE",  "SETUP AP",
                         "OTA CHECK", "OTA INSTALL", "FAULT",   "WIFI CONNECT",
                         "ARTWORK",   "DOWNLOAD",    "SD BUSY", "BLE ADV",
                         "BLE AMS",   "SAFE"};
  return uint8_t(s) < 14 ? names[uint8_t(s)] : "UNKNOWN";
}
inline uint32_t activityColor(Activity s) {
  const uint32_t colors[] = {0x206020, 0xffffff, 0x20ff40, 0x00dfff, 0xffa000,
                             0xd020ff, 0xff0000, 0xffc020, 0x2080ff, 0xff7000,
                             0xffdf00, 0x2040ff, 0x2080ff, 0xff3020};
  return uint8_t(s) < 14 ? colors[uint8_t(s)] : 0xff0000;
}
inline uint32_t activityLed(Activity s, uint32_t now) {
  // Full-scale colors are attenuated once by the configured day/night
  // brightness.
  const uint32_t period = s == Activity::Fault  ? 200
                          : s == Activity::Idle ? 2000
                                                : 800;
  const bool steady = s == Activity::Online || s == Activity::Ble ||
                      s == Activity::Ap || s == Activity::Safe;
  return steady || now % period < period / 2 ? activityColor(s) : 0;
}
inline uint16_t activity565(Activity s) {
  const uint32_t c = activityColor(s);
  return ((c >> 8) & 0xf800) | ((c >> 5) & 0x7e0) | ((c >> 3) & 31);
}
// Twelve rows, twelve columns. Shapes distinguish equal-color activities.
inline const uint16_t *activityIcon(Activity s) {
  static const uint16_t icons[14][12] = {
      {0, 0, 0x0f0, 0x108, 0x204, 0x204, 0x204, 0x108, 0x0f0, 0, 0, 0},
      {0, 0x1f8, 0x204, 0x442, 0x442, 0x478, 0x402, 0x204, 0x1f8, 0, 0, 0},
      {0, 0, 0x004, 0x00c, 0x018, 0x230, 0x360, 0x1c0, 0x080, 0, 0, 0},
      {0, 0, 0x73c, 0x8a2, 0x8a2, 0xfbc, 0x8a0, 0x8a0, 0x8a0, 0, 0, 0},
      {0, 0x3c0, 0x420, 0x810, 0x810, 0x420, 0x3e0, 0x030, 0x018, 0x00c, 0, 0},
      {0, 0x060, 0x0f0, 0x1f8, 0x060, 0x060, 0x060, 0x060, 0x3fc, 0, 0, 0},
      {0, 0x204, 0x108, 0x090, 0x060, 0x060, 0x090, 0x108, 0x204, 0, 0, 0},
      {0, 0x3fc, 0x402, 0, 0x0f0, 0x108, 0, 0x060, 0x060, 0, 0, 0},
      {0, 0x7fe, 0x402, 0x4c2, 0x4c2, 0x422, 0x452, 0x58a, 0x706, 0x7fe, 0, 0},
      {0, 0x060, 0x060, 0x060, 0x060, 0x1f8, 0x0f0, 0x060, 0x402, 0x7fe, 0, 0},
      {0, 0x3fc, 0x624, 0x424, 0x424, 0x402, 0x47a, 0x44a, 0x47a, 0x7fe, 0, 0},
      {0, 0x060, 0x250, 0x448, 0x250, 0x060, 0x250, 0x448, 0x250, 0x060, 0, 0},
      {0, 0x060, 0x050, 0x248, 0x150, 0x060, 0x150, 0x248, 0x050, 0x060, 0, 0},
      {0, 0x1f8, 0x606, 0x402, 0x462, 0x462, 0x204, 0x204, 0x108, 0x0f0, 0x060, 0}};
  return icons[uint8_t(s) < 14 ? uint8_t(s) : 6];
}
} // namespace MilestoneV5
