#pragma once

#include <Adafruit_GFX.h>
#include <SPI.h>
#include <esp_heap_caps.h>

class SimpleSt7735 : public Adafruit_GFX {
public:
  SimpleSt7735(SPIClass &spi, int8_t cs, int8_t dc, int8_t reset)
      : Adafruit_GFX(128, 160), spi_(spi), cs_(cs), dc_(dc), reset_(reset) {}

  void begin() {
    pinMode(cs_, OUTPUT);
    pinMode(dc_, OUTPUT);
    pinMode(reset_, OUTPUT);
    digitalWrite(cs_, HIGH);
    digitalWrite(reset_, HIGH);
    delay(10);
    digitalWrite(reset_, LOW);
    delay(20);
    digitalWrite(reset_, HIGH);
    delay(120);

    command(0x01);
    delay(150); // SWRESET
    command(0x11);
    delay(120); // SLPOUT
    const uint8_t b1[] = {0x01, 0x2C, 0x2D};
    const uint8_t b3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    const uint8_t c0[] = {0xA2, 0x02, 0x84};
    const uint8_t c2[] = {0x0A, 0x00};
    const uint8_t c3[] = {0x8A, 0x2A};
    const uint8_t c4[] = {0x8A, 0xEE};
    commandData(0xB1, b1, sizeof(b1));
    commandData(0xB2, b1, sizeof(b1));
    commandData(0xB3, b3, sizeof(b3));
    const uint8_t b4 = 0x07;
    commandData(0xB4, &b4, 1);
    commandData(0xC0, c0, sizeof(c0));
    const uint8_t c1 = 0xC5;
    commandData(0xC1, &c1, 1);
    commandData(0xC2, c2, sizeof(c2));
    commandData(0xC3, c3, sizeof(c3));
    commandData(0xC4, c4, sizeof(c4));
    const uint8_t c5 = 0x0E;
    commandData(0xC5, &c5, 1);
    command(0x20); // INVOFF
    const uint8_t madctl = 0x00;
    commandData(0x36, &madctl, 1);
    const uint8_t colmod = 0x05;
    commandData(0x3A, &colmod, 1);
    command(0x13);
    delay(10); // NORON
    command(0x29);
    delay(100); // DISPON
    if (!frame_) {
      frame_ = static_cast<uint16_t *>(
          heap_caps_calloc(128 * 160 * 2, sizeof(uint16_t),
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (frame_)
        front_ = frame_ + 128 * 160;
    }
    invalidate_ = true;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height())
      return;
    startWrite();
    writePixel(x, y, color);
    endWrite();
  }

  void setTone(uint8_t luminance, int8_t contrast) {
    invalidate_ = true;
    for (unsigned i = 0; i < 64; ++i) {
      int v = int(i * 255 / 63);
      v = ((v - 128) * (100 + contrast)) / 100 + 128;
      v = constrain(v, 0, 255) * constrain(luminance, 50, 100) / 100;
      tone_[i] = v;
    }
  }

  void rgb565(const uint16_t *pixels, int y, int height) {
    if (!pixels || y < 0 || height <= 0 || y + height > 160)
      return;
    if (frame_) {
      memcpy(frame_ + 128 * y, pixels, 128 * height * sizeof(uint16_t));
      return;
    }
    startWrite();
    setWindow(0, y, 127, y + height - 1);
    for (int i = 0; i < 128 * height; ++i)
      writeColor(pixels[i], 1);
    endWrite();
  }

  // Video changes nearly every body tile at once. The incremental UI flusher
  // cannot finish one frame before the next replaces it, so present a decoded
  // video frame as one bounded SPI transaction and synchronize the shadow copy.
  void flushRegion(int y, int height) {
    if (!frame_ || sleeping_ || y < 0 || height <= 0 || y + height > 160)
      return;
    flushing_ = true;
    startWrite();
    setWindow(0, y, 127, y + height - 1);
    const size_t first = size_t(y) * 128U;
    const size_t count = size_t(height) * 128U;
    for (size_t i = 0; i < count; ++i) {
      writeColor(frame_[first + i], 1);
      front_[first + i] = frame_[first + i];
    }
    endWrite();
    flushing_ = false;
  }

  // Paint a legacy 128x128 U8g2 page buffer into the v5 body.  Keeping the
  // original page-oriented renderer preserves the established typography and
  // layout while the native TFT framebuffer owns the new status bands.
  void blitMono(const uint8_t *bits, int yOffset, uint16_t color) {
    if (!bits || yOffset < 0 || yOffset + 128 > 160)
      return;
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        if (bits[(y >> 3) * 128 + x] & (1U << (y & 7)))
          frame_[(yOffset + y) * 128 + x] = color;
  }

  void blitMonoPacked(const uint8_t *bits, int yOffset, uint16_t foreground,
                      uint16_t background) {
    if (!bits || !frame_ || yOffset < 0 || yOffset + 128 > 160)
      return;
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        frame_[(yOffset + y) * 128 + x] =
            bits[(y >> 3) * 128 + x] & (1U << (y & 7)) ? foreground
                                                                  : background;
  }

  void blitRgb332(const uint8_t *pixels, int yOffset) {
    if (!pixels || !frame_ || yOffset < 0 || yOffset + 128 > 160)
      return;
    for (int i = 0; i < 128 * 128; ++i) {
      const uint8_t value = pixels[i];
      const uint16_t red = ((value >> 5) & 7) * 31 / 7;
      const uint16_t green = ((value >> 2) & 7) * 63 / 7;
      const uint16_t blue = (value & 3) * 31 / 3;
      frame_[yOffset * 128 + i] = (red << 11) | (green << 5) | blue;
    }
  }

  void startWrite() override {
    if (frame_ && !flushing_)
      return;
    if (transactionDepth_++ == 0) {
      spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
      digitalWrite(cs_, LOW);
    }
  }

  void endWrite() override {
    if (frame_ && !flushing_)
      return;
    if (transactionDepth_ && --transactionDepth_ == 0) {
      digitalWrite(cs_, HIGH);
      spi_.endTransaction();
    }
  }

  void writePixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height())
      return;
    if (frame_) {
      frame_[y * 128 + x] = color;
      return;
    }
    setWindow(x, y, x, y);
    writeColor(color, 1);
  }

  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) override {
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (x + w > width())
      w = width() - x;
    if (y + h > height())
      h = height() - y;
    if (w <= 0 || h <= 0)
      return;
    if (frame_) {
      for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
          frame_[row * 128 + col] = color;
      return;
    }
    setWindow(x, y, x + w - 1, y + h - 1);
    writeColor(color, static_cast<uint32_t>(w) * h);
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    writeFillRect(x, y, w, 1, c);
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
    writeFillRect(x, y, 1, h, c);
  }
  void flush() {
    if (!frame_ || sleeping_)
      return;
    if (invalidate_) {
      for (size_t i = 0; i < 128 * 160; ++i)
        front_[i] = ~frame_[i];
      invalidate_ = false;
    }
    unsigned sent = 0;
    for (unsigned scanned = 0; scanned < 320 && sent < 48; ++scanned) {
      unsigned tile = tile_++ % 320, x = (tile % 16) * 8, y = (tile / 16) * 8;
      bool changed = false;
      for (unsigned row = 0; row < 8 && !changed; ++row)
        changed = memcmp(frame_ + (y + row) * 128 + x,
                         front_ + (y + row) * 128 + x, 16) != 0;
      if (!changed)
        continue;
      flushing_ = true;
      startWrite();
      setWindow(x, y, x + 7, y + 7);
      for (unsigned row = 0; row < 8; ++row)
        for (unsigned col = 0; col < 8; ++col) {
          unsigned i = (y + row) * 128 + x + col;
          writeColor(frame_[i], 1);
          front_[i] = frame_[i];
        }
      endWrite();
      flushing_ = false;
      ++sent;
    }
  }
  void sleep(bool value) {
    if (value == sleeping_)
      return;
    sleeping_ = value;
    command(value ? 0x28 : 0x29);
  }

