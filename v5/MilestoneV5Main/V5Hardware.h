#pragma once

#include "V5Environment.h"
#include "V5EnvironmentLog.h"
#include "V5Tft.h"
#include <Adafruit_NeoPixel.h>
#include <MilestoneV5BoardConfig.h>
#include <MilestoneV5Features.h>
#include <MilestoneV5Rtc.h>
#include <Preferences.h>
#include <SD.h>
#include <U8g2lib.h>
#include <Wire.h>

// The Arduino loop owns the shared TFT/SD bus. No worker may access SPI or SD.
class V5Hardware {
public:
  SimpleSt7735 display{SPI, MilestoneV5::MainPins::kTftCs,
                       MilestoneV5::MainPins::kTftDc,
                       MilestoneV5::MainPins::kTftReset};
  Adafruit_NeoPixel led{1, MilestoneV5::MainPins::kRgb, NEO_GRB + NEO_KHZ800};
  MilestoneV5::RtcTime rtc{};
  V5Environment environment;
  V5EnvironmentLog environmentLog;
  bool rtcValid = false;
  bool rtcPresent = false;
  bool sdMounted = false;
  bool storageReady = false;
  bool temperatureValid = false;
  float temperature = 0;
  uint8_t savedProfile = 0;
  String files[64];
  bool corruptFiles[64]{};
  bool monochrome = false, reverseSort = false;
  bool textScroll = true, textLeft = false;
  bool textShift = true;
  uint8_t scrollSpeed = 24;
  // Legacy status meanings: idle/wait, connecting, online, AP, download,
  // installing, fault. Bluetooth is deliberately not shown as Wi-Fi state.
  uint8_t radioIndicator = 0;
  size_t fileCount = 0;

  void begin() {
    pinMode(MilestoneV5::MainPins::kTftCs, OUTPUT);
    digitalWrite(MilestoneV5::MainPins::kTftCs, HIGH);
    pinMode(MilestoneV5::MainPins::kSdCs, OUTPUT);
    digitalWrite(MilestoneV5::MainPins::kSdCs, HIGH);
    SPI.begin(MilestoneV5::MainPins::kTftSck, MilestoneV5::MainPins::kSdMiso,
              MilestoneV5::MainPins::kTftMosi, -1);
    display.setTone(92, 8);
    display.begin();
    display.fillScreen(0);
    display.setTextWrap(false);
    Wire.begin(MilestoneV5::MainPins::kI2cSda, MilestoneV5::MainPins::kI2cScl,
               100000);
    Wire.setTimeOut(20);
    led.begin();
    led.setBrightness(16);
    storageReady = preferences.begin("milestone_v5", false);
    if (storageReady) {
      savedProfile = preferences.getUChar("profile", 0);
      if (savedProfile > 2)
        savedProfile = 0;
    }
    mountSd();
    sample();
    setenv("TZ", "KST-9", 1);
    tzset();
    if (rtcValid) {
      struct tm local{};
      local.tm_year = rtc.year - 1900;
      local.tm_mon = rtc.month - 1;
      local.tm_mday = rtc.day;
      local.tm_hour = rtc.hour;
      local.tm_min = rtc.minute;
      local.tm_sec = rtc.second;
      local.tm_isdst = -1;
      struct timeval tv{mktime(&local), 0};
      settimeofday(&tv, nullptr);
    }
  }

  bool saveProfile(uint8_t profile) {
    if (profile > 2 || !storageReady)
      return false;
    if (profile == savedProfile)
      return true;
    if (preferences.putUChar("profile", profile) != 1)
      return false;
    if (preferences.getUChar("profile", 255) != profile)
      return false;
    savedProfile = profile;
    return true;
  }

