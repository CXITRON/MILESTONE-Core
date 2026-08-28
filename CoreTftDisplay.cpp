#include "CoreTftDisplay.h"
#include "CoreLogic.h"

#include <algorithm>
#include <esp_heap_caps.h>

MilestoneTftDisplay::MilestoneTftDisplay(const u8g2_cb_t *rotation,
                                         uint8_t sck, uint8_t mosi,
                                         uint8_t cs, uint8_t dc, uint8_t reset)
    : U8G2(), sck_(sck), mosi_(mosi), cs_(cs), dc_(dc), reset_(reset) {
  // The SH1107 setup function is used only to allocate/configure the exact
  // 128x128 page-major framebuffer expected by the existing renderer and MSM1
  // media format. Its byte callback is deliberately empty: no OLED bus is
  // initialized or driven.
  u8g2_Setup_sh1107_pimoroni_128x128_f(
      &u8g2, rotation, u8x8_byte_empty, u8x8_dummy_cb);
}

void MilestoneTftDisplay::beginTransfer() {
  SPI.beginTransaction(spiSettings_);
  digitalWrite(cs_, LOW);
}

void MilestoneTftDisplay::endTransfer() {
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void MilestoneTftDisplay::command(uint8_t value, const uint8_t *data,
                                  size_t length) {
  beginTransfer();
  digitalWrite(dc_, LOW);
  SPI.write(value);
  if (length > 0) {
    digitalWrite(dc_, HIGH);
    SPI.writeBytes(data, length);
  }
  endTransfer();
}

void MilestoneTftDisplay::hardwareReset() {
  digitalWrite(reset_, HIGH);
  delay(20);
  digitalWrite(reset_, LOW);
  delay(20);
  digitalWrite(reset_, HIGH);
  delay(150);
}

void MilestoneTftDisplay::initializeController() {
  hardwareReset();
  command(0x01);  // SWRESET
  delay(150);
  command(0x11);  // SLPOUT
  delay(150);

  const uint8_t frame1[] = {0x01, 0x2C, 0x2D};
  const uint8_t frame3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
  const uint8_t invctr[] = {0x07};
  const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
  const uint8_t pwctr2[] = {0xC5};
  const uint8_t pwctr3[] = {0x0A, 0x00};
  const uint8_t pwctr4[] = {0x8A, 0x2A};
  const uint8_t pwctr5[] = {0x8A, 0xEE};
  const uint8_t vmctr1[] = {0x0E};
  const uint8_t colmod[] = {0x05};  // RGB565
  const uint8_t madctl[] = {0x00};  // Verified upright orientation, RGB order.

  command(0xB1, frame1, sizeof(frame1));
  command(0xB2, frame1, sizeof(frame1));
  command(0xB3, frame3, sizeof(frame3));
  command(0xB4, invctr, sizeof(invctr));
  command(0xC0, pwctr1, sizeof(pwctr1));
  command(0xC1, pwctr2, sizeof(pwctr2));
  command(0xC2, pwctr3, sizeof(pwctr3));
  command(0xC3, pwctr4, sizeof(pwctr4));
  command(0xC4, pwctr5, sizeof(pwctr5));
  command(0xC5, vmctr1, sizeof(vmctr1));
  command(0x20);  // INVOFF
  command(0x36, madctl, sizeof(madctl));
  command(0x3A, colmod, sizeof(colmod));
  command(0x13);  // NORON
  delay(10);
  command(0x29);  // DISPON
  delay(100);
}

void MilestoneTftDisplay::setAddressWindow(uint16_t x, uint16_t y,
                                           uint16_t width, uint16_t height) {
  const uint16_t x2 = x + width - 1;
  const uint16_t y2 = y + height - 1;
  const uint8_t columns[] = {
      static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x),
      static_cast<uint8_t>(x2 >> 8), static_cast<uint8_t>(x2),
  };
  const uint8_t rows[] = {
      static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y),
      static_cast<uint8_t>(y2 >> 8), static_cast<uint8_t>(y2),
  };

  digitalWrite(dc_, LOW);
  SPI.write(0x2A);  // CASET
  digitalWrite(dc_, HIGH);
  SPI.writeBytes(columns, sizeof(columns));
  digitalWrite(dc_, LOW);
  SPI.write(0x2B);  // RASET
  digitalWrite(dc_, HIGH);
  SPI.writeBytes(rows, sizeof(rows));
  digitalWrite(dc_, LOW);
  SPI.write(0x2C);  // RAMWR
  digitalWrite(dc_, HIGH);
}

