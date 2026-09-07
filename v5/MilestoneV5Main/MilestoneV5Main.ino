#include <Arduino.h>
#include <SPI.h>

#include "V5Artwork.h"
#include "V5BundleDownload.h"
#include "V5BundleUpdate.h"
#include "V5CoreViews.h"
#include "V5Hardware.h"
#include "V5Portal.h"
#include "V5Radio.h"
#include "V5SdUpdate.h"
#include "V5Video.h"
#include "V5ZeroUpdate.h"
#include <MilestoneV5BoardConfig.h>
#include <MilestoneV5Boot.h>
#include <MilestoneV5Features.h>
#include <MilestoneV5Legacy.h>
#include <MilestoneV5Now.h>
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Runtime.h>
#include <MilestoneV5Thermal.h>
#include <MilestoneV5Transport.h>
#include <MilestoneV5Version.h>

// V5 media/catalog code can use more than Arduino's default 8 KiB loopTask stack.
// 32 KiB prevents Catalog stack frames from corrupting adjacent heap metadata.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

extern "C" bool verifyRollbackLater() { return true; }

namespace {

SPIClass linkSpi(HSPI);
V5Hardware hardware;
V5CoreViews coreViews;
V5Portal portal;
V5Video video;
V5SdUpdate sdUpdate;
V5Artwork artwork;
V5Radio radio;
V5ZeroUpdate zeroUpdate;
V5BundleUpdate bundleUpdate;
V5BundleDownload bundleDownload;
uint32_t txArtGeneration = 0;
uint8_t txOperation = 0;
uint8_t recoveryChoice = 0;
const char *recoveryLabels[] = {"이전 MAIN 슬롯", "MAIN SD 안정본",
                                "MAIN SD 복구본", "ZERO SD 안정본",
                                "ZERO SD 복구본"};
bool videoCategory = false;
MilestoneV5::NowMetadata nowMetadata{};
size_t selectedPhoto = 0;
bool photoVisible = false;
bool redraw = true;
bool temperatureSafe = false;
uint32_t lastInteractionMs = 0;
bool screenSleeping = false;
bool legacyChecked = false;
int8_t importNetwork = -1;
MilestoneV5::ThermalPolicy thermal(false);
uint32_t bootStartedMs = 0;
bool bootValidated = false;
bool restoreArmed = false;
uint32_t lastRtcSyncMs = 0;
bool rtcSynced = false;
MilestoneV5::ProfileController profiles(MilestoneV5::Profile::kCore);
uint8_t txSlot[MilestoneV5::kSpiSlotSize] = {};
uint8_t rxSlot[MilestoneV5::kSpiSlotSize] = {};
uint32_t txSequence = 0;
uint32_t wireLeaseSequence = 0;
uint32_t txLeaseId = 0;
uint32_t lastZeroSequence = 0;
uint32_t lastValidLinkMs = 0;
uint32_t mainBootId = 0;
uint8_t negotiatedProtocolVersion = 0;
uint32_t zeroCapabilities = 0;
uint8_t linkAttempts = 0;
bool awaitingAck = false;
MilestoneV5::StatusPayload zeroStatus = {};
uint32_t lastZeroStatusMs = 0;
bool zeroTemperatureKnown = false;
MilestoneV5::ModeMenu modeMenu;
bool safeModeActive = false;
const char *profileName(MilestoneV5::Profile profile);
const char *menuItemName(MilestoneV5::ModeMenuItem item);

void drawNowProgress(uint8_t frameY, uint8_t footerY) {
  const uint32_t duration = nowMetadata.durationSeconds;
  const uint32_t elapsed = min(nowMetadata.elapsedSeconds, duration);
  hardware.display.drawRect(7, 16 + frameY, 114, 8, 0xFFFF);
  if (duration)
    hardware.display.fillRect(9, 18 + frameY,
                              uint64_t(elapsed) * 110 / duration, 4, 0x07FF);
  char footer[28];
  if (duration)
    snprintf(footer, sizeof(footer), "%lu:%02lu / %lu:%02lu",
             (unsigned long)elapsed / 60, (unsigned long)elapsed % 60,
             (unsigned long)duration / 60, (unsigned long)duration % 60);
  else
    snprintf(footer, sizeof(footer), "%lu:%02lu",
             (unsigned long)elapsed / 60, (unsigned long)elapsed % 60);
  hardware.legacyText(footer, footerY, u8g2_font_6x10_tf);
}

void drawNowArtworkPlaceholder(uint8_t x, uint8_t y, uint8_t size) {
  hardware.legacyFrame(x, y, size, size, 0x7BEF);
  hardware.display.drawLine(x, 16 + y, x + size - 1, 16 + y + size - 1,
                            0x7BEF);
  hardware.display.drawLine(x + size - 1, 16 + y, x, 16 + y + size - 1,
                            0x7BEF);
  String state = artwork.stage ? "LOADING" : "MISSING";
  hardware.legacyText(state, y + size / 2 + 3, u8g2_font_5x8_tf, 0xBDF7);
}

void renderNowWaitingScreen() {
  hardware.legacyClear();
  hardware.legacyText("MILESTONE NOW", 18, u8g2_font_7x14B_tf);
  hardware.legacyRule(25);
  if (zeroStatus.stateFlags & MilestoneV5::kStatusBleError) {
    hardware.legacyText("BLUETOOTH ERROR", 48, u8g2_font_6x10_tf, 0xF800);
    hardware.legacyText("Retrying...", 78, u8g2_font_6x10_tf);
  } else if (!nowMetadata.connected &&
             (zeroStatus.stateFlags & MilestoneV5::kStatusBleAdvertising)) {
    hardware.legacyText("BLE ADVERTISING", 49, u8g2_font_6x10_tf);
    hardware.legacyText("Unlock iPhone", 70, u8g2_font_6x10_tf);
    hardware.legacyText("and play music", 86, u8g2_font_6x10_tf);
  } else if (!nowMetadata.connected) {
    hardware.legacyText("BLUETOOTH STARTING", 54, u8g2_font_6x10_tf);
    hardware.legacyText("Preparing...", 78, u8g2_font_6x10_tf);
  } else {
    hardware.legacyText("AMS CONNECTING", 54, u8g2_font_6x10_tf);
    hardware.legacyText("Preparing AMS", 78, u8g2_font_6x10_tf);
  }
  hardware.legacyText("MODE: MENU", 119, u8g2_font_5x8_tf, 0xBDF7);
}

void renderBootSplash() {
  hardware.display.fillScreen(0);
  hardware.legacyText("CYTRON//MILESTONE", 52, u8g2_font_6x10_tf, 0xFFFF);
  hardware.legacyText("MILESTONE D1", 72, u8g2_font_6x10_tf, 0xFFFF);
  hardware.legacyText(String(MilestoneV5::FIRMWARE_VERSION) + " @ " +
                          profileName(profiles.active()),
                      94, u8g2_font_5x8_tf, 0xBDF7);
}

void renderPortalScreen() {
  hardware.legacyClear();
  hardware.legacyText("MILESTONE SETUP", 10, u8g2_font_6x10_tf, 0x36DF,
                      0);
  hardware.legacyRule(14, 0x36DF);
  hardware.legacyText("Wi-Fi:", 33, u8g2_font_6x10_tf, 0xBE3A, 2);
  hardware.legacyText("MILESTONE-D1-SETUP", 46, u8g2_font_5x8_tf, 0xFFFF,
                      2);
  hardware.legacyText("Password:", 65, u8g2_font_6x10_tf, 0xBE3A, 2);
  if (portal.password.isEmpty())
    hardware.legacyText("OPEN NETWORK", 83, u8g2_font_7x14B_tf, 0x5711);
  else
    hardware.legacyText(portal.password, 83, u8g2_font_7x14B_tf, 0x5711, 15);
  hardware.legacyText("192.168.4.1", 104, u8g2_font_6x10_tf, 0xFFFF, 13);
  hardware.legacyText("BACK: CLOSE", 123, u8g2_font_5x8_tf, 0xBE3A);
}

void renderModeMenu() {
  const auto item = modeMenu.selected();
  const bool profile = item == MilestoneV5::ModeMenuItem::kCore ||
                       item == MilestoneV5::ModeMenuItem::kMedia ||
                       item == MilestoneV5::ModeMenuItem::kNow;
  uint16_t color = item == MilestoneV5::ModeMenuItem::kCore    ? 0x36DF
                   : item == MilestoneV5::ModeMenuItem::kMedia ? 0xFAD8
                   : item == MilestoneV5::ModeMenuItem::kNow   ? 0x5710
                   : item == MilestoneV5::ModeMenuItem::kSetup ? 0x07FF
                   : item == MilestoneV5::ModeMenuItem::kSafeMode
                       ? 0xF800
                   : item == MilestoneV5::ModeMenuItem::kRestart ? 0xFD20
                                                                  : 0xBDF7;
  hardware.legacyClear();
  hardware.legacyText("PROFILE / POWER", 28, u8g2_font_7x14B_tf, 0x36DF);
  const String selected = menuItemName(item);
  if (profile)
    hardware.legacyText(selected, 73, u8g2_font_logisoso28_tf, color);
  else
    // Korean action names already occupy most of the 128 px width at 1x.
    // X2 made Wi-Fi setup/restart spill past both edges. The 68 px baseline
    // optically centers the Korean font in the same selection band.
    hardware.legacyAutoText(selected, 68, color);
  const char *hint = item == MilestoneV5::ModeMenuItem::kSetup
                         ? "OK: AP OPEN"
                     : item == MilestoneV5::ModeMenuItem::kRestart
                         ? "OK: RESTART DEVICE"
                     : item == MilestoneV5::ModeMenuItem::kSafeMode
                         ? "OK: OPEN SAFE"
                     : item == MilestoneV5::ModeMenuItem::kExit
                         ? "OK: EXIT MENU"
                     : static_cast<uint8_t>(item) ==
                               static_cast<uint8_t>(profiles.active())
                         ? "OK: EXIT MENU"
                         : "OK: SWITCH PROFILE";
  hardware.legacyText(hint, 103, u8g2_font_5x8_tf, 0xBE3A);
  hardware.legacyText("PREV/NEXT · BACK: EXIT", 122, u8g2_font_5x8_tf,
                      0xBE3A);
}

void renderBody() {
  if (millis() - bootStartedMs < 3000) {
    renderBootSplash();
  } else if (portal.bundleRequested) {
    hardware.body("업데이트 묶음", "서명된 SD 업데이트", "15초 안에 OK 확인",
                  "BACK 취소");
  } else if (bundleUpdate.phase == V5BundleUpdate::Phase::Copying ||
             bundleUpdate.phase == V5BundleUpdate::Phase::CheckingRunning) {
    hardware.body("묶음 검증",
                  bundleUpdate.phase == V5BundleUpdate::Phase::Copying
                      ? "저장 후 다시 읽는 중"
                      : "실행 중인 MAIN 확인",
                  String(bundleUpdate.progress) + " / " + bundleUpdate.total,
                  "전원을 끄지 마세요");
  } else if (zeroUpdate.busy()) {
    hardware.body("ZERO 복구",
                  zeroUpdate.state == V5ZeroUpdate::State::RebootWait
                      ? "ZERO 부팅 자체 점검"
                      : "서명된 SD 이미지",
                  String(zeroUpdate.received) + " / " + zeroUpdate.size,
                  "전원을 끄지 마세요");
  } else if (sdUpdate.state == V5SdUpdate::State::Hashing ||
             sdUpdate.state == V5SdUpdate::State::Writing) {
    hardware.body(
        "복구",
        sdUpdate.state == V5SdUpdate::State::Hashing ? "SD 이미지 검증 중"
                                                     : "비활성 슬롯 기록 중",
        String(sdUpdate.done) + " / " + sdUpdate.size, "전원을 끄지 마세요");
  } else if (portal.active) {
    renderPortalScreen();
  } else if (modeMenu.isOpen()) {
    renderModeMenu();
  } else if (safeModeActive) {
    hardware.body(
        "안전 모드", temperatureSafe ? "온도 보호 정지" : "기기 복구/진단",
        restoreArmed ? "OK를 다시 눌러 확인" : recoveryLabels[recoveryChoice],
        bundleUpdate.phase == V5BundleUpdate::Phase::Failed ? bundleUpdate.error
        : zeroUpdate.state == V5ZeroUpdate::State::Failed
            ? String(zeroUpdate.error)
        : zeroUpdate.state == V5ZeroUpdate::State::Done
            ? String("ZERO 복구 완료")
        : sdUpdate.state == V5SdUpdate::State::Failed ? sdUpdate.error
                                                      : String("BACK으로 돌아가기"));
  } else if (profiles.active() == MilestoneV5::Profile::kCore) {
    coreViews.render(hardware,
                     lastValidLinkMs &&
                         millis() - lastValidLinkMs <= MilestoneV5::kLinkStaleMs,
                     &zeroStatus, negotiatedProtocolVersion, zeroCapabilities);
  } else if (profiles.active() == MilestoneV5::Profile::kMedia) {
    if (portal.media.hasEnabled()) {
      if (!portal.media.displayEnabled) {
        hardware.legacyClear();
        hardware.legacyText("MEDIA", 34, u8g2_font_logisoso20_tf, 0xF81F);
        hardware.legacyAutoText("BACK으로 재생 종료", 70);
        hardware.legacyText("PREV/NEXT로 다시 선택", 104,
                            u8g2_font_5x8_tf, 0xBDF7);
      }
    } else if (!photoVisible && !video.playing) {
      hardware.legacyClear();
      if (!hardware.fileCount) {
        hardware.legacyText("NO MEDIA", 54, u8g2_font_7x14B_tf);
        hardware.legacyText("OPEN SETUP TO ADD", 82,
                            u8g2_font_6x10_tf);
        redraw = false;
        return;
      }
      hardware.legacyText(videoCategory ? "MEDIA · VIDEO" : "MEDIA · PHOTO",
                          12, u8g2_font_6x10_tf, 0xF81F);
      hardware.legacyRule(15);
      hardware.legacyAutoText(
          hardware.fileCount ? hardware.files[selectedPhoto]
                             : String("미디어 파일 없음"),
          57);
      if (hardware.fileCount)
        hardware.legacyText(String(selectedPhoto + 1) + " / " +
                                hardware.fileCount,
                            78, u8g2_font_6x10_tf, 0x07FF);
      hardware.legacyText("PREV/NEXT 선택 · OK 열기", 103,
                          u8g2_font_5x8_tf, 0xBDF7);
      hardware.legacyText("BACK PHOTO/VIDEO", 120, u8g2_font_5x8_tf,
                          0xBDF7);
    }
  } else {
    hardware.textScroll = coreViews.scroll;
    hardware.textLeft = coreViews.left;
    hardware.scrollSpeed = coreViews.speed;
    hardware.textShift = coreViews.burnin;
    if (!nowMetadata.ready) {
      renderNowWaitingScreen();
      redraw = false;
      return;
    }
    hardware.legacyClear();
    hardware.legacyText(nowMetadata.playing ? "NOW PLAYING" : "PAUSED", 10,
                        u8g2_font_6x10_tf, 0x37F1);
    hardware.legacyRule(14);
    const uint8_t layout = coreViews.nowLayout;
    const String title = nowMetadata.title;
    if (layout == 0) {
      hardware.legacyAutoText(title, 62, 0xFFFF, true);
      drawNowProgress(87, 118);
    } else if (layout == 1) {
      hardware.legacyAutoText(title, 48, 0xFFFF, true);
      hardware.legacyAutoText(nowMetadata.artist, 73, 0x07FF);
      drawNowProgress(87, 118);
    } else if (layout == 2) {
      if (artwork.visible)
        artwork.draw(hardware.display, false, 32);
      else
        drawNowArtworkPlaceholder(34, 16, 60);
      hardware.legacyAutoText(title, 87, 0xFFFF);
      drawNowProgress(94, 122);
    } else {
      if (artwork.visible)
        artwork.draw(hardware.display, true, 31);
      else
        drawNowArtworkPlaceholder(20, 15, 88);
      drawNowProgress(105, 126);
    }
  }
  redraw = false;
}

struct DebouncedButton {
  int pin;
  bool stablePressed;
  bool sampledPressed;
  uint32_t changedMs;

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    stablePressed = sampledPressed = digitalRead(pin) == LOW;
    changedMs = millis();
  }