  void mountSd() {
    digitalWrite(MilestoneV5::MainPins::kTftCs, HIGH);
    sdMounted = SD.begin(MilestoneV5::MainPins::kSdCs, SPI, 10000000);
    if (!sdMounted)
      return; // Never format or erase on mount failure.
    const char *directories[] = {"/media",
                                 "/media/photo",
                                 "/media/video",
                                 "/media/sync",
                                 "/now",
                                 "/now/art-cache",
                                 "/logs",
                                 "/firmware",
                                 "/firmware/main",
                                 "/firmware/zero",
                                 "/firmware/main/stable",
                                 "/firmware/main/backup",
                                 "/firmware/main/recovery",
                                 "/firmware/zero/stable",
                                 "/firmware/zero/backup",
                                 "/firmware/zero/recovery"};
    for (const char *path : directories) {
      if (!SD.exists(path) && !SD.mkdir(path)) {
        sdWritable = false;
        return;
      }
    }
    sdWritable = true;
    environmentLog.recover();
  }

  void sample() {
    uint8_t bytes[7], status;
    rtcValid = readRegisters(0x68, 0, bytes, sizeof(bytes)) &&
               readRegisters(0x68, 0x0F, &status, 1) &&
               MilestoneV5::decodeRtc(bytes, sizeof(bytes), status, rtc);
    rtcPresent = rtcValid;
    if (!rtcValid && time(nullptr) >= 1704067200 &&
        time(nullptr) <= 4102444799LL) {
      time_t epoch = time(nullptr);
      struct tm t{};
      localtime_r(&epoch, &t);
      rtc = {uint16_t(t.tm_year + 1900), uint8_t(t.tm_mon + 1),
             uint8_t(t.tm_mday),         uint8_t(t.tm_hour),
             uint8_t(t.tm_min),          uint8_t(t.tm_sec)};
      rtcValid = true;
    }
    temperature = temperatureRead();
    temperatureValid =
        isfinite(temperature) && temperature >= -40 && temperature <= 125;
  }