void MilestoneTftDisplay::clearPanel() {
  uint8_t black[256] = {};
  uint32_t remaining = static_cast<uint32_t>(TFT_WIDTH) * TFT_HEIGHT * 2U;
  beginTransfer();
  setAddressWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
  while (remaining > 0) {
    const size_t chunk = std::min<uint32_t>(remaining, sizeof(black));
    SPI.writeBytes(black, chunk);
    remaining -= chunk;
  }
  endTransfer();
}

uint16_t MilestoneTftDisplay::glyph3x5(char value) {
  const auto rows = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
    return static_cast<uint16_t>((a << 12) | (b << 9) | (c << 6) | (d << 3) | e);
  };
  switch (value) {
    case 'A': return rows(2, 5, 7, 5, 5);
    case 'C': return rows(3, 4, 4, 4, 3);
    case 'D': return rows(6, 5, 5, 5, 6);
    case 'E': return rows(7, 4, 6, 4, 7);
    case 'I': return rows(7, 2, 2, 2, 7);
    case 'M': return rows(5, 7, 7, 5, 5);
    case 'N': return rows(5, 7, 7, 7, 5);
    case 'O': return rows(2, 5, 5, 5, 2);
    case 'R': return rows(6, 5, 6, 5, 5);
    case 'W': return rows(5, 5, 7, 7, 5);
    default: return 0;
  }
}

bool MilestoneTftDisplay::labelPixel(const char *text, int16_t originX,
                                     uint16_t x, uint16_t y) {
  constexpr uint8_t scale = 2;
  constexpr uint16_t cellWidth = 4U * scale;
  if (!text || x < originX || y < 3U || y >= 13U) return false;
  const uint16_t localX = x - originX;
  const size_t index = localX / cellWidth;
  if (index >= strlen(text)) return false;
  const uint16_t glyphX = localX % cellWidth;
  if (glyphX >= 3U * scale) return false;
  const uint8_t row = (y - 3U) / scale;
  const uint8_t column = glyphX / scale;
  return (glyph3x5(text[index]) &
          static_cast<uint16_t>(1U << (14U - row * 3U - column))) != 0;
}

void MilestoneTftDisplay::drawFrameChrome() {
  if (!initialized_ || !powered_) return;
  const uint16_t lineColor = toneRgb565(0x7BEF);
  const uint16_t labelColor = toneRgb565(
      strcmp(profileLabel_, "MEDIA") == 0 ? 0xF81F
      : strcmp(profileLabel_, "NOW") == 0 ? 0x37F1 : 0x26FF);
  uint8_t pixels[TFT_WIDTH * 2U];
  const size_t labelLength = strlen(profileLabel_);
  const int16_t labelWidth = labelLength == 0 ? 0 :
      static_cast<int16_t>((labelLength * 4U - 1U) * 2U);
  const int16_t labelX = (TFT_WIDTH - labelWidth) / 2;

  beginTransfer();
  setAddressWindow(0, 0, TFT_WIDTH, FRAME_Y);
  for (uint16_t y = 0; y < FRAME_Y; ++y) {
    for (uint16_t x = 0; x < TFT_WIDTH; ++x) {
      const uint16_t color = y == FRAME_Y - 1U ? lineColor
          : labelPixel(profileLabel_, labelX, x, y) ? labelColor : 0;
      pixels[x * 2U] = static_cast<uint8_t>(color >> 8);
      pixels[x * 2U + 1U] = static_cast<uint8_t>(color);
    }
    SPI.writeBytes(pixels, sizeof(pixels));
  }
  setAddressWindow(0, FRAME_Y + FRAME_HEIGHT, TFT_WIDTH, 1);
  const uint16_t bottomLineColor = toneRgb565(0xFFFF);
  for (uint16_t x = 0; x < TFT_WIDTH; ++x) {
    pixels[x * 2U] = static_cast<uint8_t>(bottomLineColor >> 8U);
    pixels[x * 2U + 1U] = static_cast<uint8_t>(bottomLineColor);
  }
  SPI.writeBytes(pixels, sizeof(pixels));
  endTransfer();
}