  bool pressed(uint32_t now) {
    const bool sample = digitalRead(pin) == LOW;
    if (sample != sampledPressed) {
      sampledPressed = sample;
      changedMs = now;
    }
    if (sample != stablePressed && now - changedMs >= 30) {
      stablePressed = sample;
      return stablePressed;
    }
    return false;
  }
};

DebouncedButton prevButton = {MilestoneV5::MainPins::kButtonPrev, false, false,
                              0};
DebouncedButton nextButton = {MilestoneV5::MainPins::kButtonNext, false, false,
                              0};
DebouncedButton okButton = {MilestoneV5::MainPins::kButtonOk, false, false, 0};
DebouncedButton backButton = {MilestoneV5::MainPins::kButtonBack, false, false,
                              0};
DebouncedButton modeButton = {MilestoneV5::MainPins::kButtonMode, false, false,
                              0};
DebouncedButton bootButton = {0, false, false, 0};

const char *profileName(MilestoneV5::Profile profile) {
  switch (profile) {
  case MilestoneV5::Profile::kCore:
    return "CORE";
  case MilestoneV5::Profile::kMedia:
    return "MEDIA";
  case MilestoneV5::Profile::kNow:
    return "NOW";
  }
  return "?";
}

const char *menuItemName(MilestoneV5::ModeMenuItem item) {
  switch (item) {
  case MilestoneV5::ModeMenuItem::kCore:
    return "CORE";
  case MilestoneV5::ModeMenuItem::kMedia:
    return "MEDIA";
  case MilestoneV5::ModeMenuItem::kNow:
    return "NOW";
  case MilestoneV5::ModeMenuItem::kSetup:
    return "Wi-Fi 설정 AP";
  case MilestoneV5::ModeMenuItem::kRestart:
    return "다시 시작";
  case MilestoneV5::ModeMenuItem::kSafeMode:
    return "안전 모드";
  case MilestoneV5::ModeMenuItem::kExit:
    return "닫기";
  }
  return "?";
}