  bool setRtcEpoch(uint32_t epoch) {
    if (epoch < 1704067200UL || epoch > 4102444799UL)
      return false;
    struct timeval tv{static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
    time_t local = uint64_t(epoch) + 9 * 3600;
    struct tm t{};
    gmtime_r(&local, &t);
    if (t.tm_year < 100 || t.tm_year > 199)
      return false;
    auto bcd = [](int v) -> uint8_t { return uint8_t((v / 10) * 16 + v % 10); };
    uint8_t bytes[] = {bcd(t.tm_sec),       bcd(t.tm_min),  bcd(t.tm_hour),
                       bcd(t.tm_wday + 1),  bcd(t.tm_mday), bcd(t.tm_mon + 1),
                       bcd(t.tm_year - 100)};
    uint8_t status;
    if (!readRegisters(0x68, 0x0F, &status, 1)) {
      rtcPresent = false;
      return true;
    }
    Wire.beginTransmission(0x68);
    Wire.write(0);
    Wire.write(bytes, sizeof(bytes));
    if (Wire.endTransmission() != 0) {
      rtcPresent = false;
      return true;
    }
    Wire.beginTransmission(0x68);
    Wire.write(0x0F);
    Wire.write(status & 0x7F);
    if (Wire.endTransmission() != 0) {
      rtcPresent = false;
      return true;
    }
    rtcPresent = true;
    return true;
  }

  void logEnvironment(uint32_t now) {
    if (!sdMounted || !sdWritable || !rtcValid || !environment.address ||
        !environment.values.hasValue() || environment.values.stale(now))
      return;
    if (logged && now - lastLog < environment.logIntervalMs)
      return;
    logged = true;
    lastLog = now;
    char row[128];
    const auto &v = environment.values.filtered();
    int n = snprintf(row, sizeof(row), "%02u:%02u:%02u,%.2f,%.2f\n",
                     rtc.hour, rtc.minute, rtc.second, v.temperatureC,
                     v.humidityPercent);
    if (n <= 0 || size_t(n) >= sizeof(row))
      return;
    environmentLog.append(rtc.year, rtc.month, rtc.day, row, n);
  }

  void scanPhotos(bool video = false) {
    fileCount = 0;
    memset(corruptFiles, 0, sizeof(corruptFiles));
    if (!sdMounted)
      return;
    File directory = SD.open(video ? "/media/video" : "/media/photo");
    if (!directory || !directory.isDirectory())
      return;
    // Bounded scan: do not stall the UI on an arbitrarily large directory.
    for (size_t inspected = 0; inspected < 512 && fileCount < 64; ++inspected) {
      File file = directory.openNextFile();
      if (!file)
        break;
      String name = file.name();
      String lower = name;
      lower.toLowerCase();
      if (!file.isDirectory() && lower.endsWith(video ? ".mvj" : ".bmp") &&
          name.indexOf('/') < 0 && name.indexOf('\\') < 0 &&
          name.length() <= 96) {
        files[fileCount++] = name;
      }
      file.close();
    }
    directory.close();
    for (size_t i = 1; i < fileCount; ++i) {
      String value = files[i];
      size_t j = i;
      while (j && files[j - 1].compareTo(value) > 0) {
        files[j] = files[j - 1];
        --j;
      }
      files[j] = value;
    }
    if (reverseSort)
      for (size_t i = 0; i < fileCount / 2; ++i) {
        String value = files[i];
        files[i] = files[fileCount - 1 - i];
        files[fileCount - 1 - i] = value;
      }
  }

  // Standard uncompressed 24-bit BMP, 128x128 maximum; no new media contract.
  // A row is read before drawing so TFT and SD never overlap transactions.
  bool photo(size_t index) {
    if (!sdMounted || index >= fileCount)
      return false;
    File file = SD.open(String("/media/photo/") + files[index], FILE_READ);
    if (!file)
      return false;
    uint8_t header[54];
    if (file.read(header, sizeof(header)) != sizeof(header))
      return false;
    auto u32 = [](const uint8_t *p) -> uint32_t {
      return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
             uint32_t(p[3]) << 24;
    };
    const uint32_t offset = u32(header + 10), width = u32(header + 18),
                   height = u32(header + 22);
    if (header[0] != 'B' || header[1] != 'M' || u32(header + 14) != 40 ||
        header[26] != 1 || header[27] || header[28] != 24 || header[29] ||
        u32(header + 30) || !width || width > 128 || !height || height > 128 ||
        offset < 54)
      return false;
    const uint32_t stride = (width * 3 + 3) & ~3UL;
    if (uint64_t(offset) + uint64_t(stride) * height > file.size())
      return false;
    display.fillRect(0, 16, 128, 128, 0);
    uint8_t row[384];
    for (uint32_t y = 0; y < height; ++y) {
      if (!file.seek(offset + (height - 1 - y) * stride) ||
          file.read(row, stride) != stride)
        return false;
      display.startWrite();
      for (uint32_t x = 0; x < width; ++x) {
        const uint8_t *p = row + x * 3;
        uint16_t color = (uint16_t(p[2] & 0xF8) << 8) |
                         (uint16_t(p[1] & 0xFC) << 3) | (p[0] >> 3);
        if (monochrome) {
          unsigned y = (p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8;
          color = ((y & 248) << 8) | ((y & 252) << 3) | (y >> 3);
        }
        display.writePixel((128 - width) / 2 + x, 16 + (128 - height) / 2 + y,
                           color);
      }
      display.endWrite();
      yield();
    }
    return true;
  }

  void statusBands(const char *profile, bool zeroOnline,
                   int16_t zeroTemperature) {
    display.fillRect(0, 0, 128, 16, 0);
    display.setTextSize(1);
    display.setTextColor(0x07FF, 0);
    display.setCursor(2, 4);
    display.print(profile[0]);
    if (environment.enabled && environment.values.hasValue() &&
        !environment.values.stale(millis()) && environment.address) {
      const auto &value = environment.values.filtered();
      if (environment.displayMask & 1) {
        display.setTextColor(environment.color(0, value.temperatureC));
        display.printf(environment.useFahrenheit ? " %.0fF" : " %.0fC",
                       environment.useFahrenheit
                           ? value.temperatureC * 1.8f + 32
                           : value.temperatureC);
      }
      if (value.hasHumidity && (environment.displayMask & 2)) {
        display.setTextColor(environment.color(1, value.humidityPercent));
        display.printf(" %.0f%%", value.humidityPercent);
      }
    } else if (environment.enabled && environment.displayMask)
      display.print(" ENV --");
    display.fillRect(116, 0, 12, 14, 0);
    if (radioIndicator == 0) {
      display.drawCircle(122, 6, 4, 0x8410);
    } else if (radioIndicator == 1) {
      // Same clock-shaped waiting/synchronizing indicator as legacy CORE.
      display.drawCircle(122, 6, 4, 0x3D7F);
      display.drawFastVLine(122, 3, 3, 0x3D7F);
      display.drawFastHLine(122, 6, 3, 0x3D7F);
    } else if (radioIndicator == 2) {
      // Same online check mark as legacy CORE.
      display.drawCircle(122, 6, 4, 0x2F2D);
      display.drawLine(119, 6, 121, 8, 0x2F2D);
      display.drawLine(121, 8, 125, 3, 0x2F2D);
    } else if (radioIndicator == 3) {
      display.setTextColor(0x07FF, 0);
      display.setCursor(116, 4);
      display.print("AP");
    } else if (radioIndicator == 4 || radioIndicator == 5) {
      const uint16_t color = radioIndicator == 4 ? 0xFD20 : 0xF81F;
      display.drawFastVLine(122, 1, 8, color);
      display.drawLine(122, 1, 119, 4, color);
      display.drawLine(122, 1, 125, 4, color);
    } else if (radioIndicator == 6) {
      display.drawLine(118, 2, 126, 10, 0xF800);
      display.drawLine(126, 2, 118, 10, 0xF800);
    }
    display.drawFastHLine(0, 15, 128, 0x4208);
    display.fillRect(0, 144, 128, 16, 0);
    display.drawFastHLine(0, 144, 128, 0x4208);
    display.setCursor(2, 150);
    display.setTextColor(!temperatureValid || temperature >= 90 ? 0xF800
                         : temperature >= 70                    ? 0xFD20
                                                                : 0xFFFF);
    if (temperatureValid)
      display.printf("M %.0f", temperature);
    else
      display.print("M --");
    display.setTextColor(0xFFFF);
    display.print(" | Z ");
    display.setTextColor(zeroTemperature >= 9500   ? 0xF800
                         : zeroTemperature >= 7500 ? 0xFD20
                                                   : 0xFFFF);
    if (zeroOnline && zeroTemperature != INT16_MIN)
      display.printf("%.0f", zeroTemperature / 100.0f);
    else
      display.print("--");
  }

  void body(const char *title, const String &line1, const String &line2 = "",
            const String &line3 = "") {
    display.fillRect(0, 16, 128, 128, 0);
    textLine(17, title, 0xFFFF);
    display.drawFastHLine(4, 35, 120, 0x7BEF);
    textLine(48, line1);
    textLine(72, line2);
    textLine(96, line3);
  }

  void localLed(bool safe, bool ap = false, bool internet = false) {
    led.setPixelColor(0, safe        ? led.Color(24, 0, 0)
                         : ap        ? led.Color(0, 16, 24)
                         : internet  ? led.Color(0, 0, 24)
                         : sdMounted ? led.Color(0, 8, 0)
                                     : led.Color(8, 4, 0));
    led.show();
  }

  void coloredLine(int y, const String &text, uint16_t color) {
    textLine(y, text, color);
  }

  void legacyClear() { display.fillRect(0, 16, 128, 128, 0); }

  void legacyText(const String &text, int baseline, const uint8_t *font,
                  uint16_t color = 0xFFFF, int x = -1) {
    glyphs.clearBuffer();
    glyphs.setFont(font);
    if (x < 0)
      x = MilestoneV5::centeredTextX(
          128, int(glyphs.getUTF8Width(text.c_str())));
    glyphs.drawUTF8(x, baseline, text.c_str());
    display.blitMono(glyphs.getBufferPtr(), 16, color);
  }

  void legacyAutoText(const String &text, int baseline,
                      uint16_t color = 0xFFFF, bool doubleWidth = false) {
    bool korean = false;
    const uint8_t *s = reinterpret_cast<const uint8_t *>(text.c_str());
    for (size_t i = 0; i + 2 < text.length(); ++i) {
      if ((s[i] & 0xF0) != 0xE0 || (s[i + 1] & 0xC0) != 0x80 ||
          (s[i + 2] & 0xC0) != 0x80)
        continue;
      uint32_t c = (s[i] & 15) * 4096 + (s[i + 1] & 63) * 64 +
                   (s[i + 2] & 63);
      if (c >= 0xAC00 && c <= 0xD7AF) {
        korean = true;
        break;
      }
    }
    glyphs.clearBuffer();
    glyphs.setFont(korean ? u8g2_font_unifont_t_korean2
                          : u8g2_font_unifont_t_japanese2);
    int width = int(glyphs.getUTF8Width(text.c_str())) *
                (doubleWidth ? 2 : 1);
    int x = MilestoneV5::centeredTextX(128, width);
    if (doubleWidth)
      glyphs.drawUTF8X2(x, baseline, text.c_str());
    else
      glyphs.drawUTF8(x, baseline, text.c_str());
    display.blitMono(glyphs.getBufferPtr(), 16, color);
  }

  void legacyRule(int y, uint16_t color = 0x7BEF) {
    glyphs.clearBuffer();
    glyphs.drawHLine(4, y, 120);
    display.blitMono(glyphs.getBufferPtr(), 16, color);
  }

  void legacyFrame(int x, int y, int width, int height,
                   uint16_t color = 0xFFFF) {
    glyphs.clearBuffer();
    glyphs.drawFrame(x, y, width, height);
    display.blitMono(glyphs.getBufferPtr(), 16, color);
  }

private:
  // Software-only glyph buffer. Never begin()/sendBuffer(): TFT owns display
  // I/O.
  U8G2_SH1107_128X128_F_SW_I2C glyphs{U8G2_R0, U8X8_PIN_NONE, U8X8_PIN_NONE,
                                      U8X8_PIN_NONE};
  void textLine(int y, const String &text, uint16_t color = 0xFFFF) {
    bool korean = false;
    const uint8_t *s = reinterpret_cast<const uint8_t *>(text.c_str());
    for (size_t i = 0; i + 2 < text.length(); ++i) {
      if ((s[i] & 0xF0) == 0xE0 && (s[i + 1] & 0xC0) == 0x80 &&
          (s[i + 2] & 0xC0) == 0x80) {
        uint32_t c =
            (s[i] & 15) * 4096 + (s[i + 1] & 63) * 64 + (s[i + 2] & 63);
        if (c >= 0xAC00 && c <= 0xD7AF)
          korean = true;
      }
    }
    glyphs.clearBuffer();
    glyphs.setFont(korean ? u8g2_font_unifont_t_korean2
                          : u8g2_font_unifont_t_japanese1);
    int width = glyphs.getUTF8Width(text.c_str()),
        x = textLeft ? 0 : max(0, (128 - width) / 2);
    unsigned slot = constrain(y / 16, 0, 9);
    uint32_t hash = MilestoneV5::crc32(
        reinterpret_cast<const uint8_t *>(text.c_str()), text.length());
    if (lineHash[slot] != hash) {
      lineHash[slot] = hash;
      lineStarted[slot] = millis();
    }
    if (textScroll && width > 128) {
      uint32_t elapsed = millis() - lineStarted[slot];
      x = elapsed < 1200
              ? 0
              : -int(((elapsed - 1200) * scrollSpeed / 1000) % (width + 32));
    }
    if (textShift && width < 126)
      x += int((millis() / 60000) % 3) - 1;
    glyphs.drawUTF8(x, 14, text.c_str());
    if (textScroll && width > 128 && x + width < 128)
      glyphs.drawUTF8(x + width + 32, 14, text.c_str());
    const uint8_t *bits = glyphs.getBufferPtr();
    display.startWrite();
    for (int yy = 0; yy < 16; ++yy)
      for (int x = 0; x < 128; ++x)
        if (bits[(yy / 8) * 128 + x] & (1U << (yy & 7)))
          display.writePixel(x, y + yy, color);
    display.endWrite();
  }
  Preferences preferences;
  uint32_t lineHash[10]{}, lineStarted[10]{};
  bool sdWritable = false;
  bool logged = false;
  uint32_t lastLog = 0;
  bool readRegisters(uint8_t address, uint8_t reg, uint8_t *data,
                     size_t length) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(address, uint8_t(length)) != length)
      return false;
    for (size_t i = 0; i < length; ++i)
      data[i] = Wire.read();
    return true;
  }
};
#define MILESTONE_V5_HARDWARE_DECLARED 1