uint8_t MilestoneTftDisplay::toneChannel8(uint8_t value) const {
  return MilestoneCoreLogic::applyDisplayTone(
      value, toneLuminancePercent_, toneContrastPercent_);
}

uint16_t MilestoneTftDisplay::rgb888To565(uint32_t rgb888) const {
  const uint8_t red = toneChannel8(static_cast<uint8_t>(rgb888 >> 16U));
  const uint8_t green = toneChannel8(static_cast<uint8_t>(rgb888 >> 8U));
  const uint8_t blue = toneChannel8(static_cast<uint8_t>(rgb888));
  return static_cast<uint16_t>(((red >> 3U) << 11U) |
                               ((green >> 2U) << 5U) | (blue >> 3U));
}

void MilestoneTftDisplay::setToneAdjustment(uint8_t luminancePercent,
                                            int8_t contrastPercent) {
  toneLuminancePercent_ = std::max<uint8_t>(50U, std::min<uint8_t>(100U, luminancePercent));
  toneContrastPercent_ = std::max<int8_t>(-20, std::min<int8_t>(20, contrastPercent));
  initializeToneTables();
  if (initialized_) drawFrameChrome();
}

void MilestoneTftDisplay::initializeToneTables() {
  for (uint8_t value = 0; value < 32U; ++value) {
    tone5_[value] = static_cast<uint8_t>(toneChannel8(
        static_cast<uint8_t>(value * 255U / 31U)) >> 3U);
  }
  for (uint8_t value = 0; value < 64U; ++value) {
    tone6_[value] = static_cast<uint8_t>(toneChannel8(
        static_cast<uint8_t>(value * 255U / 63U)) >> 2U);
  }
}

uint16_t MilestoneTftDisplay::toneRgb565(uint16_t color) const {
  return static_cast<uint16_t>(tone5_[(color >> 11U) & 0x1FU] << 11U) |
         static_cast<uint16_t>(tone6_[(color >> 5U) & 0x3FU] << 5U) |
         tone5_[color & 0x1FU];
}

void MilestoneTftDisplay::setInkColor(uint32_t rgb888) {
  inkColor_ = rgb888To565(rgb888);
}

void MilestoneTftDisplay::beginColorCapture() {
  const uint8_t *buffer = getBufferPtr();
  if (buffer) memcpy(previousMono_, buffer, sizeof(previousMono_));
}

void MilestoneTftDisplay::finishColorCapture() {
  if (!colorBuffer_) return;
  const uint8_t *buffer = getBufferPtr();
  if (!buffer) return;
  for (size_t byte = 0; byte < sizeof(previousMono_); ++byte) {
    const uint8_t changed = previousMono_[byte] ^ buffer[byte];
    if (!changed) continue;
    const uint16_t pageY = static_cast<uint16_t>(byte / FRAME_WIDTH) * 8U;
    const uint16_t x = byte % FRAME_WIDTH;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((changed & (1U << bit)) == 0) continue;
      colorBuffer_[static_cast<size_t>(pageY + bit) * FRAME_WIDTH + x] =
          (buffer[byte] & (1U << bit)) ? inkColor_ : 0;
    }
  }
}

void MilestoneTftDisplay::clearBuffer() {
  U8G2::clearBuffer();
  if (colorBuffer_) memset(colorBuffer_, 0, FRAME_WIDTH * FRAME_HEIGHT * sizeof(uint16_t));
}