bool switchActiveProfile(MilestoneV5::Profile target) {
  if (temperatureSafe)
    return false;
  safeModeActive = false;
  if (target == profiles.active())
    return true;
  if (!profiles.request(target))
    return false;
  if (target != MilestoneV5::Profile::kNow && !artwork.manual)
    artwork.invalidate();
  video.stop();
  photoVisible = false;
  profiles.notifyQuiesced();
  if (target == MilestoneV5::Profile::kMedia) {
    portal.media.displayEnabled = true;
    hardware.scanPhotos(videoCategory);
    selectedPhoto = 0;
  }
  profiles.notifyStarted(true);
  if (hardware.saveProfile(static_cast<uint8_t>(target)))
    profiles.acknowledgePersisted();
  redraw = true;
  Serial.printf("active profile: %s\n", profileName(profiles.active()));
  return profiles.active() == target;
}

void activateMenuSelection() {
  const MilestoneV5::ModeMenuItem item = modeMenu.selected();
  if (item == MilestoneV5::ModeMenuItem::kExit) {
    modeMenu.close();
    return;
  }
  if (item == MilestoneV5::ModeMenuItem::kSetup) {
    modeMenu.close();
    if (temperatureSafe)
      return;
    if (radio.busy)
      radio.requestPortal = true;
    else
      portal.open();
    redraw = true;
    return;
  }
  if (item == MilestoneV5::ModeMenuItem::kRestart) {
    if (temperatureSafe) {
      modeMenu.close();
      return;
    }
    Serial.println("restart requested from MODE menu");
    ESP.restart();
    return;
  }
  if (item == MilestoneV5::ModeMenuItem::kSafeMode) {
    video.stop();
    if (!temperatureSafe)
      MilestoneV5::bootSafetyApplication();
    safeModeActive = true;
    photoVisible = false;
    redraw = true;
    modeMenu.close();
    Serial.println(
        "internal safe-mode UI requested; optional services disabled");
    return;
  }
  const MilestoneV5::Profile target = static_cast<MilestoneV5::Profile>(item);
  switchActiveProfile(target);
  modeMenu.close();
}

