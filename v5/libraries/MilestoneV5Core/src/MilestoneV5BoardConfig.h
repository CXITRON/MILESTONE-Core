#pragma once

#include <stdint.h>

namespace MilestoneV5 {

namespace MainPins {
constexpr int kButtonPrev = 4;
constexpr int kButtonBack = 5;
constexpr int kTftReset = 6;
constexpr int kButtonNext = 7;
constexpr int kLinkSck = 8;
constexpr int kLinkMosi = 9;
constexpr int kSdCs = 10;
constexpr int kTftMosi = 11;
constexpr int kTftSck = 12;
constexpr int kSdMiso = 13;
constexpr int kButtonMode = 14;
constexpr int kButtonOk = 15;
constexpr int kLinkMiso = 16;
constexpr int kLinkCs = 17;
constexpr int kLinkReady = 18;
constexpr int kTftDc = 21;
constexpr int kI2cSda = 41;
constexpr int kI2cScl = 42;
constexpr int kTftCs = 47;
constexpr int kRgb = 48;
} // namespace MainPins

namespace ZeroPins {
constexpr int kLinkReady = 6;
constexpr int kLinkCs = 7;
constexpr int kLinkMosi = 8;
constexpr int kLinkSck = 9;
constexpr int kLinkMiso = 10;
constexpr int kRgb = 21;
} // namespace ZeroPins

constexpr uint32_t kInitialLinkClockHz = 1000000UL;
constexpr uint32_t kHeartbeatIntervalMs = 1000UL;
constexpr uint32_t kLinkStaleMs = 5000UL;

} // namespace MilestoneV5