void MilestoneTftDisplay::setDrawColor(uint8_t color) {
  U8G2::setDrawColor(color);
}

#define MILESTONE_CAPTURE_VOID(call) do { beginColorCapture(); call; finishColorCapture(); } while (0)

void MilestoneTftDisplay::drawHLine(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w) {
  MILESTONE_CAPTURE_VOID(U8G2::drawHLine(x, y, w));
}
void MilestoneTftDisplay::drawFrame(u8g2_uint_t x, u8g2_uint_t y,
                                    u8g2_uint_t w, u8g2_uint_t h) {
  MILESTONE_CAPTURE_VOID(U8G2::drawFrame(x, y, w, h));
}
void MilestoneTftDisplay::drawBox(u8g2_uint_t x, u8g2_uint_t y,
                                  u8g2_uint_t w, u8g2_uint_t h) {
  MILESTONE_CAPTURE_VOID(U8G2::drawBox(x, y, w, h));
}
void MilestoneTftDisplay::drawCircle(u8g2_uint_t x, u8g2_uint_t y,
                                     u8g2_uint_t rad, uint8_t opt) {
  MILESTONE_CAPTURE_VOID(U8G2::drawCircle(x, y, rad, opt));
}
void MilestoneTftDisplay::drawDisc(u8g2_uint_t x, u8g2_uint_t y,
                                   u8g2_uint_t rad, uint8_t opt) {
  MILESTONE_CAPTURE_VOID(U8G2::drawDisc(x, y, rad, opt));
}
void MilestoneTftDisplay::drawLine(u8g2_uint_t x1, u8g2_uint_t y1,
                                   u8g2_uint_t x2, u8g2_uint_t y2) {
  MILESTONE_CAPTURE_VOID(U8G2::drawLine(x1, y1, x2, y2));
}
void MilestoneTftDisplay::drawXBMP(u8g2_uint_t x, u8g2_uint_t y,
                                   u8g2_uint_t w, u8g2_uint_t h,
                                   const uint8_t *bitmap) {
  MILESTONE_CAPTURE_VOID(U8G2::drawXBMP(x, y, w, h, bitmap));
}

#undef MILESTONE_CAPTURE_VOID

u8g2_uint_t MilestoneTftDisplay::drawStr(u8g2_uint_t x, u8g2_uint_t y,
                                         const char *text) {
  beginColorCapture();
  const u8g2_uint_t width = U8G2::drawStr(x, y, text);
  finishColorCapture();
  return width;
}

u8g2_uint_t MilestoneTftDisplay::drawUTF8(u8g2_uint_t x, u8g2_uint_t y,
                                          const char *text) {
  beginColorCapture();
  const u8g2_uint_t width = U8G2::drawUTF8(x, y, text);
  finishColorCapture();
  return width;
}

u8g2_uint_t MilestoneTftDisplay::drawUTF8X2(u8g2_uint_t x, u8g2_uint_t y,
                                            const char *text) {
  beginColorCapture();
  const u8g2_uint_t width = U8G2::drawUTF8X2(x, y, text);
  finishColorCapture();
  return width;
}

void MilestoneTftDisplay::colorizeMonochrome(uint32_t rgb888) {
  if (!colorBuffer_) return;
  const uint8_t *buffer = getBufferPtr();
  if (!buffer) return;
  const uint16_t color = rgb888To565(rgb888);
  for (uint16_t y = 0; y < FRAME_HEIGHT; ++y) {
    for (uint16_t x = 0; x < FRAME_WIDTH; ++x) {
      const size_t byte = static_cast<size_t>(y >> 3) * FRAME_WIDTH + x;
      colorBuffer_[static_cast<size_t>(y) * FRAME_WIDTH + x] =
          (buffer[byte] & (1U << (y & 7U))) ? color : 0;
    }
  }
}

