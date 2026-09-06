#pragma once
// Shared local drivers and signed SD installer; no network runtime in SAFE.
#include "../../../MilestoneV5Main/V5Hardware.h"
#include "../../../MilestoneV5Main/V5SdUpdate.h"
#include <MilestoneV5Boot.h>
#include <MilestoneV5Bundle.h>
#include <MilestoneV5Thermal.h>

namespace V5Safety {
V5Hardware hardware;
V5SdUpdate update;
MilestoneV5::ThermalPolicy thermal(false);
const int pins[] = {
    MilestoneV5::MainPins::kButtonPrev, MilestoneV5::MainPins::kButtonNext,
    MilestoneV5::MainPins::kButtonOk, MilestoneV5::MainPins::kButtonBack,
    MilestoneV5::MainPins::kButtonMode};
bool stable[5]{true, true, true, true, true},
    raw[5]{true, true, true, true, true};
uint32_t changed[5]{};
uint8_t selected = 0;
bool armed = false, redraw = true;
uint32_t armAt = 0;
String message;
bool pressed(unsigned i, uint32_t now) {
  bool current = digitalRead(pins[i]);
  if (raw[i] != current) {
    raw[i] = current;
    changed[i] = now;
  }
  if (stable[i] != current && now - changed[i] >= 30) {
    stable[i] = current;
    return !current;
  }
  return false;
}
bool readIndex(const char *path, MilestoneV5::StableIndex &v) {
  File f = SD.open(path, FILE_READ);
  uint8_t b[MilestoneV5::kStableIndexSize];
  return f && f.size() == sizeof(b) && f.read(b, sizeof(b)) == sizeof(b) &&
         MilestoneV5::decodeStableIndex(b, sizeof(b), v);
}
String restorePath(bool backup) {
  MilestoneV5::StableIndex a{}, b{};
  bool av = readIndex("/firmware/index-a", a),
       bv = readIndex("/firmware/index-b", b);
  if (av || bv) {
    auto &v = (!av || (bv && int32_t(b.generation - a.generation) > 0)) ? b : a;
    const char *id = backup ? v.mainBackup : v.mainStable;
    if (id[0])
      return String("/firmware/sets/") + id + "/main";
  }
  return backup ? "/firmware/main/backup" : "/firmware/main/stable";
}
bool bootSlot(unsigned index) {
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      esp_partition_subtype_t(ESP_PARTITION_SUBTYPE_APP_OTA_0 + index),
      nullptr);
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  esp_app_desc_t desc;
  if (!p)
    return false;
  esp_ota_get_state_partition(p, &state);
  if (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED ||
      esp_ota_get_partition_description(p, &desc) != ESP_OK ||
      esp_ota_set_boot_partition(p) != ESP_OK)
    return false;
  ESP.restart();
  return true;
}
void begin() {
  hardware.begin();
  for (int pin : pins)
    pinMode(pin, INPUT_PULLUP);
  Serial.println("MILESTONE_V5_SAFE_RUNTIME");
}
void service() {
  const uint32_t now = millis();
  bool prev = pressed(0, now), next = pressed(1, now), ok = pressed(2, now),
       back = pressed(3, now), mode = pressed(4, now);
  static uint32_t sampled = 0;
  if (now - sampled >= 1000) {
    sampled = now;
    hardware.sample();
    thermal.sample(hardware.temperature);
    hardware.statusBands("SAFE", false, 0);
    hardware.localLed(true);
    redraw = true;
  }
  const bool busy = update.state == V5SdUpdate::State::Hashing ||
                    update.state == V5SdUpdate::State::Writing;
  if (busy) {
    if (thermal.stopped)
      update.cancel();
    else
      update.service();
    if (update.state == V5SdUpdate::State::Ready)
      ESP.restart();
  } else {
    if (prev || next) {
      selected = (selected + (prev ? 5 : 1)) % 6;
      armed = false;
      redraw = true;
    }
    if (back || mode) {
      armed = false;
      message = "";
      redraw = true;
    }
    if (armed && now - armAt > 8000) {
      armed = false;
      redraw = true;
    }
    if (ok && !thermal.stopped) {
      if (!armed) {
        armed = true;
        armAt = now;
      } else {
        armed = false;
        if (selected < 2) {
          if (!bootSlot(selected))
            message = "슬롯을 사용할 수 없음";
        } else if (selected < 5) {
          String path = selected == 4 ? String("/firmware/main/recovery")
                                      : restorePath(selected == 3);
          if (!update.begin(path.c_str()))
            message = update.error;
        } else {
          if (!hardware.sdMounted)
            hardware.mountSd();
          message = hardware.sdMounted ? "SD 정상 / RTC 확인됨" : "SD 카드 없음";
        }
      }
      redraw = true;
    }
  }
  if (redraw) {
    const char *items[] = {"MAIN 슬롯 A 부팅", "MAIN 슬롯 B 부팅",
                           "SD Stable 복구",   "SD Backup 복구",
                           "SD Recovery 복구", "SD / RTC 다시 확인"};
    hardware.body("SAFE",
                  thermal.stopped ? "온도 보호 정지"
                  : busy          ? "검증 / 설치 중"
                                  : items[selected],
                  armed ? "OK를 다시 눌러 확인" : message,
                  "PREV/NEXT 이동  BACK 취소");
    redraw = false;
  }
  hardware.display.flush();
  delay(1);
}
} // namespace V5Safety