private:
  SPIClass &spi_;
  int8_t cs_, dc_, reset_;
  uint8_t transactionDepth_ = 0;
  uint8_t tone_[64] = {};
  uint16_t *frame_ = nullptr, *front_ = nullptr;
  uint32_t tile_ = 0;
  bool invalidate_ = true, flushing_ = false, sleeping_ = false;

  void command(uint8_t value) {
    spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    digitalWrite(dc_, LOW);
    spi_.transfer(value);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
  }
  void commandData(uint8_t cmd, const uint8_t *data, size_t length) {
    spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    digitalWrite(dc_, LOW);
    spi_.transfer(cmd);
    digitalWrite(dc_, HIGH);
    while (length--)
      spi_.transfer(*data++);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
  }
  void rawCommand(uint8_t value) {
    digitalWrite(dc_, LOW);
    spi_.transfer(value);
    digitalWrite(dc_, HIGH);
  }
  void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    rawCommand(0x2A);
    spi_.transfer(x0 >> 8);
    spi_.transfer(x0);
    spi_.transfer(x1 >> 8);
    spi_.transfer(x1);
    rawCommand(0x2B);
    spi_.transfer(y0 >> 8);
    spi_.transfer(y0);
    spi_.transfer(y1 >> 8);
    spi_.transfer(y1);
    rawCommand(0x2C);
  }
  void writeColor(uint16_t color, uint32_t count) {
    color = (uint16_t(tone_[((color >> 11) & 31) * 2] >> 3) << 11) |
            (uint16_t(tone_[(color >> 5) & 63] >> 2) << 5) |
            (tone_[(color & 31) * 2] >> 3);
    const uint8_t hi = color >> 8, lo = color;
    while (count--) {
      spi_.transfer(hi);
      spi_.transfer(lo);
    }
  }
};
