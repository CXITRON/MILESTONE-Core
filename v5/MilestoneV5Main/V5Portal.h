#pragma once
#include "V5ArtworkPortal.h"
#include "V5CoreViews.h"
#include "V5Hardware.h"
#include <DNSServer.h>
#include <MilestoneV5Diagnostics.h>
#include <MilestoneV5Features.h>
#include <MilestoneV5SystemSettings.h>
#include <MilestoneV5WifiStore.h>
#include <WebServer.h>
#include <WiFi.h>

// Opened only through a local BOOT press; AP password is freshly generated.
// Every write requires an unpredictable token, including from another website.
class V5Portal {
public:
  bool active = false;
  MilestoneV5::SystemSettings system;
  bool systemPending = false;
  MilestoneV5::Diagnostics diagnostics;
  void note(uint32_t code, uint32_t value = 0) {
    diagnostics.note(code, value, millis(), time(nullptr));
  }
  String password;
  bool rescanRequested = false;
  uint8_t luminance = 92;
  int8_t contrast = 8;
  bool environmentLogging = false;
  bool wifiPending = false, wifiReplicate = false;
  MilestoneV5::WifiCredentials wifi;
  String wifiResult = "";
  float offsets[3] = {0, 0, 0};
  bool mediaRepeat = true;
  bool bundleRequested = false, bundleBusy = false;
  uint32_t bundleRequestedMs = 0;
  String bundleStatus = "idle", bundleError;
  bool downloadRequested = false, downloadBusy = false;
  String downloadVersion = "latest", downloadStatus,
         bundleSource = "/firmware/incoming";

  void begin(V5Hardware &h, V5CoreViews &views, V5Artwork &art) {
    hardware = &h;
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
    server.on("/", HTTP_GET, [this] {
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
    server.onNotFound([this] {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302);
    });
  }

  bool open() {
    if (active)
      return true;
    char secret[9], csrf[33];
    snprintf(secret, sizeof(secret), "%08lx", (unsigned long)esp_random());
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
  }
  void service() {
    if (!active)
      return;
    dns.processNextRequest();
    server.handleClient();
    if (millis() - lastActivity >= 600000)
      close();
  }

private:
  V5Hardware *hardware = nullptr;
  V5CoreViews *core = nullptr;
  WebServer server{80};
  DNSServer dns;
  V5ArtworkPortal artworkPortal;
  String token;
  uint32_t lastActivity = 0, openedMs = 0;
  void touch() { lastActivity = millis(); }
  static String escape(String value) {
    value.replace("&", "&amp;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    value.replace("\"", "&quot;");
    value.replace("'", "&#39;");
    return value;
  }
  bool authorize() {
    if (server.arg("token") != token || token.isEmpty()) {
      server.send(403, "text/plain", "Forbidden");
      return false;
    }
    touch();
    return true;
  }
  static bool integer(const String &s, int low, int high, int &out) {
    if (s.isEmpty() || s.length() > 4)
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
