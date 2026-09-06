#pragma once
#include "V5ArtworkPortal.h"
#include "V5CoreViews.h"
#include "V5Hardware.h"
#include "V5LegacyMedia.h"
#include <DNSServer.h>
#include <MilestoneV5Diagnostics.h>
#include <MilestoneV5Features.h>
#include <MilestoneV5SystemSettings.h>
#include <MilestoneV5Version.h>
#include <MilestoneV5WifiStore.h>
#include <WebServer.h>
#include <WiFi.h>
#include <nvs_flash.h>

#define MILESTONE_HAS_MEDIA 1
#define MILESTONE_HAS_NOW_VIEW 1
#define MILESTONE_HAS_GENERAL_VIEWS 1
#define MILESTONE_HAS_STREAM 0
#define MILESTONE_HAS_ARTWORK_MANAGER 1
#define MILESTONE_V5_INTEGRATED 1
#include "../../PortalPage.h"
#undef MILESTONE_V5_INTEGRATED
#undef MILESTONE_HAS_ARTWORK_MANAGER
#undef MILESTONE_HAS_STREAM
#undef MILESTONE_HAS_GENERAL_VIEWS
#undef MILESTONE_HAS_NOW_VIEW
#undef MILESTONE_HAS_MEDIA

// Opened only through a local BOOT press; AP password is freshly generated.
// Every write requires an unpredictable token, including from another website.
class V5Portal {
public:
  bool active = false;
  MilestoneV5::SystemSettings system;
  bool systemPending = false;
  MilestoneV5::Diagnostics diagnostics;
  V5LegacyMedia media;
  void note(uint32_t code, uint32_t value = 0) {
    diagnostics.note(code, value, millis(), time(nullptr));
  }
  String password;
  MilestoneV5::Profile profile = MilestoneV5::Profile::kCore;
  bool rescanRequested = false;
  uint8_t luminance = 92;
  int8_t contrast = 8;
  bool environmentLogging = false;
  bool wifiPending = false, wifiReplicate = false;
  bool timeSyncRequested = false, timeSyncSuccess = false;
  bool profilePending = false, closeRequested = false;
  MilestoneV5::Profile requestedProfile = MilestoneV5::Profile::kCore;
  MilestoneV5::WifiCredentials wifi;
  String wifiResult = "";
  float offsets[3] = {0, 0, 0};
  bool mediaRepeat = true;
  bool bundleRequested = false, bundleBusy = false;
  uint32_t bundleRequestedMs = 0;
  String bundleStatus = "idle", bundleError;
  bool downloadRequested = false, downloadBusy = false, downloadReady = false;
  String downloadVersion = "latest", downloadStatus,
         bundleSource = "/firmware/incoming";

