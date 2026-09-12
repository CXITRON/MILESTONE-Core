#include <MilestoneV5UpdateUi.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#ifdef V5_UPDATE_UI_REAL_FONTS
#include "../v5/MilestoneV5Main/V5UpdateScreen.h"
#include <fstream>
#endif

using namespace MilestoneV5::UpdateUi;
struct Text { std::string value; Font font; int x, y; };
struct TestSurface {
  std::vector<Text> texts;
  int fillWidth = 0;
#ifdef V5_UPDATE_UI_REAL_FONTS
  u8g2_t metrics{};
  u8g2_t glyphs{};
  u8x8_display_info_t info{};
  uint8_t buffer[192 * 192 / 8]{};
  uint16_t pixels[128 * 128]{};
  TestSurface() {
    // A margin around the real 128-square surface detects clipped glyphs,
    // including descenders, rather than silently clipping them in the test.
    info.tile_width = info.tile_height = 24;
    info.pixel_width = info.pixel_height = 192;
    glyphs.u8x8.display_info = &info;
    u8g2_SetupBuffer(&glyphs, buffer, 24, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  }
#endif
  int width(const char *value, Font f) {
#ifdef V5_UPDATE_UI_REAL_FONTS
    u8g2_SetFont(&metrics, V5UpdateScreen::font(f));
    return u8g2_GetUTF8Width(&metrics, value);
#else
    int width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
      if ((*p & 0xC0) == 0x80) continue;
      width += *p >= 0x80 ? 16 : f == Font::Small ? 5 : f == Font::Body ? 6
                                : f == Font::Key ? 7 : f == Font::Korean ? 8
                                : f == Font::Number ? 18 : 12;
    }
    return width;
#endif
  }
  void clear() {
    texts.clear(); fillWidth = 0;
#ifdef V5_UPDATE_UI_REAL_FONTS
    std::fill(pixels, pixels + 128 * 128, 0);
#endif
  }
  void text(const char *value, Font f, int x, int y, uint16_t color) {
    if (x < 0 || x + width(value, f) > 128)
      std::cerr << "Overflow: " << value << " x=" << x << '\n';
    assert(x >= 0 && x + width(value, f) <= 128);
    assert(y >= 0 && y < 128);
    // Every UTF-8 lead has complete continuation bytes in the emitted line.
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
      if (*p < 0x80) continue;
      unsigned bytes = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : 4;
      for (unsigned i = 1; i < bytes; ++i) { ++p; assert((*p & 0xC0) == 0x80); }
    }
    texts.push_back({value, f, x, y});
#ifdef V5_UPDATE_UI_REAL_FONTS
    std::fill(buffer, buffer + sizeof(buffer), 0);
    u8g2_SetFont(&glyphs, V5UpdateScreen::font(f));
    // Missing Korean glyphs must fail QA rather than yielding a blank label.
    u8x8_utf8_init(&glyphs.u8x8);
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
      const uint16_t c = u8x8_utf8_next(&glyphs.u8x8, *p);
      if (c < 0xFFFE && !u8g2_IsGlyph(&glyphs, c)) {
        std::cerr << "Missing glyph " << c << " in " << value << '\n';
        assert(false);
      }
    }
    u8g2_DrawUTF8(&glyphs, 32 + x, 32 + y, value);
    for (int py = 0; py < 192; ++py)
      for (int px = 0; px < 192; ++px)
        if (buffer[(py / 8) * 192 + px] & (1 << (py % 8))) {
          assert(px >= 32 && px < 160 && py >= 32 && py < 160);
          auto &pixel = pixels[(py - 32) * 128 + px - 32];
          if (pixel) std::cerr << "Overlap: " << value << '\n';
          assert(!pixel); // No text/rule/other text painted on top of a glyph.
          pixel = color;
        }
#else
    (void)color;