void serviceButtons(uint32_t now) {
  const bool boot = bootButton.pressed(now);
  const bool updating = sdUpdate.state == V5SdUpdate::State::Hashing ||
                        sdUpdate.state == V5SdUpdate::State::Writing ||
                        zeroUpdate.busy() || bundleUpdate.critical();
  if (boot && !safeModeActive && !updating) {
    photoVisible = false;
    if (radio.busy)
      radio.requestPortal = true;
    else if (portal.active)
      portal.close();
    else
      portal.open();
    redraw = true;
  }
  // Always sample every button, even when its action is unavailable.
  const bool mode = modeButton.pressed(now), back = backButton.pressed(now);
  const bool prev = prevButton.pressed(now), next = nextButton.pressed(now),
             ok = okButton.pressed(now);
  if (boot || mode || back || prev || next || ok) {
    lastInteractionMs = now;
    if (screenSleeping) {
      screenSleeping = false;
      hardware.display.sleep(false);
      redraw = true;
      return;
    }
  }
  if (updating)
    return;
  if (now - bootStartedMs < 3000)
    return;
  if (portal.bundleRequested) {
    if (back || boot) {
      portal.bundleRequested = false;
      redraw = true;
    } else if (ok && !temperatureSafe && !radio.busy) {
      portal.bundleRequested = false;
      portal.close();
      video.stop();
      artwork.invalidate();
      safeModeActive = true;
      bundleUpdate.start(portal.bundleSource);
      redraw = true;
    }
    return;
  }
  if (portal.active) {
    if (mode || back) {
      portal.close();
      redraw = true;
    } else if (ok && portal.sync.activePlayback()) {
      portal.sync.requestBrowserToggle();
    }
    return;
  }
  if (mode || back || prev || next || ok)
    redraw = true;
  if (mode) {
    photoVisible = false;
    if (modeMenu.isOpen())
      modeMenu.close();
    else
      modeMenu.open(profiles.active());
    Serial.printf("profile menu %s\n", modeMenu.isOpen() ? "open" : "closed");
  }
  if (back && modeMenu.isOpen()) {
    modeMenu.close();
    Serial.println("profile menu cancelled");
    return;
  }
  if (!modeMenu.isOpen()) {
    if (safeModeActive) {
      if (prev || next) {
        recoveryChoice = (recoveryChoice + (prev ? 4 : 1)) % 5;
        restoreArmed = false;
      }
      if (back && !temperatureSafe) {
        safeModeActive = false;
        restoreArmed = false;
      }
      if (ok && !temperatureSafe) {
        if (radio.busy)
          return;
        if (restoreArmed) {
          if (recoveryChoice == 0)
            MilestoneV5::bootPreviousApplication();
          else if (recoveryChoice < 3) {
            String path = recoveryChoice == 1
                              ? bundleUpdate.stablePath(false)
                              : String("/firmware/main/recovery");
            sdUpdate.begin(path.c_str());
          } else {
            String path = recoveryChoice == 3
                              ? bundleUpdate.stablePath(true)
                              : String("/firmware/zero/recovery");
            zeroUpdate.begin(path.c_str());
          }
          restoreArmed = false;
        } else
          restoreArmed = true;
      }
      return;
    }
    if (profiles.active() == MilestoneV5::Profile::kCore)
      coreViews.button(prev, next, ok, now);
    if (profiles.active() == MilestoneV5::Profile::kNow &&
        (prev || next || ok)) {
      coreViews.nowLayout = (coreViews.nowLayout + (prev ? 3 : 1)) % 4;
      coreViews.save();
    }
    if (profiles.active() == MilestoneV5::Profile::kMedia) {
      if (portal.media.hasEnabled()) {
        if (back)
          portal.media.hide();
        if (prev)
          portal.media.selectRelative(-1);
        if (next)
          portal.media.selectRelative(1);
        if (ok)
          portal.media.toggle();
        return;
      }
      if (back) {
        if (video.playing)
          video.stop();
        else if (photoVisible)
          photoVisible = false;
        else {
          videoCategory = !videoCategory;
          hardware.scanPhotos(videoCategory);
          selectedPhoto = 0;
        }
      }
      if (hardware.fileCount && (prev || next)) {
        video.stop();
        selectedPhoto = (selectedPhoto + (prev ? hardware.fileCount - 1 : 1)) %
                        hardware.fileCount;
        photoVisible = false;
      }
      if (ok && hardware.fileCount) {
        if (hardware.corruptFiles[selectedPhoto]) {
          hardware.body("미디어 오류", "이번 검색에서 제외됨",
                        "파일 교체 후 다시 검색");
          redraw = false;
          return;
        }
        if (videoCategory) {
          if (video.playing)
            video.toggle();
          else if (!video.open(String("/media/video/") +
                               hardware.files[selectedPhoto])) {
            hardware.corruptFiles[selectedPhoto] = true;
            hardware.body("미디어 오류", video.error);
            redraw = false;
          }
          return;
        }
        photoVisible = hardware.photo(selectedPhoto);
        if (!photoVisible) {
          hardware.corruptFiles[selectedPhoto] = true;
          hardware.body("미디어 오류", "잘못된 BMP 파일", "BACK 목록으로");
          redraw = false;
        }
      }
    }
    return;
  }
  bool moved = false;
  if (prev) {
    modeMenu.move(-1);
    moved = true;
  }
  if (next) {
    modeMenu.move(1);
    moved = true;
  }
  if (moved)
    Serial.printf("MODE selection: %s\n", menuItemName(modeMenu.selected()));
  if (ok)
    activateMenuSelection();
}

