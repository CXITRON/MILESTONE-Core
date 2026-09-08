#pragma once
#include "V5Hardware.h"
#include <MilestoneV5Calendar.h>
#include <MilestoneV5Now.h>
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Runtime.h>
#include <MilestoneV5ImageSize.h>
#include <MilestoneV5Version.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

class V5CoreViews {
public:
  static constexpr uint8_t kInfoPageCount = 9;
  uint8_t view = 0, infoPage = 0;
  uint16_t year = 2027;
  uint8_t month = 1, day = 1;
  String message = "매일을 소중하게", label = "MILESTONE";
  uint16_t colors[6] = {0x07FF, 0xFFFF, 0xFFE0, 0x07E0, 0x07FF, 0xBDF7};
  bool configured = false, hour24 = true, seconds = false, scroll = true,
       left = false, ddayText = false, afterComplete = false, cycle = false,
       burnin = true;
  uint8_t speed = 24, cycleMask = 0x7F, cycleSeconds = 8,
          order[7] = {0, 1, 2, 3, 4, 5, 6}, nowLayout = 1;
  uint16_t screenOffMinutes = 0;
  bool dirty = false;
  bool dateSet = false;
  void begin() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    otaSlotBytes = next ? next->size : 0;
    appImageBytes = running ? MilestoneV5::displayImageSize(
        running->size, [running](uint32_t offset, uint8_t *out, size_t size) {
          return esp_partition_read(running, offset, out, size) == ESP_OK;
        }) : 0;
    Preferences p;
    if (!p.begin("v5_core", true))
      return;
    uint8_t data[256];
    bool ok = p.getBytesLength("record") == sizeof(data) &&
              p.getBytes("record", data, sizeof(data)) == sizeof(data);
    p.end();
    if (ok)
      applyRecord(data);
  }
  bool applyRecord(const uint8_t *data) {
    bool modern = !memcmp(data, "VC02", 4);
    if ((!modern && memcmp(data, "VC01", 4)) ||
        MilestoneV5::crc32(data, 252) != read32(data + 252))
      return false;
    if (data[4] > 6 || data[5] >= kInfoPageCount ||
        !MilestoneV5::validDate(unsigned(data[6]) | unsigned(data[7]) << 8,
                                data[8], data[9]) ||
        data[154] || data[219])
      return false;
    if (modern) {
      unsigned mask = 0;
      for (unsigned i = 0; i < 7; ++i) {
        if (data[233 + i] > 6 || (mask & (1U << data[233 + i])))
          return false;
        mask |= 1U << data[233 + i];
      }
      if (data[230] < 5 || data[230] > 80 || !data[231] || data[231] > 127 ||
          data[232] < 3 || data[232] > 60 || data[245] > 3)
        return false;
      uint8_t f = data[229];
      hour24 = f & 1;
      seconds = f & 2;
      scroll = f & 4;
      left = f & 8;
      ddayText = f & 16;
      afterComplete = f & 32;
      cycle = f & 64;
      burnin = f & 128;
      speed = data[230];
      cycleMask = data[231];
      cycleSeconds = data[232];
      memcpy(order, data + 233, 7);
      nowLayout = data[245];
      screenOffMinutes = uint16_t(data[246]) | uint16_t(data[247]) << 8;
      colors[4] = uint16_t(data[240]) | uint16_t(data[241]) << 8;
      colors[5] = uint16_t(data[242]) | uint16_t(data[243]) << 8;
    }
    view = data[4];
    ensureEnabledView();
    infoPage = data[5];
    year = unsigned(data[6]) | unsigned(data[7]) << 8;
    month = data[8];
    day = data[9];
    message = reinterpret_cast<const char *>(data + 10);
    label = reinterpret_cast<const char *>(data + 155);
    dateSet = data[228] == 1;
    for (unsigned i = 0; i < 4; ++i)
      colors[i] = uint16_t(data[220 + i * 2]) | uint16_t(data[221 + i * 2])
                                                    << 8;
    configured = true;
    return true;
  }
  bool save() {
    if (!MilestoneV5::validDate(year, month, day) || view > 6 ||
        infoPage >= kInfoPageCount ||
        message.length() > 144 || label.length() > 64)
      return false;
    uint8_t data[256]{};
    memcpy(data, "VC02", 4);
    data[4] = view;
    data[5] = infoPage;
    data[6] = year;
    data[7] = year >> 8;
    data[8] = month;
    data[9] = day;
    memcpy(data + 10, message.c_str(), message.length());
    memcpy(data + 155, label.c_str(), label.length());
    for (unsigned i = 0; i < 4; ++i) {
      data[220 + i * 2] = colors[i];
      data[221 + i * 2] = colors[i] >> 8;
    }
    data[228] = dateSet ? 1 : 0;
    data[229] = (hour24 ? 1 : 0) | (seconds ? 2 : 0) | (scroll ? 4 : 0) |
                (left ? 8 : 0) | (ddayText ? 16 : 0) |
                (afterComplete ? 32 : 0) | (cycle ? 64 : 0) |
                (burnin ? 128 : 0);
    data[230] = speed;
    data[231] = cycleMask;
    data[232] = cycleSeconds;
    memcpy(data + 233, order, 7);
    data[240] = colors[4];
    data[241] = colors[4] >> 8;
    data[242] = colors[5];
    data[243] = colors[5] >> 8;
    data[245] = nowLayout;
    data[246] = screenOffMinutes;
    data[247] = screenOffMinutes >> 8;
    uint32_t crc = MilestoneV5::crc32(data, 252);
    for (unsigned i = 0; i < 4; ++i)
      data[252 + i] = crc >> (8 * i);
    Preferences p;
    if (!p.begin("v5_core", false))
      return false;
    uint8_t check[256];
    bool ok = p.putBytes("record", data, sizeof(data)) == sizeof(data) &&
              p.getBytes("record", check, sizeof(check)) == sizeof(check) &&
              !memcmp(data, check, sizeof(data));
    p.end();
    if (ok)
      configured = true;
    return ok;
  }
  void button(bool prev, bool next, bool ok, uint32_t now) {
    if (prev || next) {
      selectEnabledView(prev ? -1 : 1);
      dirty = true;
      changed = now;
      lastCycle = now;
    }
    if (ok && view == 6) {
      infoPage = (infoPage + 1) % kInfoPageCount;
      dirty = true;
      changed = now;
    }
  }
  bool selectEnabledView(int direction = 1) {
    if (!cycleMask)
      return false;
    const uint8_t selected = MilestoneV5::nextEnabledIndex(
        view, cycleMask, 7, direction < 0 ? -1 : 1);
    const bool changed = selected != view;
    view = selected;
    return changed || bool(cycleMask & (1U << view));
  }
  bool ensureEnabledView() {
    return (cycleMask & (1U << view)) || selectEnabledView(1);
  }
  void service(uint32_t now) {
    if (dirty && now - changed >= 1500) {
      dirty = !save();
      changed = now;
    }
  }
  bool advance(uint32_t now, bool allowed) {
    if (!allowed || !cycle || view == 6) {
      lastCycle = now;
      return false;
    }
    if (now - lastCycle < uint32_t(cycleSeconds) * 1000)
      return false;
    lastCycle = now;
    unsigned at = 0;
    while (at < 7 && order[at] != view)
      ++at;
    for (unsigned step = 1; step <= 7; ++step) {
      uint8_t next = order[(at + step) % 7];
      if (cycleMask & (1U << next)) {
        view = next;
        return true;
      }
    }
    return false;
  }
  void render(V5Hardware &h, bool zeroOnline = false,
              const MilestoneV5::StatusPayload *zero = nullptr,
              uint8_t protocolVersion = 0, uint32_t zeroCapabilities = 0) {
    char clockBuffer[16], date[20];
    String clock;
    h.textScroll = scroll;
    h.textLeft = left;
    h.scrollSpeed = speed;
    h.textShift = burnin;
    if (h.rtcValid) {
      unsigned hour = hour24 ? h.rtc.hour : ((h.rtc.hour + 11) % 12 + 1);
      snprintf(clockBuffer, sizeof(clockBuffer),
               seconds ? "%02u:%02u:%02u" : "%02u:%02u", hour,
               h.rtc.minute, h.rtc.second);
      clock = hour24 ? String(clockBuffer)
                     : String(h.rtc.hour >= 12 ? "PM " : "AM ") + clockBuffer;
      snprintf(date, sizeof(date), "%04u.%02u.%02u", h.rtc.year, h.rtc.month,
               h.rtc.day);
    } else {
      clock = "--:--";
      strcpy(date, "날짜 미확정");
    }
    String dday = "D --?";
    if (h.rtcValid && dateSet) {
      int32_t delta =
          MilestoneV5::dayOrdinal(year, month, day) -
          MilestoneV5::dayOrdinal(h.rtc.year, h.rtc.month, h.rtc.day);
      dday = delta == 0                   ? String("D-DAY")
             : delta < 0 && afterComplete ? String("")
             : ddayText && delta > 0
                 ? String(delta) + "일 남음"
                 : String(delta > 0 ? "D-" : "D+") + abs(delta);
    }
    h.display.fillRect(0, 16, 128, 128, 0);
    const int8_t ox = burnin ? int8_t((millis() / 60000UL) % 3) - 1 : 0;
    switch (view) {
    case 0:
      title(h, label, ox);
      centered(h, dday, 65, ddayText ? u8g2_font_unifont_t_korean2
                                     : u8g2_font_logisoso32_tf,
               colors[3], ox);
      centered(h, String(date) + " " + weekday(h), 92,
               u8g2_font_unifont_t_korean2, colors[1], ox);
      centered(h, clock, 120, u8g2_font_logisoso20_tf, colors[0], ox);
      break;
    case 1:
      title(h, label, ox);
      centered(h, dday, 65, ddayText ? u8g2_font_unifont_t_korean2
                                     : u8g2_font_logisoso32_tf,
               colors[3], ox);
      scrollingLine(h, message, 118, colors[2], ox, 1, 126, true);
      break;
    case 2:
      title(h, "MILESTONE", ox);
      messageBlock(h, 75, 59, 83, colors[2], ox);
      break;
    case 3:
      title(h, "현재 시각", ox);
      if (h.rtcValid) {
        centered(h, clock, 61,
                 seconds ? u8g2_font_logisoso20_tf
                         : u8g2_font_logisoso28_tf,
                 colors[0], ox);
        centered(h, date, 83, u8g2_font_6x10_tf, colors[1], ox);
        centered(h, weekday(h) + "요일 · KST", 113,
                 u8g2_font_unifont_t_korean2, colors[1], ox);
      } else {
        centered(h, "시간 미확정", 66, u8g2_font_unifont_t_korean2,
                 colors[1], ox);
        centered(h, "MODE: MENU", 92, u8g2_font_6x10_tf, colors[5], ox);
      }
      break;
    case 4:
      title(h, date, ox);
      if (h.rtcValid)
        centered(h, clock, 57,
                 seconds ? u8g2_font_logisoso20_tf
                         : u8g2_font_logisoso28_tf,
                 colors[0], ox);
      else
        centered(h, "시간 미확정", 53, u8g2_font_unifont_t_korean2,
                 colors[1], ox);
      rule(h, 68, colors[5]);
      messageBlock(h, 105, 94, 118, colors[2], ox);
      break;
    case 5:
      title(h, label, ox);
      if (h.rtcValid) {
        centered(h, dday, 45,
                 ddayText ? u8g2_font_unifont_t_korean2
                          : u8g2_font_logisoso20_tf,
                 colors[3], ox);
        // Keep the dashboard proportions identical with seconds on or off.
        // The former no-seconds Logisoso font made only this state oversized.
        centered(h, clock, 72, u8g2_font_6x10_tf, colors[0], ox);
        centered(h, String(date) + " " + weekday(h), 96,
                 u8g2_font_unifont_t_korean2, colors[1], ox);
      } else {
        centered(h, "시간 미확정", 78, u8g2_font_unifont_t_korean2,
                 colors[1], ox);
      }
      rule(h, 101, colors[5]);
      scrollingLine(h, message, 123, colors[2], ox, 1, 126, true);
      break;
    case 6:
      if (infoPage == 0) {
        infoHeader(h, "SYSTEM", 1);
        infoLine(h, 29, "FW", MilestoneV5::FIRMWARE_VERSION);
        infoLine(h, 47, "UP", uptime());
        infoLine(h, 65, "RESET", resetReason());
        infoLine(h, 83, "CHIP", String(ESP.getChipModel()) + " R" + ESP.getChipRevision());
        infoLine(h, 101, "CPU", String(getCpuFrequencyMhz()) + " MHz");
        infoLine(h, 119, "CORES", String(ESP.getChipCores()));
      } else if (infoPage == 1) {
        infoHeader(h, "MEMORY", 2);
        infoLine(h, 29, "HEAP", bytes(ESP.getHeapSize()));
        infoLine(h, 47, "USED", bytes(ESP.getHeapSize() - ESP.getFreeHeap()));
        infoLine(h, 65, "FREE", bytes(ESP.getFreeHeap()));
        infoLine(h, 83, "MIN", bytes(ESP.getMinFreeHeap()));
        infoLine(h, 101, "BLOCK", bytes(ESP.getMaxAllocHeap()));
        infoLine(h, 119, "STACK", bytes(uxTaskGetStackHighWaterMark(nullptr)));
      } else if (infoPage == 2) {
        infoHeader(h, "PSRAM", 3);
        infoLine(h, 29, "TOTAL", bytes(ESP.getPsramSize()));
        infoLine(h, 47, "FREE", bytes(ESP.getFreePsram()));
        infoLine(h, 65, "USED", bytes(ESP.getPsramSize() - ESP.getFreePsram()));
        infoLine(h, 83, "BLOCK", bytes(ESP.getMaxAllocPsram()));
        infoLine(h, 101, "TYPE", "EXTERNAL RAM");
        infoLine(h, 119, "USE", "COLOR / MEDIA");
      } else if (infoPage == 3) {
        infoHeader(h, "STORAGE", 4);
        infoLine(h, 29, "FLASH", bytes(ESP.getFlashChipSize()));
        infoLine(h, 47, "APP", appImageBytes ? bytes(appImageBytes) : "UNKNOWN");
        infoLine(h, 65, "OTA FREE", bytes(otaSlotBytes));
        infoLine(h, 83, "SD", h.sdMounted ? "MOUNTED" : "MISSING");
        // Never perform filesystem capacity walks while painting a frame.
        // A slow or marginal card must not stall buttons, SPI heartbeat or UI.
        infoLine(h, 101, "SD WRITE", h.sdMounted ? "READY" : "-");
        infoLine(h, 119, "MEDIA", h.sdMounted ? "/media" : "-");
      } else if (infoPage == 4) {
        infoHeader(h, "NETWORK", 5);
        // The display path must not wait on the Wi-Fi driver lock. MAIN owns
        // only short radio leases; cached link/status flags are sufficient.
        const char *mainRadio = h.radioIndicator == 3 ? "SETUP AP"
                                : h.radioIndicator == 1 ? "BUSY"
                                : h.radioIndicator == 2 ? "ZERO ONLINE"
                                                       : "IDLE";
        infoLine(h, 29, "MAIN", mainRadio);
        infoLine(h, 47, "ZERO", zeroOnline ? "ONLINE" : "OFFLINE");
        infoLine(h, 65, "ZERO WIFI",
                 zeroOnline && zero && (zero->stateFlags & 8) ? "ON" : "OFF");
        infoLine(h, 83, "ZERO BLE",
                 zeroOnline && zero && (zero->stateFlags & 4) ? "ON" : "WAIT");
        infoLine(h, 101, "PROTOCOL",
                 protocolVersion ? String("SPI v") + protocolVersion : "-");
        infoLine(h, 119, "SETUP", "MODE MENU");
      } else if (infoPage == 5) {
        infoHeader(h, "TIME / RTC", 6);
        infoLine(h, 29, "CLOCK", h.rtcValid ? "VALID" : "NOT SET");
        infoLine(h, 47, "RTC", h.rtcPresent ? "DS3231" : "MISSING");
        infoLine(h, 65, "DATE", h.rtcValid ? rtcDate(h) : "-");
        infoLine(h, 83, "TIME", h.rtcValid ? rtcTime(h) : "-");
        infoLine(h, 101, "ZONE", "KST / UTC+9");
        infoLine(h, 119, "SOURCE", h.rtcPresent ? "RTC" : "SYSTEM");
      } else if (infoPage == 6) {
        infoHeader(h, "ENVIRONMENT", 7);
        const bool present = h.environment.address == 0x38;
        const bool valid = present && h.environment.values.hasValue();
        const bool stale = valid && h.environment.values.stale(millis());
        infoLine(h, 29, "SENSOR", present ? "AHT20" : "MISSING");
        infoLine(h, 47, "STATE", !present ? "OFFLINE" : stale ? "STALE" : valid ? "VALID" : "WAITING");
        if (valid) {
          const auto &sample = h.environment.values.filtered();
          infoLine(h, 65, "TEMP", String(sample.temperatureC, 1) + " C");
          infoLine(h, 83, "HUMID", String(sample.humidityPercent, 1) + " %");
        } else {
          infoLine(h, 65, "TEMP", "-");
          infoLine(h, 83, "HUMID", "-");
        }
        infoLine(h, 101, "ERRORS", String(h.environment.errors));
        infoLine(h, 119, "PRESSURE", "NOT INSTALLED");
      } else if (infoPage == 7) {
        infoHeader(h, "MAIN / ZERO", 8);
        infoLine(h, 29, "LINK", zeroOnline ? "ONLINE" : "OFFLINE");
        infoLine(h, 47, "PROTOCOL",
                 protocolVersion ? String("SPI v") + protocolVersion : "-");
        infoLine(h, 65, "ZERO TEMP",
                 zeroOnline && zero && zero->temperatureCenti != INT16_MIN
                     ? String(zero->temperatureCenti / 100.0f, 1) + " C"
                     : "-");
        infoLine(h, 83, "ZERO HEAP",
                 zeroOnline && zero ? bytes(zero->freeHeap) : "-");
        infoLine(h, 101, "ZERO PSRAM",
                 zeroOnline && zero ? bytes(zero->freePsram) : "-");
        infoLine(h, 119, "BLE / WIFI",
                 zeroOnline && zero
                     ? String((zero->stateFlags & 4) ? "ON" : "WAIT") + " / " +
                           ((zero->stateFlags & 8) ? "ON" : "OFF")
                     : "- / -");
      } else {
        infoHeader(h, "FIRMWARE / SAFE", 9);
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
        const esp_partition_t *safe = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY,
            "safety");
        infoLine(h, 29, "RUNNING", running ? running->label : "UNKNOWN");
        infoLine(h, 47, "NEXT OTA", next ? next->label : "NONE");
        infoLine(h, 65, "SAFE", safe ? "AVAILABLE" : "MISSING");
        infoLine(h, 83, "ZERO OTA",
                 zeroCapabilities & MilestoneV5::kCapabilityCompanionOta
                     ? "SUPPORTED"
                     : "UNAVAILABLE");
        infoLine(h, 101, "VERIFY", "SIGN + SHA256");
        infoLine(h, 119, "NEXT PAGE", "OK");
      }
      break;
    }
  }