#endif
  }
  void fill(int x, int y, int w, int h, uint16_t color) {
    assert(x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= 128 && y + h <= 128);
    fillWidth = w;
#ifdef V5_UPDATE_UI_REAL_FONTS
    for (int row = y; row < y + h; ++row)
      for (int col = x; col < x + w; ++col) pixels[row * 128 + col] = color;
#else
    (void)color;
#endif
  }
  void frame(int x, int y, int w, int h, uint16_t color) {
    fill(x, y, w, 1, color); fill(x, y + h - 1, w, 1, color);
    fill(x, y, 1, h, color); fill(x + w - 1, y, 1, h, color);
    fillWidth = 0;
  }
  void rule(int y, uint16_t color) { fill(0, y, 128, 1, color); fillWidth = 0; }
  bool has(const char *text) const {
    for (const auto &entry : texts) if (entry.value == text) return true;
    return false;
  }
  void save(const char *directory, const char *name) {
#ifdef V5_UPDATE_UI_REAL_FONTS
    if (!directory) return;
    std::ofstream out(std::string(directory) + "/" + name + ".ppm", std::ios::binary);
    out << "P6\n128 128\n255\n";
    for (uint16_t c : pixels) {
      out.put((c >> 11) * 255 / 31); out.put(((c >> 5) & 63) * 255 / 63);
      out.put((c & 31) * 255 / 31);
    }
    assert(out.good());
#else
    (void)directory; (void)name;
#endif
  }
};

int main(int argc, char **argv) {
  const char *output = argc > 1 ? argv[1] : nullptr;
  TestSurface surface;
  Screen<TestSurface> screen(surface);
  screen.result(Result::Available, "5.2.2", "", true, false);
  assert(surface.has("OK") && surface.has("다운로드") && surface.has("BACK") && surface.has("취소"));
  surface.save(output, "01-available");
  screen.result(Result::Available, "5.2.2", "", false, true);
  assert(surface.has("설치")); surface.save(output, "02-ready");
  screen.result(Result::Current, "5.2.2", "", true, false);
  assert(surface.has("현재 최신 버전") && surface.has("닫기"));
  surface.save(output, "03-current");
  screen.result(Result::Failed, "", "다운로드 폴더를 사용할 수 없습니다", false, false);
  assert(surface.has("확인 실패") && surface.has("상세: 설정 AP"));
  surface.save(output, "04-failed");
  screen.result(Result::Failed, "", "HTTPS connection timed out: ZERO response missing", true, false);
  surface.save(output, "05-failed-ascii");
  screen.confirmation("5.2.2", 15, false); surface.save(output, "06-confirm");
  screen.confirmation("", 1, false); surface.save(output, "07-sd-confirm");
  screen.progress("UPDATE / DOWNLOAD", "다운로드", "연결 준비 중", 497858, 1345424, true, true);
  assert(surface.has("37%") && surface.has("BACK")); surface.save(output, "08-download");
  screen.progress("MAIN / INSTALL", "슬롯 기록", "설치 진행 중", 1345424, 1345424, true, false);
  assert(surface.has("100%") && surface.has("전원 유지")); surface.save(output, "09-writing");
  screen.progress("ZERO / BOOT", "부팅 확인", "응답 대기 중", 1345424, 1345424, false, false);
  assert(surface.has("--") && !surface.has("100%")); surface.save(output, "10-boot-wait");
  screen.progress("ZERO / INSTALL", "설치 준비", "응답 대기 중", 500, 1345424, false, false);
  assert(surface.has("--")); surface.save(output, "11-handshake");
  screen.progress("UPDATE / DOWNLOAD", "다운로드", "연결 준비 중", 0, 0, true, true);
  assert(surface.has("--") && !surface.has("0%")); surface.save(output, "12-connecting");
  for (const char *title : {"묶음 검증", "MAIN 검증", "파일 검증", "이미지 전송", "이미지 검증", "설치 확정"})
    screen.progress("BUNDLE / VERIFY", title, "검증 진행 중", 90, 100, true, false);
  for (const char *version : {"0.0.0", "5.2.2", "65535.65535.65535"}) {
    screen.result(Result::Current, version, "", true, false);
    screen.confirmation(version, 15, true);
  }
  for (uint32_t total : {1u, 1023u, 1024u, 1048575u, 1048576u, UINT32_MAX})
    for (uint32_t done : {0u, 1u, total / 2, total, UINT32_MAX})
      screen.progress("TEST", "검증 중", "응답 대기 중", done, total, true, false);
  for (const char *error : {"", "SD", "\n\r  연결 실패", "ZERO 응답 시간 초과\n다시 확인", "\xE3\x80"})
    screen.result(Result::Failed, "", error, false, false);
  for (unsigned length = 1; length < 300; ++length) {
    const std::string error = std::string(length, 'A') + "연결 오류입니다 확인해 주세요";
    screen.result(Result::Failed, "", error.c_str(), true, false);
    assert(surface.texts.size() <= 8);
  }
  std::cout << "Update-only UI layout, UTF-8, actions, bounded progress tests passed\n";
}
