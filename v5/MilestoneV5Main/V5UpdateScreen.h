#pragma once
#include <MilestoneV5UpdateUi.h>
#include <clib/u8g2.h>

namespace V5UpdateScreen {
inline const uint8_t *font(MilestoneV5::UpdateUi::Font value) {
  using MilestoneV5::UpdateUi::Font;
  switch (value) {
  case Font::Small: return u8g2_font_5x8_tf;
  case Font::Body: return u8g2_font_6x10_tf;
  case Font::Key: return u8g2_font_7x14B_tf;
  case Font::Korean: return u8g2_font_unifont_t_korean2;
  case Font::Number: return u8g2_font_logisoso28_tf;
  case Font::Version: return u8g2_font_logisoso20_tf;
  }
  return u8g2_font_6x10_tf;
}

template <class Hardware> class Surface {
public:
  explicit Surface(Hardware &hardware) : h(hardware) {}
  int width(const char *text, MilestoneV5::UpdateUi::Font value) {
    u8g2_SetFont(&metrics, font(value));
    return u8g2_GetUTF8Width(&metrics, text);
  }
  void clear() { h.legacyClear(); }
  void text(const char *text, MilestoneV5::UpdateUi::Font value,
            int x, int baseline, uint16_t color) {
    h.legacyText(text, baseline, font(value), color, x);
  }
  void rule(int y, uint16_t color) { h.legacyRule(y, color); }
  void frame(int x, int y, int w, int height, uint16_t color) {
    h.legacyFrame(x, y, w, height, color);
  }
  void fill(int x, int y, int w, int height, uint16_t color) {
    h.display.fillRect(x, y + 16, w, height, color);
  }
private:
  Hardware &h;
  u8g2_t metrics{}; // Width calculation only; never allocates a second framebuffer.
};
} // namespace V5UpdateScreen