void exchangeHeartbeat(uint32_t now) {
  if (!digitalRead(MilestoneV5::MainPins::kLinkReady))
    return;
  if (awaitingAck && linkAttempts >= 4) {
    awaitingAck = false;
    negotiatedProtocolVersion = 0;
    txLeaseId = 0;
    linkAttempts = 0;
  }
  if (!awaitingAck) {
    uint8_t payload[476] = {};
    uint16_t payloadLength = 0;
    MilestoneV5::MessageType type = MilestoneV5::MessageType::kHeartbeat;
    txOperation = 0;
    txArtGeneration = 0;
    if (negotiatedProtocolVersion == 0) {
      const MilestoneV5::HelloPayload hello = {
          MilestoneV5::kProtocolVersion,
          MilestoneV5::kProtocolVersion,
          1,
          psramFound() ? uint32_t(MilestoneV5::kCapabilityPsram) : 0UL,
          mainBootId,
      };
      if (!MilestoneV5::encodeHelloPayload(hello, payload, sizeof(payload)))
        return;
      payloadLength = MilestoneV5::kHelloPayloadSize;
      type = MilestoneV5::MessageType::kHello;
    } else if (zeroUpdate.busy() &&
               zeroUpdate.state != V5ZeroUpdate::State::Hashing) {
      size_t n = 0;
      if (zeroUpdate.request(payload, n)) {
        payloadLength = n;
        type = payload[0] == 2 || payload[0] == 3
                   ? MilestoneV5::MessageType::kOtaChunk
                   : MilestoneV5::MessageType::kOtaControl;
      } else {
        payload[0] = static_cast<uint8_t>(profiles.active());
        payload[1] = 0;
        payload[2] = 1;
        payloadLength = 3;
      }
    } else if (bundleUpdate.critical()) {
      payload[0] = static_cast<uint8_t>(profiles.active());
      payload[1] = 0;
      payload[2] = 1;
      payloadLength = 3;
    } else if (bundleDownload.active && bundleDownload.useZero &&
               (txSequence % 4) != 0) {
      size_t n = 0;
      if (!bundleDownload.request(payload, n))
        return;
      payloadLength = n;
      type = MilestoneV5::MessageType::kTaskRequest;
      txOperation = payload[0];
    } else if (portal.wifiPending || portal.wifiReplicate) {
      txOperation = 1;
      type = MilestoneV5::MessageType::kTaskRequest;
      payload[0] = 1;
      MilestoneV5::encodeWifi(portal.wifi, payload + 1);
      payloadLength = 1 + MilestoneV5::kWifiWireBytes;
    } else if (!portal.active && !coreViews.configured && !legacyChecked) {
      payload[0] = 32;
      payload[1] = 0;
      payloadLength = 2;
      type = MilestoneV5::MessageType::kTaskRequest;
      txOperation = 32;
    } else if (!portal.active && importNetwork >= 0) {
      payload[0] = 33;
      payload[1] = importNetwork;
      payloadLength = 2;
      type = MilestoneV5::MessageType::kTaskRequest;
      txOperation = 33;
    } else if (portal.systemPending) {
      payload[0] = 34;
      portal.system.encode(payload + 1);
      payloadLength = 97;
      type = MilestoneV5::MessageType::kTaskRequest;
      txOperation = 34;
    } else if (!safeModeActive && !radio.busy &&
               (radio.zeroArtworkAllowed || artwork.stage == 2) &&
               (profiles.active() == MilestoneV5::Profile::kNow ||
                artwork.manual) &&
               (artwork.stage == 1 || artwork.stage == 2) &&
               (txSequence % 4) != 0) {
      size_t length = 0;
      if (artwork.request(payload, sizeof(payload), length)) {
        type = MilestoneV5::MessageType::kTaskRequest;
        payloadLength = length;
        txOperation = payload[0];
        txArtGeneration = artwork.generation;
      } else
        return;
    } else {
      payload[0] = static_cast<uint8_t>(profiles.active());
      payload[1] = static_cast<uint8_t>(modeMenu.isOpen());
      payload[2] = static_cast<uint8_t>(safeModeActive);
      payloadLength = 3;
    }
    const bool leased = type == MilestoneV5::MessageType::kTaskRequest ||
                        type == MilestoneV5::MessageType::kOtaControl ||
                        type == MilestoneV5::MessageType::kOtaChunk;
    txLeaseId = 0;
    if (leased) {
      if (++wireLeaseSequence == 0)
        ++wireLeaseSequence;
      txLeaseId = wireLeaseSequence;
    }
    const MilestoneV5::FrameFields fields = {
        type, MilestoneV5::kFlagAckRequired, txLeaseId, ++txSequence,
        lastZeroSequence,
    };
    if (!MilestoneV5::encodeSpiSlot(fields, payload, payloadLength, txSlot,
                                    sizeof(txSlot)))
      return;
    awaitingAck = true;
    linkAttempts = 0;
  }
  ++linkAttempts;

  linkSpi.beginTransaction(
      SPISettings(MilestoneV5::kInitialLinkClockHz, MSBFIRST, SPI_MODE0));
  digitalWrite(MilestoneV5::MainPins::kLinkCs, LOW);
  linkSpi.transferBytes(txSlot, rxSlot, sizeof(rxSlot));
  digitalWrite(MilestoneV5::MainPins::kLinkCs, HIGH);
  linkSpi.endTransaction();

  MilestoneV5::DecodedFrame decoded = {};
  MilestoneV5::DecodeStatus frameStatus = MilestoneV5::DecodeStatus::kTooShort;
  if (MilestoneV5::decodeSpiSlot(rxSlot, sizeof(rxSlot), decoded,
                                 frameStatus) == MilestoneV5::SlotStatus::kOk) {
    if (!(decoded.fields.flags & MilestoneV5::kFlagResponse) ||
        decoded.fields.leaseId != txLeaseId ||
        decoded.fields.ackSequence != txSequence)
      return;
    bool valid = false;
    if (decoded.fields.type == MilestoneV5::MessageType::kHelloReply) {
      MilestoneV5::HelloPayload hello = {};
      if (MilestoneV5::decodeHelloPayload(decoded.payload,
                                          decoded.payloadLength, hello) &&
          hello.board == 2) {
        negotiatedProtocolVersion = MilestoneV5::negotiateProtocolVersion(
            MilestoneV5::kProtocolVersion, MilestoneV5::kProtocolVersion,
            hello.minimumVersion, hello.maximumVersion);
        valid = negotiatedProtocolVersion != 0;
        if (valid)
          zeroCapabilities = hello.capabilities;
        if (valid)
          portal.note(3);
        Serial.printf("ZERO protocol negotiated: v%u capabilities=%08lX\n",
                      negotiatedProtocolVersion,
                      static_cast<unsigned long>(hello.capabilities));
        Serial0.printf("ZERO protocol negotiated: v%u capabilities=%08lX\n",
                       negotiatedProtocolVersion,
                       static_cast<unsigned long>(hello.capabilities));
      }
    } else if (decoded.fields.type == MilestoneV5::MessageType::kOtaControl) {
      valid = zeroUpdate.response(decoded.payload, decoded.payloadLength);
      if (valid)
        redraw = true;
    } else if (decoded.fields.type == MilestoneV5::MessageType::kTaskResult) {
      if (decoded.payloadLength == 2 && decoded.payload[0] == 34 &&
          txOperation == 34) {
        valid = true;
        portal.systemPending = false;
      } else if (decoded.payloadLength >= 2 && decoded.payload[0] == 32 &&
                 txOperation == 32) {
        valid = true;
        legacyChecked = true;
        if (decoded.payload[1] == 1 &&
            decoded.payloadLength == MilestoneV5::kLegacySnapshotBytes + 2 &&
            !coreViews.configured) {
          V5CoreViews previous = coreViews;
          if (!portal.applyImportedSystem(decoded.payload + 2) ||
              !coreViews.applyRecord(decoded.payload + 2) || !coreViews.save())
            coreViews = previous;
          else
            redraw = true;
        }
      } else if (decoded.payloadLength >= 2 && decoded.payload[0] == 33 &&
                 txOperation == 33) {
        valid = true;
        if (decoded.payload[1] == 1 &&
            decoded.payloadLength == MilestoneV5::kWifiWireBytes + 2) {
          MilestoneV5::WifiCredentials v;
          MilestoneV5::WifiStore store;
          if (MilestoneV5::decodeWifi(decoded.payload + 2,
                                      MilestoneV5::kWifiWireBytes, v) &&
              !store.save(v))
            valid = false;
        }
        if (valid)
          --importNetwork;
      } else if (decoded.payloadLength >= 14 &&
                 (decoded.payload[0] == 16 || decoded.payload[0] == 17) &&
                 decoded.payload[0] == txOperation) {
        valid = bundleDownload.response(decoded.payload, decoded.payloadLength);
      } else if (decoded.payloadLength == 6 &&
                 (decoded.payload[0] == 3 || decoded.payload[0] == 4) &&
                 decoded.payload[0] == txOperation && decoded.payload[5] <= 2) {
        valid =
            MilestoneV5::readVideoU32(decoded.payload + 1) == txArtGeneration;
        if (valid)
          artwork.result(decoded.payload, decoded.payloadLength);
      } else if (decoded.payloadLength == 2 && decoded.payload[0] == 1 &&
                 decoded.payload[1] <= 2 && txOperation == 1) {
        valid = true;
        if (decoded.payload[1] != 2) {
          MilestoneV5::WifiStore store;
          bool saved = decoded.payload[1] == 0 && store.save(portal.wifi);
          portal.wifiPending = portal.wifiReplicate = false;
          portal.wifiTestState = saved ? 2 : 3;
          portal.wifi = {};
          portal.wifiResult =
              saved ? "연결 시험 및 두 보드 저장 완료"
                    : "연결 시험 또는 저장 실패. 기존 네트워크 유지";
          portal.note(9, saved ? 0 : 1);
        }
      } else if (decoded.payloadLength == 2 && decoded.payload[0] == 34 &&
                 decoded.payload[1] <= 1 && txOperation == 34) {
        valid = true;
        portal.systemPending = false;
        portal.note(10, decoded.payload[1]);
      } else if (decoded.payloadLength == 5 && decoded.payload[0] == 2) {
        const uint32_t epoch = MilestoneV5::readVideoU32(decoded.payload + 1);
        valid = epoch >= 1704067200UL && epoch <= 4102444799UL;
        if (valid && (!rtcSynced || now - lastRtcSyncMs >= 3600000)) {
          rtcSynced = hardware.setRtcEpoch(epoch);
          lastRtcSyncMs = now;
        }
      }
    } else if (decoded.fields.type == MilestoneV5::MessageType::kArtworkChunk) {
      valid = txOperation == 4 && decoded.payloadLength >= 9 &&
              decoded.payloadLength <= 468 &&
              MilestoneV5::readVideoU32(decoded.payload) == txArtGeneration;
      if (valid && txArtGeneration == artwork.generation) {
        valid = artwork.chunk(decoded.payload, decoded.payloadLength);
        if (valid)
          redraw = true;
      }
    } else if (decoded.fields.type == MilestoneV5::MessageType::kStatus) {
      valid = MilestoneV5::decodeStatusPayload(
                  decoded.payload, decoded.payloadLength, zeroStatus) &&
              (zeroStatus.stateFlags & 1);
      if (valid) {
        lastZeroStatusMs = now;
        zeroTemperatureKnown = true;
      }
      if (!valid) {
        negotiatedProtocolVersion = 0;
        awaitingAck = false;
      }
    } else if (decoded.fields.type == MilestoneV5::MessageType::kAmsMetadata) {
      const MilestoneV5::NowMetadata previous = nowMetadata;
      valid = MilestoneV5::decodeNow(decoded.payload, decoded.payloadLength,
                                     nowMetadata);
      if (valid && profiles.active() == MilestoneV5::Profile::kNow &&
          (strcmp(previous.title, nowMetadata.title) ||
           strcmp(previous.artist, nowMetadata.artist) ||
           strcmp(previous.album, nowMetadata.album) ||
           previous.ready != nowMetadata.ready ||
           previous.playing != nowMetadata.playing ||
           previous.elapsedSeconds != nowMetadata.elapsedSeconds ||
           previous.durationSeconds != nowMetadata.durationSeconds))
        redraw = true;
    }
    if (valid) {
      lastZeroSequence = decoded.fields.sequence;
      lastValidLinkMs = now;
      awaitingAck = false;
      txLeaseId = 0;
    }
  }
}

} // namespace