private:
  uint32_t appImageBytes = 0, otaSlotBytes = 0;
  U8G2_SH1107_128X128_F_SW_I2C canvas{U8G2_R0, U8X8_PIN_NONE,
                                      U8X8_PIN_NONE, U8X8_PIN_NONE};
  uint32_t changed = 0, lastCycle = 0;

  void paint(V5Hardware &h, uint16_t color) {
    h.display.blitMono(canvas.getBufferPtr(), 16, color);
  }
  void centered(V5Hardware &h, const String &text, int baseline,
                const uint8_t *font, uint16_t color, int8_t offset = 0) {
    canvas.clearBuffer();
    canvas.setFont(font);
    int width = canvas.getUTF8Width(text.c_str());
    int x = MilestoneV5::centeredTextX(128, width, offset);
    canvas.drawUTF8(x, baseline, text.c_str());
    paint(h, color);
  }
  void title(V5Hardware &h, const String &text, int8_t offset) {
    canvas.clearBuffer();
    canvas.setFont(u8g2_font_unifont_t_korean2);
    canvas.drawUTF8(max(0, int(offset)), 15, text.c_str());
    paint(h, colors[4]);
    rule(h, 17, colors[5]);
  }
  void rule(V5Hardware &h, int y, uint16_t color) {
    canvas.clearBuffer();
    canvas.drawHLine(0, y, 128);
    paint(h, color);
  }
  void scrollingLine(V5Hardware &h, String text, int baseline,
                     uint16_t color, int8_t offset, int leftEdge, int width,
                     bool centerIfFits) {
    text.replace("\r", "");
    text.replace("\n", " ");
    canvas.clearBuffer();
    canvas.setFont(u8g2_font_unifont_t_korean2);
    const int textWidth = canvas.getUTF8Width(text.c_str());
    int x = this->left || !centerIfFits
                ? leftEdge + int(offset)
                : leftEdge + (width - textWidth) / 2 + int(offset);
    if (scroll && textWidth > width) {
      const int travel = textWidth + width + 12;
      x = leftEdge + width - int((millis() * speed / 1000UL) % travel);
      canvas.setClipWindow(leftEdge, max(0, baseline - 18),
                           leftEdge + width - 1, min(127, baseline + 2));
    }
    canvas.drawUTF8(x, baseline, text.c_str());
    canvas.setMaxClipWindow();
    paint(h, color);
  }
  bool splitMessage(const String &text, String &first, String &second,
                    int maximum = 124) {
    first = second = "";
    String current;
    bool onSecond = false, overflow = false;
    for (size_t i = 0; i < text.length();) {
      size_t bytes = 1;
      const uint8_t lead = text[i];
      if ((lead & 0xE0) == 0xC0)
        bytes = 2;
      else if ((lead & 0xF0) == 0xE0)
        bytes = 3;
      else if ((lead & 0xF8) == 0xF0)
        bytes = 4;
      const String character = text.substring(i, min(text.length(), i + bytes));
      if (character == "\n") {
        if (!onSecond) {
          first = current;
          current = "";
          onSecond = true;
        } else {
          overflow = true;
          break;
        }
      } else if (canvas.getUTF8Width((current + character).c_str()) > maximum &&
                 !current.isEmpty()) {
        if (!onSecond) {
          first = current;
          current = character;
          onSecond = true;
        } else {
          overflow = true;
          break;
        }
      } else {
        current += character;
      }
      i += bytes;
    }
    if (onSecond)
      second = current;
    else
      first = current;
    return overflow;
  }
  void ellipsis(String &text, int maximum) {
    while (!text.isEmpty() &&
           canvas.getUTF8Width((text + "...").c_str()) > maximum) {
      int last = text.length() - 1;
      while (last > 0 && (uint8_t(text[last]) & 0xC0) == 0x80)
        --last;
      text.remove(last);
    }
    text += "...";
  }
  void messageBlock(V5Hardware &h, int single, int firstBaseline,
                    int secondBaseline, uint16_t color, int8_t offset) {
    canvas.setFont(u8g2_font_unifont_t_korean2);
    String first, second;
    const bool overflow = splitMessage(message, first, second);
    if (overflow && scroll) {
      scrollingLine(h, message, single, color, offset, 1, 126, true);
      return;
    }
    if (overflow && second.isEmpty())
      second = "...";
    canvas.clearBuffer();
    canvas.setFont(u8g2_font_unifont_t_korean2);
    if (!second.isEmpty()) {
      if (overflow)
        ellipsis(second, 124);
      const int x1 = left ? 2 : max(1, (128 - canvas.getUTF8Width(first.c_str())) / 2 + offset);
      const int x2 = left ? 2 : max(1, (128 - canvas.getUTF8Width(second.c_str())) / 2 + offset);
      canvas.drawUTF8(x1, firstBaseline, first.c_str());
      canvas.drawUTF8(x2, secondBaseline, second.c_str());
    } else {
      const int x = left ? 2 : max(1, (128 - canvas.getUTF8Width(first.c_str())) / 2 + offset);
      canvas.drawUTF8(x, single, first.c_str());
    }
    paint(h, color);
  }
  String weekday(const V5Hardware &h) const {
    if (!h.rtcValid)
      return "-";
    struct tm value{};
    value.tm_year = h.rtc.year - 1900;
    value.tm_mon = h.rtc.month - 1;
    value.tm_mday = h.rtc.day;
    value.tm_isdst = -1;
    mktime(&value);
    static const char *names[] = {"일", "월", "화", "수", "목", "금", "토"};
    return names[value.tm_wday < 0 || value.tm_wday > 6 ? 0 : value.tm_wday];
  }
  void infoHeader(V5Hardware &h, const String &name, unsigned page) {
    canvas.clearBuffer();
    canvas.setFont(u8g2_font_6x10_tf);
    canvas.drawStr(1, 10, name.c_str());
    canvas.setFont(u8g2_font_5x8_tf);
    char count[8];
    snprintf(count, sizeof(count), "%u/%u", page, kInfoPageCount);
    canvas.drawStr(127 - canvas.getStrWidth(count), 10, count);
    canvas.drawHLine(0, 14, 128);
    paint(h, colors[5]);
  }
  void infoLine(V5Hardware &h, int baseline, const String &name,
                const String &value) {
    canvas.clearBuffer();
    canvas.setFont(u8g2_font_5x8_tf);
    String line = name + ": " + value;
    canvas.drawStr(2, baseline, line.c_str());
    paint(h, colors[5]);
  }
  static String bytes(uint32_t value) {
    if (value >= 1024UL * 1024UL)
      return String(value / (1024.0f * 1024.0f), 2) + " MB";
    if (value >= 1024UL)
      return String(value / 1024.0f, 1) + " KB";
    return String(value) + " B";
  }
  static String bytes64(uint64_t value) {
    if (value >= 1024ULL * 1024ULL * 1024ULL)
      return String(value / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
    if (value >= 1024ULL * 1024ULL)
      return String(value / (1024.0 * 1024.0), 1) + " MB";
    return String(static_cast<unsigned long>(value / 1024ULL)) + " KB";
  }
  static String resetReason() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWER ON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT WDT";
    case ESP_RST_TASK_WDT: return "TASK WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
    }
  }
  static String rtcDate(const V5Hardware &h) {
    char value[16];
    snprintf(value, sizeof(value), "%04u.%02u.%02u", h.rtc.year, h.rtc.month,
             h.rtc.day);
    return value;
  }
  static String rtcTime(const V5Hardware &h) {
    char value[16];
    snprintf(value, sizeof(value), "%02u:%02u:%02u", h.rtc.hour, h.rtc.minute,
             h.rtc.second);
    return value;
  }
  static String uptime() {
    uint64_t seconds = millis() / 1000ULL;
    char result[24];
    snprintf(result, sizeof(result), "%llud %02u:%02u:%02u",
             (unsigned long long)(seconds / 86400ULL),
             unsigned(seconds / 3600ULL % 24ULL),
             unsigned(seconds / 60ULL % 60ULL), unsigned(seconds % 60ULL));
    return result;
  }
  static uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
  }
};