bool MilestoneTftDisplay::begin() {
  initializeToneTables();
  inkColor_ = rgb888To565(0xFFFFFFUL);
  pinMode(cs_, OUTPUT);
  pinMode(dc_, OUTPUT);
  pinMode(reset_, OUTPUT);
  digitalWrite(cs_, HIGH);
  digitalWrite(dc_, HIGH);
  digitalWrite(reset_, HIGH);
  SPI.begin(sck_, -1, mosi_, cs_);

  initializeController();
  enableUTF8Print();
  colorBuffer_ = static_cast<uint16_t *>(heap_caps_malloc(
      FRAME_WIDTH * FRAME_HEIGHT * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  clearBuffer();
  clearPanel();
  powered_ = true;
  initialized_ = true;
  drawFrameChrome();
  return true;
}

void MilestoneTftDisplay::setProfileLabel(const char *label) {
  if (!label) return;
  size_t index = 0;
  while (index + 1U < sizeof(profileLabel_) && label[index] != '\0') {
    const char value = label[index];
    profileLabel_[index] = value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
    ++index;
  }
  profileLabel_[index] = '\0';
  drawFrameChrome();
}

void MilestoneTftDisplay::setPowerSave(uint8_t enabled) {
  const bool powerOn = enabled == 0;
  if (powerOn == powered_) return;
  command(powerOn ? 0x29 : 0x28);  // DISPON / DISPOFF
  powered_ = powerOn;
  if (powerOn) drawFrameChrome();
}

void MilestoneTftDisplay::writeBufferArea(uint16_t x, uint16_t y,
                                          uint16_t width, uint16_t height) {
  if (!powered_ || width == 0 || height == 0 || x >= FRAME_WIDTH || y >= FRAME_HEIGHT) return;
  width = std::min<uint16_t>(width, FRAME_WIDTH - x);
  height = std::min<uint16_t>(height, FRAME_HEIGHT - y);
  const uint8_t *buffer = getBufferPtr();
  if (!buffer) return;

  uint8_t pixels[256];
  beginTransfer();
  setAddressWindow(x, FRAME_Y + y, width, height);
  for (uint16_t row = y; row < y + height; ++row) {
    uint16_t column = x;
    while (column < x + width) {
      const uint16_t count = std::min<uint16_t>(128, x + width - column);
      for (uint16_t i = 0; i < count; ++i) {
        const uint16_t pixelX = column + i;
        const size_t index = static_cast<size_t>(row >> 3) * FRAME_WIDTH + pixelX;
        const bool set = (buffer[index] & static_cast<uint8_t>(1U << (row & 7U))) != 0;
        const uint16_t color = colorBuffer_
            ? colorBuffer_[static_cast<size_t>(row) * FRAME_WIDTH + pixelX]
            : (set ? 0xFFFF : 0);
        pixels[i * 2U] = static_cast<uint8_t>(color >> 8);
        pixels[i * 2U + 1U] = static_cast<uint8_t>(color);
      }
      SPI.writeBytes(pixels, static_cast<size_t>(count) * 2U);
      column += count;
    }
  }
  endTransfer();
}

void MilestoneTftDisplay::drawRgb332(uint16_t x, uint16_t y, uint16_t width,
                                     uint16_t height, const uint8_t *source) {
  if (!powered_ || !source || !width || !height || x + width > FRAME_WIDTH ||
      y + height > FRAME_HEIGHT) return;
  uint8_t pixels[256];
  beginTransfer();
  setAddressWindow(x, FRAME_Y + y, width, height);
  for (uint16_t row = 0; row < height; ++row) {
    uint16_t column = 0;
    while (column < width) {
      const uint16_t count = std::min<uint16_t>(128, width - column);
      for (uint16_t i = 0; i < count; ++i) {
        const uint8_t value = source[static_cast<size_t>(row) * width + column + i];
        const uint16_t red = static_cast<uint16_t>((value >> 5U) & 0x07U) * 31U / 7U;
        const uint16_t green = static_cast<uint16_t>((value >> 2U) & 0x07U) * 63U / 7U;
        const uint16_t blue = static_cast<uint16_t>(value & 0x03U) * 31U / 3U;
        const uint16_t color = toneRgb565(
            static_cast<uint16_t>((red << 11U) | (green << 5U) | blue));
        pixels[i * 2U] = static_cast<uint8_t>(color >> 8U);
        pixels[i * 2U + 1U] = static_cast<uint8_t>(color);
      }
      SPI.writeBytes(pixels, static_cast<size_t>(count) * 2U);
      column += count;
    }
  }
  endTransfer();
}

void MilestoneTftDisplay::loadRgb332(const uint8_t *source) {
  if (!source || !colorBuffer_) return;
  memset(getBufferPtr(), 0xFF, FRAME_WIDTH * FRAME_HEIGHT / 8U);
  for (size_t i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i) {
    const uint8_t value = source[i];
    const uint16_t red = static_cast<uint16_t>((value >> 5U) & 0x07U) * 31U / 7U;
    const uint16_t green = static_cast<uint16_t>((value >> 2U) & 0x07U) * 63U / 7U;
    const uint16_t blue = static_cast<uint16_t>(value & 0x03U) * 31U / 3U;
    colorBuffer_[i] = toneRgb565(
        static_cast<uint16_t>((red << 11U) | (green << 5U) | blue));
  }
}

void MilestoneTftDisplay::loadRgb565(uint16_t x, uint16_t y, uint16_t width,
                                     uint16_t height, const uint8_t *source) {
  if (!source || !colorBuffer_ || !width || !height ||
      x + width > FRAME_WIDTH || y + height > FRAME_HEIGHT) return;
  uint8_t *mono = getBufferPtr();
  for (uint16_t row = 0; row < height; ++row) {
    for (uint16_t column = 0; column < width; ++column) {
      const size_t sourceOffset = (static_cast<size_t>(row) * width + column) * 2U;
      const uint16_t targetX = x + column;
      const uint16_t targetY = y + row;
      const uint16_t sourceColor =
          static_cast<uint16_t>(source[sourceOffset] << 8U) | source[sourceOffset + 1U];
      colorBuffer_[static_cast<size_t>(targetY) * FRAME_WIDTH + targetX] =
          toneRgb565(sourceColor);
      mono[static_cast<size_t>(targetY >> 3U) * FRAME_WIDTH + targetX] |=
          static_cast<uint8_t>(1U << (targetY & 7U));
    }
  }
}

void MilestoneTftDisplay::drawRgb565(uint16_t x, uint16_t y, uint16_t width,
                                     uint16_t height, const uint8_t *source) {
  if (!powered_ || !source || !width || !height || x + width > FRAME_WIDTH ||
      y + height > FRAME_HEIGHT) return;
  uint8_t pixels[256];
  beginTransfer();
  setAddressWindow(x, FRAME_Y + y, width, height);
  for (uint16_t row = 0; row < height; ++row) {
    uint16_t column = 0;
    while (column < width) {
      const uint16_t count = std::min<uint16_t>(128, width - column);
      for (uint16_t i = 0; i < count; ++i) {
        const size_t offset =
            (static_cast<size_t>(row) * width + column + i) * 2U;
        const uint16_t color = toneRgb565(
            static_cast<uint16_t>(source[offset] << 8U) | source[offset + 1U]);
        pixels[i * 2U] = static_cast<uint8_t>(color >> 8U);
        pixels[i * 2U + 1U] = static_cast<uint8_t>(color);
      }
      SPI.writeBytes(pixels, static_cast<size_t>(count) * 2U);
      column += count;
    }
  }
  endTransfer();
}

void MilestoneTftDisplay::sendBuffer() {
  writeBufferArea(0, 0, FRAME_WIDTH, FRAME_HEIGHT);
}

void MilestoneTftDisplay::updateDisplayArea(uint8_t tileX, uint8_t tileY,
                                            uint8_t tileWidth, uint8_t tileHeight) {
  writeBufferArea(static_cast<uint16_t>(tileX) * 8U,
                  static_cast<uint16_t>(tileY) * 8U,
                  static_cast<uint16_t>(tileWidth) * 8U,
                  static_cast<uint16_t>(tileHeight) * 8U);
}