void setup() {
  Serial0.begin(115200);
  Serial0.println("[BOOT-UART] setup entered");
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial0.println("[BOOT] setup entered");
  Serial0.println("[BOOT] hardware.begin START");
  hardware.begin();
  Serial0.println("[BOOT] hardware.begin DONE");
  bootStartedMs = millis();
  renderBootSplash();
  hardware.display.flush();
  Serial0.println("[BOOT] display flush DONE");
  Serial0.println("[BOOT] coreViews.begin START");
  coreViews.begin();
  Serial0.println("[BOOT] coreViews.begin DONE");
  Serial0.println("[BOOT] portal.begin START");
  portal.begin(hardware, coreViews, artwork, nowMetadata, lastValidLinkMs);
  Serial0.println("[BOOT] portal.begin DONE");
  portal.note(6, hardware.sdMounted ? 1 : 0);
  if (!coreViews.configured) {
    uint8_t legacy[MilestoneV5::kLegacySnapshotBytes];
    if (MilestoneV5::legacySnapshot(legacy) &&
        portal.applyImportedSystem(legacy) && coreViews.applyRecord(legacy))
      coreViews.save();
  }
  MilestoneV5::importLegacyNetworks();
  {
    MilestoneV5::WifiStore store;
    MilestoneV5::WifiCredentials v;
    if (!store.load(v))
      importNetwork = 7;
  }
  Serial0.println("[BOOT] bundleUpdate.beginBoot START");
  bundleUpdate.beginBoot(hardware, sdUpdate, zeroUpdate);
  Serial0.println("[BOOT] bundleUpdate.beginBoot DONE");
  profiles = MilestoneV5::ProfileController(
      static_cast<MilestoneV5::Profile>(hardware.savedProfile));
  if (profiles.active() == MilestoneV5::Profile::kMedia)
    hardware.scanPhotos();
  pinMode(MilestoneV5::MainPins::kLinkCs, OUTPUT);
  digitalWrite(MilestoneV5::MainPins::kLinkCs, HIGH);
  pinMode(MilestoneV5::MainPins::kLinkReady, INPUT_PULLDOWN);
  prevButton.begin();
  nextButton.begin();
  okButton.begin();
  backButton.begin();
  modeButton.begin();
  bootButton.begin();
  if (!digitalRead(MilestoneV5::MainPins::kButtonBack) &&
      !digitalRead(MilestoneV5::MainPins::kButtonMode))
    MilestoneV5::bootSafetyApplication();
  Serial0.println("[BOOT] MAIN-ZERO SPI init");
  linkSpi.begin(
      MilestoneV5::MainPins::kLinkSck, MilestoneV5::MainPins::kLinkMiso,
      MilestoneV5::MainPins::kLinkMosi, MilestoneV5::MainPins::kLinkCs);
  Serial0.println("[BOOT] MAIN-ZERO SPI ready");
  mainBootId = esp_random();
  Serial.println("MILESTONE_V5_MAIN_RUNTIME");
  Serial.println(MilestoneV5::FIRMWARE_VERSION);
  Serial0.println("MILESTONE_V5_MAIN_RUNTIME");
  Serial0.println(MilestoneV5::FIRMWARE_VERSION);
}