  void begin(V5Hardware &h, V5CoreViews &views, V5Artwork &art,
             const MilestoneV5::NowMetadata &metadata,
             const uint32_t &lastLinkMs) {
    hardware = &h;
    artwork = &art;
    now = &metadata;
    zeroLastLinkMs = &lastLinkMs;
    media.begin(h.sdMounted);
    system.begin();
    diagnostics.begin();
    note(1);
    systemPending = system.loaded;
    {
      MilestoneV5::WifiStore store;
      wifiReplicate = store.load(wifi);
    }
    core = &views;
    const char *headers[] = {"X-CSRF-Token", "X-Artwork-Key"};
    server.collectHeaders(headers, 2);
    artworkPortal.begin(
        server, art, [this] { return authorize(); },
        [this] {
          touch();
          return token;
        });
    Preferences prefs;
    if (prefs.begin("milestone_v5", true)) {
      uint16_t tone =
          prefs.getUShort("tone", uint16_t(92) | (uint16_t(28) << 8));
      luminance = tone & 255;
      contrast = int(tone >> 8) - 20;
      if (luminance < 50 || luminance > 100 || contrast < -20 ||
          contrast > 20) {
        luminance = 92;
        contrast = 8;
      }
      environmentLogging = prefs.getBool("env_log", false);
      uint8_t media = prefs.getUChar("media", 2);
      if (media > 7)
        media = 2;
      h.monochrome = media & 1;
      mediaRepeat = media & 2;
      h.reverseSort = media & 4;
      String env = prefs.getString("env_config", "");
      String limits = prefs.getString("env_limits", "");
      unsigned mask;
      float warnings[3], dangers[3];
      int end = 0;
      if (sscanf(limits.c_str(), "L1 %u %f %f %f %f %f %f%n", &mask,
                 &warnings[0], &dangers[0], &warnings[1], &dangers[1],
                 &warnings[2], &dangers[2], &end) == 7 &&
          end == int(limits.length()) && validLimits(mask, warnings, dangers)) {
        h.environment.displayMask = mask & 3U;
        memcpy(h.environment.warning, warnings, sizeof(warnings));
        memcpy(h.environment.danger, dangers, sizeof(dangers));
      }
      int on, f;
      unsigned interval, logInterval;
      float t, humidity, pressure;
      int consumed = 0;
      if (sscanf(env.c_str(), "E1 %d %d %u %u %f %f %f%n", &on, &f, &interval,
                 &logInterval, &t, &humidity, &pressure, &consumed) == 7 &&
          consumed == int(env.length()) && (on == 0 || on == 1) &&
          (f == 0 || f == 1) && interval >= 1000 && interval <= 60000 &&
          logInterval >= 60000 && logInterval <= 86400000 && isfinite(t) &&
          isfinite(humidity) && isfinite(pressure) && fabsf(t) <= 20 &&
          fabsf(humidity) <= 50 && fabsf(pressure) <= 200) {
        offsets[0] = t;
        offsets[1] = humidity;
        offsets[2] = 0;
        h.environment.configure(on, f, interval, logInterval, t, humidity,
                                pressure);
      }
      prefs.end();
    }
    h.display.setTone(luminance, contrast);
    server.on("/v5-debug", HTTP_GET, [this] {
      touch();
      String page =
          F("<!doctype html><html lang='ko'><meta charset='utf-8'><meta "
            "name='viewport' content='width=device-width'><title>MILESTONE "
            "설정</title><style>body{font:16px "
            "sans-serif;max-width:640px;margin:32px "
            "auto;padding:16px;background:#15191f;color:#eee}label{display:"
            "block;margin:20px "
            "0}button,input{font:inherit;padding:10px}a{color:#8cf}</"
            "style><h1>MILESTONE 설정</h1><p><a href='/status'>장치 "
            "상태</a></p><form method='post' action='/settings'><input "
            "type='hidden' name='token' value='");
      page += token +
              "'><label>화면 명도 <input name='luminance' type='number' "
              "min='50' max='100' value='" +
              String(luminance) + "'></label>";
      page += "<label>화면 대비 <input name='contrast' type='number' min='-20' "
              "max='20' value='" +
              String(contrast) + "'></label>";
      page += "<label><input type='checkbox' name='env_log' value='1' " +
              String(environmentLogging ? "checked" : "") +
              ">환경 이력 SD 기록</label><button>저장</button></form>";
      page += "<form method='post' action='/rescan'><input type='hidden' "
              "name='token' value='" +
              token + "'><p><button>사진 목록 새로 읽기</button></p></form>";
      page +=
          "<form method='post' action='/wifi'><input type='hidden' "
          "name='token' value='" +
          token +
          "'><label>Wi-Fi 이름 <input name='ssid' maxlength='32' "
          "required></label><label>보안 <select name='security'><option "
          "value='0'>Personal / Open</option><option value='1'>Enterprise "
          "PEAP</option></select></label><label>암호 <input name='password' "
          "type='password' maxlength='63'></label><label>PEAP 사용자 이름 "
          "<input name='username' maxlength='64'></label><label>PEAP 외부 ID "
          "(선택) <input name='identity' maxlength='64'></label><button>연결 "
          "시험 후 저장</button></form><p>" +
          wifiResult + "</p>";
      {
        MilestoneV5::WifiStore store;
        MilestoneV5::WifiCredentials list[MilestoneV5::kWifiMaxNetworks];
        uint8_t count = 0;
        store.loadAll(list, count);
        page += "<p>저장된 Wi-Fi (최대 8개): ";
        for (unsigned i = 0; i < count; ++i)
          page += escape(list[i].ssid) + " ";
        page += "</p>";
      }
      char date[16];
      snprintf(date, sizeof(date), "%04u-%02u-%02u", core->year, core->month,
               core->day);
      page += "<h2>CORE 화면</h2><form method='post' action='/core'><input "
              "type='hidden' name='token' value='" +
              token + "'>";
      page += "<label>목표 날짜 <input type='date' name='date' "
              "min='2000-01-01' max='2099-12-31' value='" +
              String(date) + "' required></label>";
      page += "<label>일정 이름 <input name='label' maxlength='64' value='" +
              escape(core->label) +
              "'></label><label>문구 <input name='message' maxlength='144' "
              "value='" +
              escape(core->message) + "'></label>";
      const char *names[] = {"시간",   "날짜",      "문구",
                             "디데이", "일정 이름", "정보"};
      for (unsigned i = 0; i < 6; ++i) {
        const uint16_t color = core->colors[i];
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                 ((color >> 11) & 31) * 255 / 31,
                 ((color >> 5) & 63) * 255 / 63, (color & 31) * 255 / 31);
        page +=
            "<label>" + String(names[i]) +
            " 색상 <input type='color' name='color" + i + "' value='" + hex +
            "' oninput='this.parentElement.style.color=this.value'></label>";
      }
      page += "<button>CORE 저장</button></form><form method='post' "
              "action='/display-options'><input type='hidden' name='token' "
              "value='" +
              token + "'>";
      const char *optionKeys[] = {"hour24", "seconds",   "scroll",
                                  "left",   "dday_text", "after_complete",
                                  "cycle",  "burnin"};
      const char *optionLabels[] = {"24시간",           "초 표시",
                                    "긴 문구 스크롤",   "왼쪽 정렬",
                                    "디데이 한글 표기", "지난 디데이 숨김",
                                    "CORE 자동 순환",   "화면 위치 미세 이동"};
      const bool flags[] = {core->hour24, core->seconds,  core->scroll,
                            core->left,   core->ddayText, core->afterComplete,
                            core->cycle,  core->burnin};
      for (unsigned i = 0; i < 8; ++i)
        page += "<label><input type='checkbox' name='" + String(optionKeys[i]) +
                "' value='1' " + String(flags[i] ? "checked" : "") + ">" +
                optionLabels[i] + "</label>";
      page += "<label>스크롤 속도 <input name='speed' type='number' min='5' "
              "max='80' value='" +
              String(core->speed) +
              "'></label><label>순환 간격 (초) <input name='cycle_seconds' "
              "type='number' min='3' max='60' value='" +
              String(core->cycleSeconds) +
              "'></label><label>화면 꺼짐 (분, 0=안 끔) <input "
              "name='screen_off' type='number' min='0' max='1440' value='" +
              String(core->screenOffMinutes) + "'></label>";
      const char *views[] = {"디데이+시간", "디데이+문구", "문구",     "시계",
                             "문구+시계",   "대시보드",    "장치 정보"};
      for (unsigned i = 0; i < 7; ++i) {
        page += "<label><input type='checkbox' name='enabled" + String(i) +
                "' value='1' " +
                String(core->cycleMask & (1U << i) ? "checked" : "") + ">" +
                views[i] + "</label><label>순서 " + String(i + 1) +
                " <select name='order" + i + "'>";
        for (unsigned j = 0; j < 7; ++j)
          page += "<option value='" + String(j) + "' " +
                  String(core->order[i] == j ? "selected" : "") + ">" +
                  views[j] + "</option>";
        page += "</select></label>";
      }
      page += "<label>NOW 구성 <select name='now_layout'>";
      const char *layouts[] = {"곡명", "곡명+아티스트", "곡명+표지",
                               "표지 중심"};
      for (unsigned i = 0; i < 4; ++i)
        page += "<option value='" + String(i) + "' " +
                String(core->nowLayout == i ? "selected" : "") + ">" +
                layouts[i] + "</option>";
      page +=
          "</select></label><button>표시 옵션 "
          "저장</button></form><h2>환경센서</h2><form method='post' "
          "action='/environment'><input type='hidden' name='token' value='" +
          token + "'>";
      page += "<label><input type='checkbox' name='enabled' value='1' " +
              String(hardware->environment.enabled ? "checked" : "") +
              ">센서 사용</label><label><input type='checkbox' "
              "name='fahrenheit' value='1' " +
              String(hardware->environment.useFahrenheit ? "checked" : "") +
              ">화씨 표시 (로그는 섭씨)</label>";
      page += "<label>측정 주기 (초) <input type='number' name='interval' "
              "min='1' max='60' value='" +
              String(hardware->environment.intervalMs / 1000) +
              "'></label><label>로그 주기 (분) <input type='number' "
              "name='log_interval' min='1' max='1440' value='" +
              String(hardware->environment.logIntervalMs / 60000) +
              "'></label>";
      const char *units[] = {"온도 °C", "습도 %"};
      for (unsigned i = 0; i < 2; ++i)
        page += "<label>" + String(units[i]) +
                " 보정 <input type='number' step='0.01' name='offset" + i +
                "' value='" + String(offsets[i], 2) + "'></label>";
      page += "<button>저장 및 센서 재검색</button></form><form method='post' "
              "action='/environment-limits'><input type='hidden' name='token' "
              "value='" +
              token + "'>";
      for (unsigned i = 0; i < 2; ++i)
        page += "<label><input type='checkbox' name='show" + String(i) +
                "' value='1' " +
                String(hardware->environment.displayMask & (1U << i) ? "checked"
                                                                     : "") +
                ">" + units[i] + " 표시</label><label>경고 " +
                String("이상") +
                " <input type='number' step='0.1' name='warn" + i +
                "' value='" + String(hardware->environment.warning[i], 1) +
                "'></label><label>위험 이상" +
                " <input type='number' step='0.1' name='danger" + i +
                "' value='" + String(hardware->environment.danger[i], 1) +
                "'></label>";
      page += "<button>표시 및 경고 저장</button></form><h2>MEDIA</h2><form "
              "method='post' action='/media'><input type='hidden' name='token' "
              "value='" +
              token + "'>";
      page += "<label><input type='checkbox' name='mono' value='1' " +
              String(hardware->monochrome ? "checked" : "") +
              ">흑백 (NOW에는 적용하지 않음)</label><label><input "
              "type='checkbox' name='repeat' value='1' " +
              String(mediaRepeat ? "checked" : "") +
              ">영상 반복</label><label><input type='checkbox' name='reverse' "
              "value='1' " +
              String(hardware->reverseSort ? "checked" : "") +
              ">파일명 역순</label><button>MEDIA 저장</button></form>";
      page += "<h2>NOW</h2><a href='/artwork'>앨범아트 "
              "검색·미리보기·업로드·관리</a><p>개발판 NOW: 캐시에 없는 곡의 "
              "제목·아티스트·앨범을 기존 앨범아트 서버에 HTTP로 전송합니다. "
              "암호화된 전송은 아직 지원하지 않습니다.</p>";
      page += "<h2>시스템</h2><form method='post' action='/system'><input "
              "type='hidden' name='token' value='" +
              token + "'>";
      const char *sysKeys[] = {"led", "fixed_ap", "wifi_sleep", "boot_sync"},
                 *sysLabels[] = {"상태 LED", "고정 AP 암호", "Wi-Fi 절전",
                                 "부팅 시 시간 동기화"};
      bool sysValues[] = {system.ledEnabled, system.fixedAp, system.wifiSleep,
                          system.bootSync};
      for (unsigned i = 0; i < 4; ++i)
        page += "<label><input type='checkbox' name='" + String(sysKeys[i]) +
                "' value='1' " + String(sysValues[i] ? "checked" : "") + ">" +
                sysLabels[i] + "</label>";
      page += "<label>새 AP 암호 (비우면 기존 유지) <input type='password' "
              "name='ap_password' maxlength='63'></label><label><input "
              "type='checkbox' name='open_ap' value='1'>암호 없는 AP 허용 "
              "(주변에서 설정에 접근할 수 있음)</label>";
      const char *numberKeys[] = {"led_day",   "led_night", "night_start",
                                  "night_end", "ntp",       "retry"},
                 *numberLabels[] = {"낮 LED 밝기 0–255",
                                    "밤 LED 밝기 0–255",
                                    "야간 시작 (분, 0–1439)",
                                    "야간 종료 (분, 0–1439)",
                                    "시간 동기화 간격 (초, 0=수동)",
                                    "Wi-Fi 재시도 (초)"};
      uint32_t numberValues[] = {system.ledDay,     system.ledNight,
                                 system.nightStart, system.nightEnd,
                                 system.ntpSeconds, system.retrySeconds};
      for (unsigned i = 0; i < 6; ++i)
        page += "<label>" + String(numberLabels[i]) +
                " <input type='number' name='" + numberKeys[i] + "' value='" +
                String(numberValues[i]) + "'></label>";
      page += "<button>시스템 저장</button></form>";
      page +=
          "<p><a href='/diagnostics'>진단 이력 (복사·저장 가능)</a></p><form "
          "method='post' action='/diagnostics/clear'><input type='hidden' "
          "name='token' value='" +
          token + "'><button>진단 이력만 지우기</button></form>";
      page += "<h2>업데이트</h2><p>" + escape(downloadStatus) +
              "</p><form method='post' action='/download'><input type='hidden' "
              "name='token' value='" +
              token +
              "'><label>릴리스 버전 (또는 latest) <input name='version' "
              "value='latest' maxlength='23'></label><button>서명된 업데이트 "
              "다운로드</button></form>";
      page +=
          "<h2>서명된 SD 업데이트 묶음</h2><p>상태: " + escape(bundleStatus) +
          " " + escape(bundleError) +
          "</p><form method='post' action='/bundle'><input type='hidden' "
          "name='token' value='" +
          token +
          "'><p>다운로드 완료한 묶음 또는 SD에 준비한 묶음을 설치합니다. MAIN "
          "승인 후에만 ZERO를 설치하며, 10분 안정화 후 Stable로 "
          "승격합니다.</p><button>기기에서 확인 후 설치</button></form></html>";
      server.sendHeader("Cache-Control", "no-store");
      server.send(200, "text/html; charset=utf-8", page);
    });
    server.on("/", HTTP_GET, [this] {
      touch();
      server.sendHeader("Cache-Control", "no-store");
      server.send_P(200, "text/html; charset=utf-8", MILESTONE_PORTAL_HTML);
    });
    server.on("/status", HTTP_GET, [this] {
      touch();
      String s =
          "{\"rtcValid\":" + String(hardware->rtcValid ? "true" : "false") +
          ",\"sdMounted\":" + String(hardware->sdMounted ? "true" : "false");
      s += ",\"freeHeap\":" + String(ESP.getFreeHeap()) +
           ",\"freePsram\":" + String(ESP.getFreePsram());
      s += ",\"environmentErrors\":" + String(hardware->environment.errors);
      s += ",\"logErrors\":" + String(hardware->environmentLog.errors) +
           ",\"environmentAddress\":" + String(hardware->environment.address) +
           ",\"environmentChipId\":" + String(hardware->environment.chip);
      s += ",\"environmentLastValidMs\":" +
           String(hardware->environment.values.lastValidMs());
      if (hardware->environment.values.hasValue()) {
        const auto &v = hardware->environment.values.filtered();
        s += ",\"temperatureC\":" + String(v.temperatureC, 2) +
             ",\"pressureHpa\":null" +
             ",\"humidityPercent\":" +
             (v.hasHumidity ? String(v.humidityPercent, 2) : String("null"));
      }
      s += ",\"environmentStale\":" +
           String(hardware->environment.values.stale(millis()) ? "true"
                                                               : "false") +
           "}";
      server.send(200, "application/json", s);
    });
    server.on("/diagnostics", HTTP_GET, [this] {
      touch();
      String text = "MILESTONE v5 diagnostics\nEvent: 1 boot, 2 boot accepted, "
                    "3 ZERO online, 4 ZERO lost, 5 thermal, 6 SD, 7 bundle "
                    "stage, 8 download, 9 Wi-Fi test\n";
      for (unsigned i = 0; i < diagnostics.count; ++i) {
        auto &e = diagnostics.events[(diagnostics.head + 15 - i) % 16];
        text += String(e.code) + "," + e.value + ",uptime_ms=" + e.uptime +
                ",epoch=" + e.epoch + "\n";
      }
      server.sendHeader("Cache-Control", "no-store");
      server.send(200, "text/plain; charset=utf-8", text);
    });
    server.on("/diagnostics/clear", HTTP_POST, [this] {
      if (!authorize())
        return;
      server.send(diagnostics.clear() ? 200 : 500, "text/plain",
                  "Diagnostics clear result");
    });
    server.on("/settings", HTTP_POST, [this] {
      if (!authorize())
        return;
      int l, c;
      if (!integer(server.arg("luminance"), 50, 100, l) ||
          !integer(server.arg("contrast"), -20, 20, c)) {
        server.send(400, "text/plain", "Invalid setting");
        return;
      }
      Preferences prefs;
      if (!prefs.begin("milestone_v5", false)) {
        server.send(500, "text/plain", "Storage unavailable");
        return;
      }
      const uint16_t tone = uint16_t(l) | (uint16_t(c + 20) << 8);
      bool ok = prefs.putUShort("tone", tone) == 2 &&
                prefs.getUShort("tone", 0) == tone;
      const bool logging = server.arg("env_log") == "1";
      ok = ok && prefs.putBool("env_log", logging) == 1;
      prefs.end();
      if (!ok) {
        server.send(500, "text/plain", "Save failed");
        return;
      }
      luminance = l;
      contrast = c;
      environmentLogging = logging;
      hardware->display.setTone(luminance, contrast);
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/core", HTTP_POST, [this] {
      if (!authorize())
        return;
      String date = server.arg("date"), message = server.arg("message"),
             label = server.arg("label");
      int y, m, d;
      bool valid = date.length() == 10 && date[4] == '-' && date[7] == '-' &&
                   integer(date.substring(0, 4), 2000, 2099, y) &&
                   integer(date.substring(5, 7), 1, 12, m) &&
                   integer(date.substring(8, 10), 1, 31, d) &&
                   MilestoneV5::validDate(y, m, d) && message.length() <= 144 &&
                   label.length() <= 64;
      uint16_t colors[6];
      for (unsigned i = 0; i < 6; ++i) {
        String c = server.arg(String("color") + i);
        uint32_t rgb = 0;
        bool colorValid = c.length() == 7 && c[0] == '#';
        for (unsigned j = 1; colorValid && j < 7; ++j) {
          char v = c[j];
          int n = v >= '0' && v <= '9'   ? v - '0'
                  : v >= 'a' && v <= 'f' ? v - 'a' + 10
                  : v >= 'A' && v <= 'F' ? v - 'A' + 10
                                         : -1;
          if (n < 0)
            colorValid = false;
          else
            rgb = (rgb << 4) | n;
        }
        valid = valid && colorValid;
        colors[i] = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) |
                    ((rgb >> 3) & 0x001F);
      }
      if (!valid) {
        server.send(400, "text/plain", "Invalid CORE settings");
        return;
      }
      V5CoreViews previous = *core;
      core->year = y;
      core->month = m;
      core->day = d;
      core->dateSet = true;
      core->message = message;
      core->label = label;
      memcpy(core->colors, colors, sizeof(colors));
      if (!core->save()) {
        *core = previous;
        server.send(500, "text/plain", "CORE save failed");
        return;
      }
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/display-options", HTTP_POST, [this] {
      if (!authorize())
        return;
      int speed, period, off, layout;
      uint8_t order[7], mask = 0, seen = 0;
      bool valid = integer(server.arg("speed"), 5, 80, speed) &&
                   integer(server.arg("cycle_seconds"), 3, 60, period) &&
                   integer(server.arg("screen_off"), 0, 1440, off) &&
                   integer(server.arg("now_layout"), 0, 3, layout);
      for (unsigned i = 0; i < 7; ++i) {
        int v = 0;
        valid = integer(server.arg(String("order") + i), 0, 6, v) && valid &&
                !(seen & (1U << v));
        seen |= 1U << v;
        order[i] = v;
        if (server.arg(String("enabled") + i) == "1")
          mask |= 1U << i;
      }
      if (!valid || !mask) {
        server.send(400, "text/plain", "Invalid display options/order");
        return;
      }
      V5CoreViews previous = *core;
      core->hour24 = server.arg("hour24") == "1";
      core->seconds = server.arg("seconds") == "1";
      core->scroll = server.arg("scroll") == "1";
      core->left = server.arg("left") == "1";
      core->ddayText = server.arg("dday_text") == "1";
      core->afterComplete = server.arg("after_complete") == "1";
      core->cycle = server.arg("cycle") == "1";
      core->burnin = server.arg("burnin") == "1";
      core->speed = speed;
      core->cycleSeconds = period;
      core->screenOffMinutes = off;
      core->nowLayout = layout;
      core->cycleMask = mask;
      memcpy(core->order, order, 7);
      if (!core->save()) {
        *core = previous;
        server.send(500, "text/plain", "Save failed");
        return;
      }
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/environment", HTTP_POST, [this] {
      if (!authorize())
        return;
      int interval, logInterval;
      float values[3] = {0, 0, 0};
      bool valid = integer(server.arg("interval"), 1, 60, interval) &&
                   integer(server.arg("log_interval"), 1, 1440, logInterval);
      const float limits[] = {20, 50};
      for (unsigned i = 0; i < 2; ++i) {
        String text = server.arg(String("offset") + i);
        char *end = nullptr;
        values[i] = strtof(text.c_str(), &end);
        valid = valid && !text.isEmpty() && text.length() <= 10 &&
                end == text.c_str() + text.length() && isfinite(values[i]) &&
                fabsf(values[i]) <= limits[i];
      }
      if (!valid) {
        server.send(400, "text/plain", "Invalid environment settings");
        return;
      }
      bool on = server.arg("enabled") == "1",
           fahrenheit = server.arg("fahrenheit") == "1";
      char data[128];
      snprintf(data, sizeof(data), "E1 %d %d %u %u %.2f %.2f %.2f", on,
               fahrenheit, unsigned(interval) * 1000,
               unsigned(logInterval) * 60000, values[0], values[1], values[2]);
      Preferences p;
      bool saved = p.begin("milestone_v5", false) &&
                   p.putString("env_config", data) == strlen(data) &&
                   p.getString("env_config", "") == data;
      p.end();
      if (!saved) {
        server.send(500, "text/plain", "Save failed");
        return;
      }
      memcpy(offsets, values, sizeof(offsets));
      hardware->environment.configure(on, fahrenheit, interval * 1000,
                                      logInterval * 60000, values[0], values[1],
                                      values[2]);
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/environment-limits", HTTP_POST, [this] {
      if (!authorize())
        return;
      unsigned mask = 0;
      float w[3], d[3];
      bool valid = true;
      for (unsigned i = 0; i < 2; ++i) {
        if (server.arg(String("show") + i) == "1")
          mask |= 1U << i;
        String a = server.arg(String("warn") + i),
               b = server.arg(String("danger") + i);
        char *ae, *be;
        w[i] = strtof(a.c_str(), &ae);
        d[i] = strtof(b.c_str(), &be);
        valid = valid && !a.isEmpty() && !b.isEmpty() &&
                ae == a.c_str() + a.length() && be == b.c_str() + b.length();
      }
      w[2] = 0;
      d[2] = 0;
      if (!valid || !validLimits(mask, w, d)) {
        server.send(400, "text/plain", "Invalid environment limits");
        return;
      }
      char data[128];
      snprintf(data, sizeof(data), "L1 %u %.1f %.1f %.1f %.1f %.1f %.1f", mask,
               w[0], d[0], w[1], d[1], w[2], d[2]);
      Preferences p;
      bool saved = p.begin("milestone_v5", false) &&
                   p.putString("env_limits", data) == strlen(data) &&
                   p.getString("env_limits", "") == data;
      p.end();
      if (!saved) {
        server.send(500, "text/plain", "Save failed");
        return;
      }
      hardware->environment.displayMask = mask & 3U;
      memcpy(hardware->environment.warning, w, sizeof(w));
      memcpy(hardware->environment.danger, d, sizeof(d));
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/media", HTTP_POST, [this] {
      if (!authorize())
        return;
      uint8_t value = (server.arg("mono") == "1" ? 1 : 0) |
                      (server.arg("repeat") == "1" ? 2 : 0) |
                      (server.arg("reverse") == "1" ? 4 : 0);
      Preferences p;
      bool ok = p.begin("milestone_v5", false) &&
                p.putUChar("media", value) == 1 &&
                p.getUChar("media", 255) == value;
      p.end();
      if (!ok) {
        server.send(500, "text/plain", "Save failed");
        return;
      }
      hardware->monochrome = value & 1;
      mediaRepeat = value & 2;
      hardware->reverseSort = value & 4;
      rescanRequested = true;
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/bundle", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (bundleBusy || bundleRequested || downloadBusy || downloadRequested) {
        server.send(409, "text/plain", "Bundle operation already pending");
        return;
      }
      if (!hardware->sdMounted) {
        server.send(409, "text/plain", "SD unavailable");
        return;
      }
      bundleRequested = true;
      bundleRequestedMs = millis();
      server.send(
          202, "text/plain; charset=utf-8",
          "15초 안에 기기의 OK 버튼으로 확인하세요. BACK으로 취소합니다.");
    });
    server.on("/download", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (bundleBusy || bundleRequested || downloadBusy || downloadRequested) {
        server.send(409, "text/plain", "Update already pending");
        return;
      }
      downloadVersion = server.arg("version");
      downloadRequested = true;
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/wifi", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (wifiPending || downloadBusy || bundleBusy) {
        server.send(409, "text/plain", "Network operation pending");
        return;
      }
      MilestoneV5::WifiCredentials next;
      String ssid = server.arg("ssid"), pass = server.arg("password"),
             username = server.arg("username"),
             identity = server.arg("identity");
      int security;
      if (ssid.length() > 32 || pass.length() > 63 || username.length() > 64 ||
          identity.length() > 64 ||
          !integer(server.arg("security"), 0, 1, security)) {
        server.send(400, "text/plain", "Invalid Wi-Fi");
        return;
      }
      ssid.toCharArray(next.ssid, sizeof(next.ssid));
      pass.toCharArray(next.password, sizeof(next.password));
      next.security = security;
      if (security) {
        username.toCharArray(next.username, sizeof(next.username));
        identity.toCharArray(next.identity, sizeof(next.identity));
      }
      if (!MilestoneV5::validWifiCredentials(next)) {
        server.send(400, "text/plain", "Invalid Wi-Fi");
        return;
      }
      wifi = next;
      wifiPending = true;
      wifiResult = "ZERO 연결 시험 대기 (실패하면 기존 설정 유지)";
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/system", HTTP_POST, [this] {
      if (!authorize())
        return;
      const char *keys[] = {"led_day",   "led_night", "night_start",
                            "night_end", "ntp",       "retry"};
      const int low[] = {0, 0, 0, 0, 0, 15},
                high[] = {255, 255, 1439, 1439, 604800, 86400};
      int v[6];
      bool valid = true;
      for (unsigned i = 0; i < 6; ++i)
        valid = integer(server.arg(keys[i]), low[i], high[i], v[i]) && valid;
      String password = server.arg("ap_password");
      if (password.length() > 63 ||
          (password.length() && password.length() < 8))
        valid = false;
      auto next = system;
      next.ledEnabled = server.arg("led") == "1";
      next.fixedAp = server.arg("fixed_ap") == "1";
      next.wifiSleep = server.arg("wifi_sleep") == "1";
      next.bootSync = server.arg("boot_sync") == "1";
      if (password.length())
        next.apPassword = password;
      if (server.arg("open_ap") == "1")
        next.apPassword = "";
      if (next.fixedAp && next.apPassword.isEmpty() &&
          server.arg("open_ap") != "1")
        valid = false;
      if (!valid) {
        server.send(400, "text/plain",
                    "Invalid system settings / open AP confirmation required");
        return;
      }
      next.ledDay = v[0];
      next.ledNight = v[1];
      next.nightStart = v[2];
      next.nightEnd = v[3];
      next.ntpSeconds = v[4];
      next.retrySeconds = v[5];
      if (!next.save()) {
        server.send(500, "text/plain", "System save failed");
        return;
      }
      system = next;
      systemPending = true;
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on("/rescan", HTTP_POST, [this] {
      if (!authorize())
        return;
      rescanRequested = true;
      server.sendHeader("Location", "/");
      server.send(303);
    });
    registerLegacyApi();
    server.onNotFound([this] {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302);
    });
  }

  bool open() {
    if (active)
      return true;
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    char secret[9], csrf[33];
    for (unsigned i = 0; i < 8; ++i)
      secret[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    secret[8] = 0;
    snprintf(csrf, sizeof(csrf), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    password = system.fixedAp ? system.apPassword : String(secret);
    token = csrf;
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP("MILESTONE-D1-SETUP",
                     password.isEmpty() ? nullptr : password.c_str())) {
      WiFi.mode(WIFI_OFF);
      password = "";
      return false;
    }
    dns.start(53, "*", WiFi.softAPIP());
    server.begin();
    active = true;
    openedMs = millis();
    touch();
    return true;
  }
  bool canYieldRadio(uint32_t now) const {
    return active && now - openedMs >= 60000 && now - lastActivity >= 30000 &&
           WiFi.softAPgetStationNum() == 0;
  }
  static bool validLimits(unsigned mask, const float *w, const float *d) {
    const float low[] = {-40, 0}, high[] = {85, 100};
    if (mask > 3)
      return false;
    for (unsigned i = 0; i < 2; ++i)
      if (!isfinite(w[i]) || !isfinite(d[i]) || w[i] < low[i] ||
          w[i] > high[i] || d[i] < low[i] || d[i] > high[i] ||
          d[i] < w[i])
        return false;
    return true;
  }
  bool applyImportedSystem(const uint8_t *snapshot) {
    if (!system.importLegacy(snapshot))
      return false;
    systemPending = true;
    Preferences p;
    uint16_t tone = system.luminance | (uint16_t(system.contrast + 20) << 8);
    bool ok = p.begin("milestone_v5", false) &&
              p.putUShort("tone", tone) == 2 &&
              p.putUChar("media", (system.monochrome ? 1 : 0) | 2) == 1;
    p.end();
    if (ok) {
      luminance = system.luminance;
      contrast = system.contrast;
      hardware->monochrome = system.monochrome;
      hardware->display.setTone(luminance, contrast);
    }
    return ok;
  }
  void close() {
    if (!active)
      return;
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    artworkPortal.close();
    active = false;
    password = "";
    token = "";
    closeRequested = false;
  }
  void service() {
    if (!active)
      return;
    dns.processNextRequest();
    server.handleClient();
    if (resetRequestedMs && millis() - resetRequestedMs >= 500) {
      media.clear();
      clearArtworkStorage();
      close();
      nvs_flash_erase();
      ESP.restart();
      return;
    }
    if (closeRequested) {
      close();
      return;
    }
    if (millis() - lastActivity >= 600000)
      close();
  }

private:
  V5Hardware *hardware = nullptr;
  V5CoreViews *core = nullptr;
  V5Artwork *artwork = nullptr;
  const MilestoneV5::NowMetadata *now = nullptr;
  const uint32_t *zeroLastLinkMs = nullptr;
  WebServer server{80};
  DNSServer dns;
  V5ArtworkPortal artworkPortal;
  String token;
  uint32_t lastActivity = 0, openedMs = 0;
  bool wifiScanRunning = false;
  bool mediaUploadRejected = false;
  uint32_t resetRequestedMs = 0;
  void touch() { lastActivity = millis(); }
  static String escape(String value) {
    value.replace("&", "&amp;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    value.replace("\"", "&quot;");
    value.replace("'", "&#39;");
    return value;
  }
  static String jsonEscape(String value) {
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    value.replace("\t", "\\t");
    return value;
  }
  static const char *profileId(MilestoneV5::Profile value) {
    switch (value) {
    case MilestoneV5::Profile::kMedia:
      return "media";
    case MilestoneV5::Profile::kNow:
      return "now";
    default:
      return "core";
    }
  }
  static String rgbHex(uint16_t color) {
    char value[8];
    snprintf(value, sizeof(value), "#%02X%02X%02X",
             ((color >> 11) & 31) * 255 / 31,
             ((color >> 5) & 63) * 255 / 63, (color & 31) * 255 / 31);
    return value;
  }
  static bool parseColor(const String &text, uint16_t &color) {
    if (text.length() != 7 || text[0] != '#')
      return false;
    uint32_t rgb = 0;
    for (unsigned i = 1; i < 7; ++i) {
      const char c = text[i];
      const int value = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
      if (value < 0)
        return false;
      rgb = (rgb << 4) | value;
    }
    color = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) |
            ((rgb >> 3) & 0x001F);
    return true;
  }
  void sendJson(int status, const String &body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(status, "application/json; charset=utf-8", body);
  }
  void registerLegacyApi() {
    server.on("/api/status", HTTP_GET, [this] {
      touch();
      const bool connected = WiFi.status() == WL_CONNECTED;
      const bool zeroOnline = zeroLastLinkMs && *zeroLastLinkMs &&
                              millis() - *zeroLastLinkMs <= 5000;
      const bool bleConnected = zeroOnline && now && now->connected;
      const bool amsReady = bleConnected && now->ready;
      const char *id = profileId(profile);
      String body = "{\"firmware\":\"" +
                    String(MilestoneV5::FIRMWARE_VERSION) +
                    "\",\"profile\":\"" + String(id) + "\",\"state\":\"" +
                    (active ? "설정 AP" : "정상 동작") + "\"";
      body += ",\"reset_reason\":\"ESP reset\",\"reset_reason_code\":" +
              String(int(esp_reset_reason()));
      body += ",\"uptime_sec\":" + String(millis() / 1000UL) +
              ",\"cpu_mhz\":" + String(getCpuFrequencyMhz());
      body += ",\"temperature_c\":" + String(temperatureRead(), 1) +
              ",\"heap_free\":" + String(ESP.getFreeHeap()) +
              ",\"heap_total\":" + String(ESP.getHeapSize()) +
              ",\"heap_min\":" + String(ESP.getMinFreeHeap()) +
              ",\"heap_largest\":" + String(ESP.getMaxAllocHeap());
      body += ",\"stack_free\":" +
              String(uxTaskGetStackHighWaterMark(nullptr)) +
              ",\"flash_total\":" + String(ESP.getFlashChipSize()) +
              ",\"sketch_size\":" + String(ESP.getSketchSize()) +
              ",\"ota_free\":" + String(ESP.getFreeSketchSpace());
      body += ",\"nvs_ready\":true,\"nvs_free_entries\":0";
      body += ",\"wifi\":\"" +
              jsonEscape(connected ? WiFi.SSID() : String("설정 AP")) +
              "\",\"ip\":\"" +
              jsonEscape(connected ? WiFi.localIP().toString()
                                   : WiFi.softAPIP().toString()) +
              "\"";
      body += ",\"time_valid\":" +
              String(time(nullptr) >= 1704067200 ? "true" : "false") +
              ",\"last_sync\":\"-\",\"ntp_active\":" +
              String(timeSyncRequested ? "true" : "false") +
              ",\"ntp_failed\":false,\"time_sync_pending\":" +
              String(timeSyncRequested ? "true" : "false") +
              ",\"time_sync_success\":" +
              String(timeSyncSuccess ? "true" : "false");
      body += ",\"wifi_test\":\"" +
              String(wifiPending ? "testing"
                                 : wifiResult.indexOf("완료") >= 0 ? "success"
                                                                    : "idle") +
              "\",\"wifi_error\":\"" + jsonEscape(wifiResult) + "\"";
      body += ",\"bluetooth_enabled\":true,\"bluetooth_active\":" +
              String(zeroOnline ? "true" : "false") +
              ",\"bluetooth_connected\":" +
              String(bleConnected ? "true" : "false") +
              ",\"bluetooth_ams_ready\":" +
              String(amsReady ? "true" : "false") +
              ",\"bluetooth_advertising\":" +
              String(zeroOnline && !bleConnected ? "true" : "false") +
              ",\"bluetooth_stage\":\"" +
              String(!zeroOnline ? "error"
                     : amsReady   ? "ready"
                     : bleConnected ? "discovering"
                                    : "advertising") +
              "\"";
      body += ",\"media_supported\":true,\"general_views_supported\":true";
      body += ",\"latest_firmware\":\"" +
              String(MilestoneV5::FIRMWARE_VERSION) +
              "\",\"latest_profile\":\"" + String(id) +
              "\",\"update_state\":\"" +
              String(downloadBusy ? "checking" : downloadReady ? "available"
                                                              : "current") +
              "\",\"update_available\":" +
              String(downloadReady ? "true" : "false") +
              ",\"update_install_ready\":" +
              String(downloadReady ? "true" : "false") +
              ",\"update_check_pending\":" +
              String(downloadBusy ? "true" : "false") + "}"
              ;
      sendJson(200, body);
    });
    server.on("/api/diagnostics", HTTP_GET, [this] {
      String body = "{\"last_boot\":\"ESP reset\",\"boot_validated\":true,"
                    "\"last_validated_uptime_sec\":" +
                    String(millis() / 1000UL) +
                    ",\"max_temperature_c\":" + String(temperatureRead(), 1) +
                    ",\"last_ota_result\":\"-\",\"rollback_last\":\"-\","
                    "\"rollback_reason\":\"\",\"history_count\":" +
                    String(diagnostics.count) + ",\"events\":[";
      for (unsigned i = 0; i < diagnostics.count; ++i) {
        const auto &event =
            diagnostics.events[(diagnostics.head + 15 - i) % 16];
        if (i)
          body += ',';
        body += "{\"event\":\"event_" + String(event.code) +
                "\",\"detail\":\"value " + String(event.value) +
                "\",\"uptime_sec\":" + String(event.uptime / 1000UL) + "}";
      }
      body += "]}";
      sendJson(200, body);
    });
    server.on("/api/diagnostics/clear", HTTP_POST, [this] {
      if (!authorize())
        return;
      sendJson(diagnostics.clear() ? 200 : 500,
               diagnostics.count ? "{\"error\":\"삭제 실패\"}"
                                 : "{\"ok\":true}");
    });
    server.on("/api/config", HTTP_GET, [this] { sendLegacyConfig(); });
    server.on("/api/config", HTTP_POST, [this] { saveLegacyConfig(); });
    server.on("/api/radio-config", HTTP_GET, [this] {
      sendJson(200, "{\"fixed_ap\":" +
                        String(system.fixedAp ? "true" : "false") +
                        ",\"ap_password_set\":" +
                        String(!system.apPassword.isEmpty() ? "true" : "false") +
                        ",\"bluetooth_now_playing\":true,"
                        "\"bluetooth_supported\":true,"
                        "\"bluetooth_configurable\":false}");
    });
    server.on("/api/radio-config", HTTP_POST, [this] {
      if (!authorize())
        return;
      auto next = system;
      next.fixedAp = server.arg("fixed_ap") == "1";
      const String pass = server.arg("ap_password");
      if (pass.length() > 63 || (pass.length() && pass.length() < 8)) {
        sendJson(400, "{\"error\":\"AP 비밀번호는 비워 두거나 8~63자로 입력하세요.\"}");
        return;
      }
      next.apPassword = pass;
      if (!next.save()) {
        sendJson(500, "{\"error\":\"AP 설정 저장 실패\"}");
        return;
      }
      system = next;
      systemPending = true;
      sendJson(200, "{\"ok\":true}");
    });
    server.on("/api/now-config", HTTP_GET, [this] {
      static const char *names[] = {"곡명만", "곡명 + 아티스트",
                                    "곡명 + 앨범 표지", "앨범 표지 중심"};
      unsigned layout = min(unsigned(core->nowLayout), 3U);
      sendJson(200, "{\"layout\":" + String(layout == 2 ? 3 : layout == 3 ? 4 : layout) +
                        ",\"layout_name\":\"" + names[layout] +
                        "\",\"artwork_available\":" +
                        String(artwork && artwork->visible ? "true" : "false") +
                        ",\"artwork_status\":\"" +
                        jsonEscape(!artwork ? String("대기")
                                   : artwork->visible ? String("표시 중")
                                   : artwork->stage ? String("다운로드 중")
                                   : artwork->lastError.isEmpty()
                                       ? String("대기")
                                       : artwork->lastError) + "\"}");
    });
    server.on("/api/now-config", HTTP_POST, [this] {
      if (!authorize())
        return;
      int layout;
      if (!integer(server.arg("layout"), 0, 4, layout) || layout == 2) {
        sendJson(400, "{\"error\":\"NOW 표시 구성이 올바르지 않습니다.\"}");
        return;
      }
      core->nowLayout = layout == 3 ? 2 : layout == 4 ? 3 : layout;
      const bool saved = core->save();
      sendJson(saved ? 200 : 500,
               saved ? "{\"ok\":true}"
                     : "{\"error\":\"저장 실패\"}");
    });
    server.on("/api/wifi/scan", HTTP_GET, [this] { handleWifiScan(); });
    server.on("/api/wifi/test", HTTP_POST, [this] { handleWifiTest(); });
    server.on("/api/wifi/delete", HTTP_POST, [this] {
      if (!authorize())
        return;
      String ssid = server.arg("ssid");
      MilestoneV5::WifiStore store;
      const bool removed = !ssid.isEmpty() && store.remove(ssid.c_str());
      sendJson(removed ? 200 : 400,
               removed ? "{\"ok\":true}"
                       : "{\"error\":\"저장된 Wi-Fi를 삭제하지 못했습니다.\"}");
    });
    server.on("/api/time/sync", HTTP_POST, [this] {
      if (!authorize())
        return;
      timeSyncRequested = true;
      timeSyncSuccess = false;
      sendJson(202, "{\"ok\":true,\"state\":\"connecting\"}");
    });
    server.on("/api/profile/switch", HTTP_POST, [this] {
      if (!authorize())
        return;
      String target = server.arg("profile");
      if (target == "core")
        requestedProfile = MilestoneV5::Profile::kCore;
      else if (target == "media")
        requestedProfile = MilestoneV5::Profile::kMedia;
      else if (target == "now")
        requestedProfile = MilestoneV5::Profile::kNow;
      else {
        sendJson(400, "{\"error\":\"프로필이 올바르지 않습니다.\"}");
        return;
      }
      profilePending = true;
      sendJson(202, "{\"ok\":true,\"state\":\"switching\"}");
    });
    server.on("/api/media/status", HTTP_GET, [this] {
      sendJson(200, "{\"ready\":" + String(media.ready ? "true" : "false") +
                        ",\"item_count\":" + String(media.catalog.count) +
                        ",\"max_items\":" + String(V5LegacyMedia::kMaxItems) +
                        ",\"media_used_bytes\":" + String(media.usedBytes()) +
                        ",\"media_limit_bytes\":8388608,\"psram\":true}");
    });
    server.on("/api/media/list", HTTP_GET, [this] {
      String body = "{\"items\":[";
      for (unsigned i = 0; i < media.catalog.count; ++i) {
        const auto &entry = media.catalog.entries[i];
        if (i)
          body += ',';
        body += "{\"id\":" + String(entry.id) + ",\"name\":\"" +
                jsonEscape(entry.name) + "\",\"display_seconds\":" +
                String(entry.displaySeconds) + ",\"enabled\":" +
                String(entry.flags & V5LegacyMedia::kEnabled ? "true" : "false") +
                ",\"animated\":" +
                String(entry.flags & V5LegacyMedia::kAnimated ? "true" : "false") +
                ",\"frames\":" + String(entry.frames) +
                ",\"size\":" + String(entry.size) +
                ",\"duration_ms\":" + String(entry.duration) + "}";
      }
      body += "]}";
      sendJson(200, body);
    });
    server.on(
        "/api/media/upload", HTTP_POST,
        [this] {
          if (!localRequest())
            return server.send(403, "text/plain", "Forbidden");
          V5LegacyMedia::Entry entry;
          const bool saved = !mediaUploadRejected && media.finishUpload(entry);
          mediaUploadRejected = false;
          if (!saved) {
            sendJson(400, "{\"error\":\"" + jsonEscape(media.error) + "\"}");
            return;
          }
          sendJson(200, "{\"ok\":true,\"id\":" + String(entry.id) +
                            ",\"frames\":" + String(entry.frames) + "}");
        },
        [this] {
          HTTPUpload &part = server.upload();
          if (part.status == UPLOAD_FILE_START) {
            mediaUploadRejected = !localRequest();
            String sizeText = server.arg("size"), displayText = server.arg("display");
            size_t expected = 0;
            bool valid = !sizeText.isEmpty() && sizeText.length() <= 9;
            for (unsigned i = 0; i < sizeText.length(); ++i) {
              valid = valid && sizeText[i] >= '0' && sizeText[i] <= '9';
              expected = expected * 10 + (sizeText[i] - '0');
            }
            int display = 0;
            valid = integer(displayText, 3, 60, display) && valid;
            if (!valid ||
                !media.beginUpload(server.arg("name"), expected, display))
              mediaUploadRejected = true;
          } else if (part.status == UPLOAD_FILE_WRITE) {
            if (!mediaUploadRejected && !media.writeUpload(part.buf, part.currentSize))
              mediaUploadRejected = true;
          } else if (part.status == UPLOAD_FILE_ABORTED) {
            media.abortUpload();
            mediaUploadRejected = true;
          }
        });
    server.on("/api/media/update", HTTP_POST, [this] {
      if (!authorize())
        return;
      uint32_t id;
      int display;
      if (!unsignedInteger(server.arg("id"), id) ||
          !integer(server.arg("display"), 3, 60, display) ||
          !media.update(id, server.arg("name"), display,
                        server.arg("enabled") == "1")) {
        sendJson(400, "{\"error\":\"미디어 설정 저장 실패\"}");
        return;
      }
      sendJson(200, "{\"ok\":true}");
    });
    server.on("/api/media/order", HTTP_POST, [this] {
      if (!authorize())
        return;
      uint32_t id;
      const String direction = server.arg("direction");
      if (!unsignedInteger(server.arg("id"), id) ||
          (direction != "up" && direction != "down") ||
          !media.move(id, direction == "up")) {
        sendJson(400, "{\"error\":\"미디어 순서 변경 실패\"}");
        return;
      }
      sendJson(200, "{\"ok\":true}");
    });
    server.on("/api/media/delete", HTTP_POST, [this] {
      if (!authorize())
        return;
      uint32_t id;
      if (!unsignedInteger(server.arg("id"), id) || !media.remove(id)) {
        sendJson(400, "{\"error\":\"미디어 삭제 실패\",\"stage\":\"commit\"}");
        return;
      }
      sendJson(200, "{\"ok\":true,\"stage\":\"committed\"}");
    });
    server.on("/api/media/clear", HTTP_POST, [this] {
      if (!authorize())
        return;
      const bool cleared = server.arg("confirm") == "MEDIA" && media.clear();
      sendJson(cleared ? 200 : 400,
               cleared ? "{\"ok\":true}"
                       : "{\"error\":\"미디어 전체 삭제 실패\"}");
    });
    server.on("/api/media/repair", HTTP_POST, [this] {
      if (!authorize())
        return;
      const bool repaired = server.arg("confirm") == "REPAIR" && media.repair();
      sendJson(repaired ? 200 : 400,
               repaired ? "{\"ok\":true}"
                        : "{\"error\":\"미디어 저장소 복구 실패\"}");
    });
    server.on("/api/portal/close", HTTP_POST, [this] {
      if (!authorize())
        return;
      closeRequested = true;
      sendJson(200, "{\"ok\":true}");
    });
    server.on("/api/update/check", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (downloadBusy || downloadRequested || bundleBusy) {
        sendJson(409, "{\"error\":\"업데이트 작업이 이미 진행 중입니다.\"}");
        return;
      }
      downloadVersion = "latest";
      downloadRequested = true;
      downloadReady = false;
      sendJson(202, "{\"ok\":true,\"state\":\"checking\"}");
    });
    server.on("/api/update/install", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (!downloadReady || bundleBusy || bundleRequested) {
        sendJson(409, "{\"error\":\"먼저 서명된 업데이트 묶음을 확인하세요.\"}");
        return;
      }
      bundleRequested = true;
      bundleRequestedMs = millis();
      sendJson(202, "{\"ok\":true,\"state\":\"physical-confirmation\"}");
    });
    server.on("/api/settings/reset", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (server.arg("confirm") != "DEFAULTS") {
        sendJson(400, "{\"error\":\"확인값이 올바르지 않습니다.\"}");
        return;
      }
      V5CoreViews defaults;
      MilestoneV5::SystemSettings defaultsSystem;
      if (!defaults.save() || !defaultsSystem.save()) {
        sendJson(500, "{\"error\":\"기본값 저장 실패\"}");
        return;
      }
      *core = defaults;
      system = defaultsSystem;
      systemPending = true;
      luminance = system.luminance;
      contrast = system.contrast;
      hardware->display.setTone(luminance, contrast);
      sendJson(200, "{\"ok\":true}");
    });
    server.on("/api/reset", HTTP_POST, [this] {
      if (!authorize())
        return;
      if (server.arg("confirm") != "RESET") {
        sendJson(400, "{\"error\":\"확인값이 올바르지 않습니다.\"}");
        return;
      }
      resetRequestedMs = millis();
      sendJson(200, "{\"ok\":true}");
    });
  }
  bool localRequest() {
    return active && server.client().localIP() == WiFi.softAPIP();
  }
  static bool unsignedInteger(const String &text, uint32_t &value) {
    if (text.isEmpty() || text.length() > 10)
      return false;
    uint64_t parsed = 0;
    for (unsigned i = 0; i < text.length(); ++i) {
      if (text[i] < '0' || text[i] > '9')
        return false;
      parsed = parsed * 10 + (text[i] - '0');
      if (parsed > UINT32_MAX)
        return false;
    }
    value = parsed;
    return true;
  }
  static void clearArtworkStorage() {
    for (unsigned removed = 0; removed < 4096; ++removed) {
      File directory = SD.open("/now/art-cache");
      if (!directory)
        break;
      File file = directory.openNextFile();
      if (!file) {
        directory.close();
        break;
      }
      String name = file.name();
      const bool isFile = !file.isDirectory();
      file.close();
      directory.close();
      if (!isFile)
        break;
      if (!name.startsWith("/"))
        name = String("/now/art-cache/") + name;
      if (!SD.remove(name))
        break;
    }
    SD.remove("/now/art-index-a");
    SD.remove("/now/art-index-b");
    SD.remove("/now/art-index.tmp");
  }
  void sendLegacyConfig() {
    touch();
    char target[16];
    snprintf(target, sizeof(target), "%04u-%02u-%02u", core->year,
             core->month, core->day);
    const unsigned mode = core->cycle ? 6U : core->view == 6 ? 7U : core->view;
    String order;
    for (unsigned i = 0; i < 7; ++i) {
      if (i)
        order += ',';
      order += String(core->order[i]);
    }
    order += ",7";
    MilestoneV5::WifiStore store;
    MilestoneV5::WifiCredentials networks[MilestoneV5::kWifiMaxNetworks];
    uint8_t count = 0;
    store.loadAll(networks, count);
    String body = "{\"title\":\"" + jsonEscape(core->label) +
                  "\",\"target\":\"" + target +
                  "\",\"message\":\"" + jsonEscape(core->message) +
                  "\",\"mode\":" + String(mode) +
                  ",\"cycle_mask\":" + String(core->cycleMask) +
                  ",\"cycle_order\":\"" + order +
                  "\",\"cycle_interval\":" + String(core->cycleSeconds);
    body += ",\"dday_style\":" + String(core->ddayText ? 1 : 0) +
            ",\"after_mode\":" + String(core->afterComplete ? 1 : 0) +
            ",\"msg_align\":" + String(core->left ? 1 : 0) +
            ",\"msg_scroll\":" + String(core->scroll ? "true" : "false") +
            ",\"scroll_speed\":" + String(core->speed) +
            ",\"hour24\":" + String(core->hour24 ? 1 : 0) +
            ",\"show_seconds\":" + String(core->seconds ? 1 : 0);
    body += ",\"show_temp\":false,\"boot_sync\":" +
            String(system.bootSync ? "true" : "false") +
            ",\"wifi_sleep\":" + String(system.wifiSleep ? "true" : "false") +
            ",\"burnin\":" + String(core->burnin ? "true" : "false") +
            ",\"led_enabled\":" + String(system.ledEnabled ? "true" : "false");
    body += ",\"ntp_period\":" + String(system.ntpSeconds) +
            ",\"dday_period\":0,\"retry_period\":" +
            String(system.retrySeconds) + ",\"led_brightness\":" +
            String(system.ledDay) + ",\"led_night_level\":" +
            String(system.ledNight) + ",\"night_start\":" +
            String(system.nightStart) + ",\"night_end\":" +
            String(system.nightEnd) + ",\"screen_off\":" +
            String(core->screenOffMinutes) + ",\"display_luminance\":" +
            String(system.luminance) + ",\"display_contrast\":" +
            String(system.contrast);
    body += ",\"color_title\":\"" + rgbHex(core->colors[4]) +
            "\",\"color_date\":\"" + rgbHex(core->colors[1]) +
            "\",\"color_time\":\"" + rgbHex(core->colors[0]) +
            "\",\"color_dday\":\"" + rgbHex(core->colors[3]) +
            "\",\"color_message\":\"" + rgbHex(core->colors[2]) +
            "\",\"color_info\":\"" + rgbHex(core->colors[5]) + "\"";
    body += ",\"media_monochrome\":" +
            String(system.monochrome ? "true" : "false") +
            ",\"enterprise_supported\":true";
    if (count) {
      body += ",\"wifi_ssid\":\"" + jsonEscape(networks[0].ssid) +
              "\",\"wifi_security\":\"" +
              String(networks[0].security ? "enterprise_peap" : "personal") +
              "\",\"wifi_username\":\"" +
              jsonEscape(networks[0].username) +
              "\",\"wifi_identity\":\"" +
              jsonEscape(networks[0].identity) + "\"";
    } else {
      body += ",\"wifi_ssid\":\"\",\"wifi_security\":\"personal\","
              "\"wifi_username\":\"\",\"wifi_identity\":\"\"";
    }
    body += ",\"saved_networks\":[";
    for (unsigned i = 0; i < count; ++i) {
      if (i)
        body += ',';
      body += "{\"ssid\":\"" + jsonEscape(networks[i].ssid) +
              "\",\"security\":\"" +
              String(networks[i].security ? "enterprise_peap" : "personal") +
              "\",\"preferred\":" + String(i ? "false" : "true") + "}";
    }
    body += "]}";
    sendJson(200, body);
  }
  void saveLegacyConfig() {
    if (!authorize())
      return;
    String target = server.arg("target"), label = server.arg("title"),
           messageText = server.arg("message");
    int year, month, day, mode, cycleSeconds, speed, hour24, seconds,
        ddayStyle, afterMode, alignment, ntp, retry, ledDay, ledNight,
        nightStart, nightEnd, screenOff, displayLuminance, displayContrast;
    bool valid = target.length() == 10 && target[4] == '-' && target[7] == '-' &&
                 integer(target.substring(0, 4), 2000, 2099, year) &&
                 integer(target.substring(5, 7), 1, 12, month) &&
                 integer(target.substring(8, 10), 1, 31, day) &&
                 MilestoneV5::validDate(year, month, day) &&
                 label.length() <= 64 && messageText.length() <= 144 &&
                 integer(server.arg("mode"), 0, 8, mode) &&
                 integer(server.arg("cycle_interval"), 0, 60, cycleSeconds) &&
                 (cycleSeconds == 0 || cycleSeconds >= 3) &&
                 integer(server.arg("scroll_speed"), 5, 80, speed) &&
                 integer(server.arg("hour24"), 0, 1, hour24) &&
                 integer(server.arg("show_seconds"), 0, 1, seconds) &&
                 integer(server.arg("dday_style"), 0, 1, ddayStyle) &&
                 integer(server.arg("after_mode"), 0, 1, afterMode) &&
                 integer(server.arg("msg_align"), 0, 1, alignment) &&
                 integer(server.arg("ntp_period"), 0, 604800, ntp) &&
                 integer(server.arg("retry_period"), 15, 86400, retry) &&
                 integer(server.arg("led_brightness"), 1, 64, ledDay) &&
                 integer(server.arg("led_night_level"), 1, 32, ledNight) &&
                 integer(server.arg("night_start"), 0, 1439, nightStart) &&
                 integer(server.arg("night_end"), 0, 1439, nightEnd) &&
                 integer(server.arg("screen_off"), 0, 1440, screenOff) &&
                 integer(server.arg("display_luminance"), 50, 100,
                         displayLuminance) &&
                 integer(server.arg("display_contrast"), -20, 20,
                         displayContrast);
    uint16_t colors[6];
    const char *keys[] = {"color_time", "color_date", "color_message",
                          "color_dday", "color_title", "color_info"};
    for (unsigned i = 0; i < 6; ++i)
      valid = parseColor(server.arg(keys[i]), colors[i]) && valid;
    uint8_t order[7];
    valid = parseLegacyOrder(server.arg("cycle_order"), order) && valid;
    int cycleMask;
    valid = integer(server.arg("cycle_mask"), 1, 255, cycleMask) && valid;
    if (!valid) {
      sendJson(400, "{\"error\":\"설정값을 확인하세요.\"}");
      return;
    }
    V5CoreViews previousCore = *core;
    auto previousSystem = system;
    core->year = year;
    core->month = month;
    core->day = day;
    core->dateSet = true;
    core->label = label;
    core->message = messageText;
    core->cycle = mode == 6;
    if (mode <= 5)
      core->view = mode;
    else if (mode == 7)
      core->view = 6;
    else if (mode == 8) {
      requestedProfile = MilestoneV5::Profile::kMedia;
      profilePending = true;
    }
    core->cycleSeconds = cycleSeconds ? cycleSeconds : 8;
    core->speed = speed;
    core->hour24 = hour24;
    core->seconds = seconds;
    core->ddayText = ddayStyle;
    core->afterComplete = afterMode;
    core->left = alignment;
    core->scroll = server.arg("msg_scroll") == "1";
    core->burnin = server.arg("burnin") == "1";
    core->cycleMask = cycleMask & 0x7F;
    memcpy(core->order, order, sizeof(order));
    core->screenOffMinutes = screenOff;
    memcpy(core->colors, colors, sizeof(colors));
    system.luminance = displayLuminance;
    system.contrast = displayContrast;
    system.monochrome = server.arg("media_monochrome") == "1";
    system.ledEnabled = server.arg("led_enabled") == "1";
    system.ledDay = ledDay;
    system.ledNight = ledNight;
    system.nightStart = nightStart;
    system.nightEnd = nightEnd;
    system.ntpSeconds = ntp;
    system.retrySeconds = retry;
    system.wifiSleep = server.arg("wifi_sleep") == "1";
    system.bootSync = server.arg("boot_sync") == "1";
    if (!core->save() || !system.save()) {
      *core = previousCore;
      system = previousSystem;
      sendJson(500, "{\"error\":\"설정 저장에 실패했습니다.\"}");
      return;
    }
    luminance = system.luminance;
    contrast = system.contrast;
    hardware->monochrome = system.monochrome;
    hardware->display.setTone(luminance, contrast);
    systemPending = true;
    sendJson(200, "{\"ok\":true}");
  }
  static bool parseLegacyOrder(const String &value, uint8_t *order) {
    bool seen[7]{};
    bool mediaSeen = false;
    unsigned count = 0, start = 0;
    while (start <= value.length()) {
      int comma = value.indexOf(',', start);
      String part = comma < 0 ? value.substring(start)
                              : value.substring(start, comma);
      part.trim();
      if (part.length() == 1 && part[0] >= '0' && part[0] <= '6') {
        unsigned item = part[0] - '0';
        if (seen[item] || count >= 7)
          return false;
        seen[item] = true;
        order[count++] = item;
      } else if (part == "7" && !mediaSeen)
        mediaSeen = true;
      else
        return false;
      if (comma < 0)
        break;
      start = comma + 1;
    }
    return count == 7 && mediaSeen;
  }
  void handleWifiScan() {
    if (!authorize())
      return;
    int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_FAILED) {
      WiFi.mode(WIFI_AP_STA);
      if (WiFi.scanNetworks(true, true, false, 120) == WIFI_SCAN_FAILED) {
        sendJson(500, "{\"error\":\"Wi-Fi 검색을 시작하지 못했습니다.\"}");
        return;
      }
      wifiScanRunning = true;
      sendJson(202, "{\"state\":\"scanning\"}");
      return;
    }
    if (result == WIFI_SCAN_RUNNING) {
      sendJson(202, "{\"state\":\"scanning\"}");
      return;
    }
    String body = "{\"state\":\"done\",\"networks\":[";
    bool first = true;
    for (int i = 0; i < result; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.isEmpty())
        continue;
      bool duplicate = false;
      for (int j = 0; j < i; ++j)
        if (WiFi.SSID(j) == ssid)
          duplicate = true;
      if (duplicate)
        continue;
      const wifi_auth_mode_t auth = WiFi.encryptionType(i);
      const bool caRequired = auth == WIFI_AUTH_WPA3_ENTERPRISE ||
                              auth == WIFI_AUTH_WPA2_WPA3_ENTERPRISE ||
                              auth == WIFI_AUTH_WPA3_ENT_192;
      const bool enterprise = auth == WIFI_AUTH_WPA2_ENTERPRISE ||
                              auth == WIFI_AUTH_WPA_ENTERPRISE || caRequired;
      if (!first)
        body += ',';
      first = false;
      body += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" +
              String(WiFi.RSSI(i)) + ",\"open\":" +
              String(auth == WIFI_AUTH_OPEN ? "true" : "false") +
              ",\"enterprise\":" +
              String(enterprise ? "true" : "false") +
              ",\"enterprise_ca_required\":" +
              String(caRequired ? "true" : "false") + "}";
    }
    body += "]}";
    WiFi.scanDelete();
    wifiScanRunning = false;
    WiFi.mode(WIFI_AP);
    sendJson(200, body);
  }
  void handleWifiTest() {
    if (!authorize())
      return;
    if (wifiPending || downloadBusy || bundleBusy) {
      sendJson(409, "{\"error\":\"다른 네트워크 작업이 진행 중입니다.\"}");
      return;
    }
    MilestoneV5::WifiCredentials next;
    String ssid = server.arg("ssid"), pass = server.arg("pass"),
           security = server.arg("security"),
           username = server.arg("username"), identity = server.arg("identity");
    if (ssid.length() > 32 || pass.length() > 63 || username.length() > 64 ||
        identity.length() > 64 ||
        (security != "personal" && security != "enterprise_peap")) {
      sendJson(400, "{\"error\":\"Wi-Fi 입력값을 확인하세요.\"}");
      return;
    }
    ssid.toCharArray(next.ssid, sizeof(next.ssid));
    pass.toCharArray(next.password, sizeof(next.password));
    next.security = security == "enterprise_peap" ? 1 : 0;
    if (next.security) {
      username.toCharArray(next.username, sizeof(next.username));
      identity.toCharArray(next.identity, sizeof(next.identity));
    }
    if (!MilestoneV5::validWifiCredentials(next)) {
      sendJson(400, "{\"error\":\"비밀번호 또는 Enterprise 계정값을 확인하세요.\"}");
      return;
    }
    if (wifiScanRunning) {
      WiFi.scanDelete();
      wifiScanRunning = false;
    }
    wifi = next;
    wifiPending = true;
    wifiResult = "연결 시험 중";
    sendJson(202, "{\"ok\":true,\"state\":\"testing\"}");
  }
  bool authorize() {
    if (!localRequest()) {
      server.send(403, "text/plain", "Forbidden");
      return false;
    }
    touch();
    return true;
  }
  static bool integer(const String &s, int low, int high, int &out) {
    if (s.isEmpty() || s.length() > 10)
      return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.length())
      return false;
    for (; i < s.length(); ++i)
      if (s[i] < '0' || s[i] > '9')
        return false;
    out = s.toInt();
    return out >= low && out <= high;
  }
};
