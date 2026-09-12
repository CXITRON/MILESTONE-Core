#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace MilestoneV5 {
namespace UpdateUi {

enum class Font { Small, Body, Key, Korean, Number, Version };
enum class Result { Available, Current, Failed };

// Update-only layout. The surface measures the actual firmware font; no CORE
// alignment/scroll settings, OTA state, storage or button handling live here.
template <class Surface> class Screen {
public:
  explicit Screen(Surface &surface) : s(surface) {}

  void result(Result result, const char *version, const char *error,
              bool automatic, bool ready) {
    const uint16_t accent = result == Result::Failed ? Red
                            : result == Result::Current ? Green : Cyan;
    header(automatic ? "AUTO CHECK" : "MANUAL CHECK",
           result == Result::Available ? "업데이트 있음"
           : result == Result::Current ? "현재 최신 버전" : "확인 실패", accent);
    if (result == Result::Failed) {
      detail(error && error[0] ? error : "원인 정보 없음");
      center("상세: 설정 AP", Font::Korean, 103, Muted);
      closeHint();
    } else {
      versionLine(version);
      center(result == Result::Current ? "확인 완료"
             : ready ? "파일 준비 완료" : "서명 확인 완료",
             Font::Korean, 88, Muted);
      if (result == Result::Available)
        choices(ready ? "설치" : "다운로드");
      else
        closeHint();
    }
  }

  void confirmation(const char *version, unsigned seconds, bool download) {
    char caption[32];
    snprintf(caption, sizeof(caption), "CONFIRM / %us", seconds);
    header(caption, download ? "다운로드 확인" : "설치 확인", Cyan);
    if (version && version[0])
      versionLine(version);
    else
      center("서명된 묶음", Font::Korean, 66, White);
    center(download ? "파일 먼저 받기" : "검증 후 설치", Font::Korean, 88, Muted);
    choices(download ? "다운로드" : "설치");
  }

  void progress(const char *caption, const char *title, const char *waiting,
                uint32_t done, uint32_t total, bool measurable, bool cancel) {
    header(caption, title, Cyan);
    if (measurable && total) {
      const unsigned percent = done >= total ? 100 : uint64_t(done) * 100 / total;
      char value[32];
      snprintf(value, sizeof(value), "%u%%", percent);
      center(value, Font::Number, 71, White);
      s.frame(8, 80, 112, 8, Muted);
      if (percent)
        s.fill(10, 82, percent * 108 / 100, 4, Cyan);
      // Binary units keep the transfer count legible even for large images.
      const uint32_t unit = total >= 1048576 ? 1048576 : 1024;
      const uint64_t d = uint64_t(done > total ? total : done) * 10 / unit;
      const uint64_t t = uint64_t(total) * 10 / unit;
      snprintf(value, sizeof(value), "%lu.%lu / %lu.%lu %s",
               (unsigned long)(d / 10), (unsigned long)(d % 10),
               (unsigned long)(t / 10), (unsigned long)(t % 10),
               unit == 1024 ? "KiB" : "MiB");
      center(value, Font::Small, 102, Muted);
    } else {
      // A handshake/self-test has no byte-based percentage. Never show a
      // stale image counter as its progress, or invent progress while waiting.
      center("--", Font::Number, 71, White);
      center(waiting, Font::Korean, 99, Muted);
    }
    s.rule(109, Rule);
    if (cancel)
      key("BACK", "취소", 123);
    else
      center("전원 유지", Font::Korean, 123, Muted);
  }

private:
  Surface &s;
  static constexpr uint16_t White = 0xFFFF, Muted = 0xBDF7, Cyan = 0x36DF,
                            Green = 0x5710, Red = 0xF800, Rule = 0x4208;

  void center(const char *text, Font font, int baseline, uint16_t color) {
    s.text(text, font, (128 - s.width(text, font)) / 2, baseline, color);
  }
  void header(const char *caption, const char *title, uint16_t accent) {
    s.clear();
    center(caption, Font::Small, 8, Muted);
    center(title, Font::Korean, 29, accent);
    s.rule(35, Rule);
  }
  void versionLine(const char *version) {
    char value[28];
    snprintf(value, sizeof(value), "v%s", version && version[0] ? version : "--");
    Font font = Font::Version;
    if (s.width(value, font) > 120) font = Font::Key;
    if (s.width(value, font) > 120) font = Font::Body;
    center(value, font, 68, White);
  }
  void key(const char *button, const char *action, int baseline) {
    // Labels share an optical centre despite the different font heights.
    s.text(button, Font::Key, 8, baseline - 1, Muted);
    s.text(action, Font::Korean, 120 - s.width(action, Font::Korean), baseline, White);
  }
  void choices(const char *action) {
    s.rule(94, Rule);
    key("OK", action, 108);
    key("BACK", "취소", 125);
  }
  void closeHint() {
    s.rule(109, Rule);
    s.text("ANY KEY", Font::Small, 8, 121, Muted);
    s.text("닫기", Font::Korean, 120 - s.width("닫기", Font::Korean), 123, White);
  }

  static size_t characterBytes(const char *p) {
    const uint8_t c = static_cast<uint8_t>(*p);
    if (!c) return 0;
    if (c < 0x80) return 1;
    const size_t n = c >= 0xC2 && c <= 0xDF ? 2
                     : c >= 0xE0 && c <= 0xEF ? 3
                     : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
    for (size_t i = 1; i < n; ++i)
      if ((static_cast<uint8_t>(p[i]) & 0xC0) != 0x80) return 0;
    return n;
  }
  static void removeLast(char *line) {
    size_t length = strlen(line);
    if (!length) return;
    do { --length; } while (length && (uint8_t(line[length]) & 0xC0) == 0x80);
    line[length] = 0;
  }
  void detail(const char *error) {
    Font font = Font::Body;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(error); *p; ++p)
      if (*p >= 0x80) { font = Font::Korean; break; }
    for (unsigned row = 0; row < 3; ++row) {
      char line[64] = {};
      size_t used = 0;
      size_t lastSpace = 0;
      const char *afterSpace = nullptr;
      while (*error == ' ' || *error == '\n' || *error == '\r') ++error;
      while (*error) {
        if (*error == '\n' || *error == '\r') { ++error; break; }
        const size_t bytes = characterBytes(error);
        const size_t count = bytes ? bytes : 1;
        if (used + count >= sizeof(line)) break;
        if (bytes) memcpy(line + used, error, count);
        else line[used] = '?';
        line[used + count] = 0;
        if (s.width(line, font) > 116) {
          line[used] = 0;
          if (afterSpace && *error != ' ') {
            used = lastSpace;
            line[used] = 0;
            error = afterSpace;
          }
          break;
        }
        if (*error == ' ' && used) { lastSpace = used; afterSpace = error + 1; }
        used += count;
        error += count;
      }
      while (used && line[used - 1] == ' ') line[--used] = 0;
      if (row == 2 && *error) {
        while (strlen(line) > sizeof(line) - 4 ||
               s.width(line, font) + s.width("...", font) > 116)
          removeLast(line);
        strcat(line, "...");
      }
      if (line[0]) center(line, font, 52 + row * 17, White);
      if (!*error) break;
    }
  }
};

} // namespace UpdateUi
} // namespace MilestoneV5