void loop() {
  const uint32_t now = millis();
  portal.profile = profiles.active();
  if (!bootValidated && now - bootStartedMs >= 10000 &&
      (bundleUpdate.candidateReady || bundleUpdate.candidateRejected ||
       now - bootStartedMs >= 60000)) {
    bootValidated = MilestoneV5::finishBootCandidate(
        ESP.getFreeHeap() > 16384 && bundleUpdate.candidateReady &&
        !bundleUpdate.candidateRejected);
    if (bootValidated)
      portal.note(2);
  }
  serviceButtons(now);
  if (portal.profilePending) {
    switchActiveProfile(portal.requestedProfile);
    portal.profilePending = false;
    portal.profile = profiles.active();
  }
  coreViews.service(now);
  if (coreViews.advance(now, profiles.active() == MilestoneV5::Profile::kCore &&
                                 !safeModeActive && !portal.active &&
                                 !modeMenu.isOpen() && !bundleUpdate.active()))
    redraw = true;
  zeroUpdate.service(now, !temperatureSafe);
  const auto previousBundlePhase = bundleUpdate.phase;
  const bool companionHealthy = lastValidLinkMs != 0 &&
                                now - lastValidLinkMs <= 5000 &&
                                !(zeroStatus.stateFlags & (32 | 64));
  bundleUpdate.service(now, !temperatureSafe, bootValidated, companionHealthy);
  if (previousBundlePhase == V5BundleUpdate::Phase::Copying &&
      bundleUpdate.phase == V5BundleUpdate::Phase::InstallingMain &&
      bundleDownload.ready && portal.bundleSource == bundleDownload.directory)
    bundleDownload.discard();
  if (bundleUpdate.critical() || bundleUpdate.phase != previousBundlePhase)
    redraw = true;
  if (bundleUpdate.phase != previousBundlePhase)
    portal.note(7, static_cast<unsigned>(bundleUpdate.phase));
  portal.bundleBusy = bundleUpdate.active();
  portal.bundleStatus = String(static_cast<unsigned>(bundleUpdate.phase));
  portal.bundleError = bundleUpdate.error;
  if (portal.bundleRequested) {
    redraw = true;
    if (now - portal.bundleRequestedMs >= 15000)
      portal.bundleRequested = false;
  }
  if (sdUpdate.state == V5SdUpdate::State::Hashing ||
      sdUpdate.state == V5SdUpdate::State::Writing) {
    if (temperatureSafe)
      sdUpdate.cancel();
    else
      sdUpdate.service();
    if (sdUpdate.state == V5SdUpdate::State::Ready)
      ESP.restart();
  }
  if (!safeModeActive && !bundleUpdate.critical())
    hardware.environment.service(now);
  if (!safeModeActive && !bundleUpdate.critical())
    artwork.maintain(now, hardware.sdMounted);
  if (!safeModeActive && !bundleUpdate.critical() &&
      profiles.active() == MilestoneV5::Profile::kNow)
    artwork.observe(nowMetadata, now, hardware.sdMounted);
  if (portal.downloadRequested) {
    portal.downloadRequested = false;
    bundleDownload.begin(portal.downloadVersion,
                         lastValidLinkMs != 0 &&
                             (portal.active || !(zeroStatus.stateFlags & 4)));
  }
  radio.downloadWanted = bundleDownload.active && !bundleDownload.useZero;
  radio.service(now, portal, hardware, artwork, lastValidLinkMs != 0,
                zeroStatus.stateFlags,
                safeModeActive || bundleUpdate.critical());
  bundleDownload.service(radio.downloadReady,
                         !temperatureSafe && !safeModeActive &&
                             !bundleUpdate.active(),
                         lastValidLinkMs != 0);
  if (portal.downloadBusy && !bundleDownload.active)
    portal.note(8, bundleDownload.ready ? 0 : 1);
  portal.downloadBusy = bundleDownload.active;
  portal.downloadReady = bundleDownload.ready;
  portal.downloadStatus =
      bundleDownload.active ? String("다운로드 ") + bundleDownload.received +
                                  " / " + bundleDownload.total
      : bundleDownload.ready
          ? String("다운로드 검증 완료. 기기 확인 후 설치할 수 있습니다.")
          : bundleDownload.error;
  if (bundleDownload.ready)
    portal.bundleSource = bundleDownload.directory;
  if (radio.redraw) {
    redraw = true;
    radio.redraw = false;
  }
  if (safeModeActive || bundleUpdate.critical())
    portal.close();
  portal.service();
  if (portal.rescanRequested) {
    video.stop();
    hardware.scanPhotos(videoCategory);
    selectedPhoto = 0;
    photoVisible = false;
    portal.rescanRequested = false;
    redraw = true;
  }
  if (!safeModeActive && !bundleUpdate.critical() && portal.environmentLogging)
    hardware.logEnvironment(now);
  static uint32_t lastSampleMs = 0;
  if (now - lastSampleMs >= 1000) {
    lastSampleMs = now;
    hardware.sample();
    bool previouslyStopped = temperatureSafe;
    thermal.sample(hardware.temperature);
    temperatureSafe = thermal.stopped;
    if (previouslyStopped != temperatureSafe)
      portal.note(5, temperatureSafe ? 1 : 0);
    if (getCpuFrequencyMhz() != (thermal.throttled ? 80UL : 240UL))
      setCpuFrequencyMhz(thermal.throttled ? 80 : 240);
    if (temperatureSafe) {
      video.stop();
      portal.sync.remove();
      safeModeActive = true;
      photoVisible = false;
    }
    hardware.radioIndicator =
        temperatureSafe                                                  ? 6
        : bundleUpdate.active()                                          ? 5
        : bundleDownload.active                                          ? 4
        : portal.active                                                  ? 3
        : radio.busy                                                      ? 1
        : (zeroStatus.stateFlags & 8) && lastValidLinkMs                 ? 2
                                                                         : 0;
    if (now - bootStartedMs >= 3000)
      hardware.statusBands(
          profileName(profiles.active()),
          zeroTemperatureKnown && lastValidLinkMs &&
              now - lastValidLinkMs <= MilestoneV5::kLinkStaleMs,
          zeroStatus.temperatureCenti);
    uint16_t minute = hardware.rtc.hour * 60 + hardware.rtc.minute;
    const auto &s = portal.system;
    bool night = hardware.rtcValid &&
                 (s.nightStart > s.nightEnd
                      ? (minute >= s.nightStart || minute < s.nightEnd)
                      : (minute >= s.nightStart && minute < s.nightEnd));
    hardware.led.setBrightness(s.ledEnabled ? (night ? s.ledNight : s.ledDay)
                                            : 0);
    hardware.localLed(safeModeActive, portal.active, radio.busy);
    // Device information is intentionally static until the user changes its
    // page. Rebuilding it every second repeatedly queried Wi-Fi/SD/heap state
    // and made button input visibly lag, while status bands already refresh
    // independently.
    if ((profiles.active() == MilestoneV5::Profile::kCore &&
         coreViews.view != 6) ||
        safeModeActive)
      redraw = true;
  }
  static uint32_t lastBodyRender = 0;
  bool shouldSleep =
      coreViews.screenOffMinutes &&
      now - lastInteractionMs >= uint32_t(coreViews.screenOffMinutes) * 60000 &&
      !safeModeActive && !portal.active && !modeMenu.isOpen() &&
      !bundleUpdate.active() && !bundleDownload.active && !video.playing;
  if (shouldSleep != screenSleeping) {
    screenSleeping = shouldSleep;
    hardware.display.sleep(shouldSleep);
  }
  if (hardware.textScroll && !safeModeActive && !portal.active &&
      !modeMenu.isOpen() && !video.playing && now - lastBodyRender >= 80)
    redraw = true;
  if (redraw && ((!zeroUpdate.busy() && !bundleUpdate.critical()) ||
                 now - lastBodyRender >= 100)) {
    renderBody();
    lastBodyRender = now;
  }
  if (!safeModeActive && !bundleUpdate.critical() && !modeMenu.isOpen() &&
      !portal.active && video.playing) {
    video.repeat = portal.mediaRepeat;
    video.monochrome = hardware.monochrome;
    video.service(hardware.display, now);
    if (!video.playing && video.error[0]) {
      hardware.corruptFiles[selectedPhoto] = true;
      hardware.body("미디어 오류", video.error, "BACK 목록으로");
    }
  }
  if (!safeModeActive && !bundleUpdate.critical() && !modeMenu.isOpen() &&
      portal.active && profiles.active() == MilestoneV5::Profile::kMedia &&
      portal.sync.activePlayback()) {
    const bool rendered =
        portal.sync.servicePlayback(hardware.display, now, hardware.monochrome);
    if (!rendered && portal.sync.state == V5SyncMedia::State::Error)
      redraw = true;
  }
  if (!safeModeActive && !bundleUpdate.critical() && !modeMenu.isOpen() &&
      !portal.active && profiles.active() == MilestoneV5::Profile::kMedia &&
      portal.media.hasEnabled())
    portal.media.service(hardware.display, now);
  static uint32_t lastHeartbeatMs = 0;
  hardware.display.flush();
  const bool transferringZero =
      (zeroUpdate.busy() && zeroUpdate.state != V5ZeroUpdate::State::Hashing &&
       zeroUpdate.state != V5ZeroUpdate::State::RebootWait) ||
      (bundleDownload.active && bundleDownload.useZero);
  if (now - lastHeartbeatMs >=
      (transferringZero ? 5UL
       : awaitingAck    ? 20UL
       : (artwork.stage == 1 || artwork.stage == 2) && !safeModeActive
           ? 20UL
           : MilestoneV5::kHeartbeatIntervalMs)) {
    lastHeartbeatMs = now;
    exchangeHeartbeat(now);
  }
  if (lastValidLinkMs != 0 &&
      now - lastValidLinkMs > MilestoneV5::kLinkStaleMs) {
    portal.note(4);
    lastValidLinkMs = 0;
    negotiatedProtocolVersion = 0;
    zeroCapabilities = 0;
    awaitingAck = false;
    txLeaseId = 0;
    nowMetadata = {};
    redraw = true;
    Serial.println(
        "ZERO link stale; MAIN standalone services remain available");
  }
  delay(1);
}
