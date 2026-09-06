#pragma once

#include <Adafruit_GFX.h>
#include <SPI.h>

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

    command(0x01); delay(150);                 // SWRESET
    command(0x11); delay(120);                 // SLPOUT
    const uint8_t b1[] = {0x01, 0x2C, 0x2D};
    const uint8_t b3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    const uint8_t c0[] = {0xA2, 0x02, 0x84};
    const uint8_t c2[] = {0x0A, 0x00};
    const uint8_t c3[] = {0x8A, 0x2A};
    const uint8_t c4[] = {0x8A, 0xEE};
    commandData(0xB1, b1, sizeof(b1));
    commandData(0xB2, b1, sizeof(b1));
    commandData(0xB3, b3, sizeof(b3));
    const uint8_t b4 = 0x07; commandData(0xB4, &b4, 1);
    commandData(0xC0, c0, sizeof(c0));
    const uint8_t c1 = 0xC5; commandData(0xC1, &c1, 1);
    commandData(0xC2, c2, sizeof(c2));
    commandData(0xC3, c3, sizeof(c3));
    commandData(0xC4, c4, sizeof(c4));
    const uint8_t c5 = 0x0E; commandData(0xC5, &c5, 1);
    command(0x20);                              // INVOFF
    const uint8_t madctl = 0x00; commandData(0x36, &madctl, 1);
    const uint8_t colmod = 0x05; commandData(0x3A, &colmod, 1);
    command(0x13); delay(10);                  // NORON
    command(0x29); delay(100);                 // DISPON
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    startWrite();
    writePixel(x, y, color);
    endWrite();
  }

  void startWrite() override {
    if (transactionDepth_++ == 0) {
      spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
      digitalWrite(cs_, LOW);
    }
  }

  void endWrite() override {
    if (transactionDepth_ && --transactionDepth_ == 0) {
      digitalWrite(cs_, HIGH);
      spi_.endTransaction();
    }
  }

  void writePixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    setWindow(x, y, x, y);
    writeColor(color, 1);
  }

  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) override {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width()) w = width() - x;
    if (y + h > height()) h = height() - y;
    if (w <= 0 || h <= 0) return;
    setWindow(x, y, x + w - 1, y + h - 1);
    writeColor(color, static_cast<uint32_t>(w) * h);
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    writeFillRect(x, y, w, 1, c);
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
    writeFillRect(x, y, 1, h, c);
  }

 private:
  SPIClass &spi_;
  int8_t cs_, dc_, reset_;
  uint8_t transactionDepth_ = 0;

  void command(uint8_t value) {
    spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW); digitalWrite(dc_, LOW); spi_.transfer(value);
    digitalWrite(cs_, HIGH); spi_.endTransaction();
  }
  void commandData(uint8_t cmd, const uint8_t *data, size_t length) {
    spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW); digitalWrite(dc_, LOW); spi_.transfer(cmd);
    digitalWrite(dc_, HIGH);
    while (length--) spi_.transfer(*data++);
    digitalWrite(cs_, HIGH); spi_.endTransaction();
  }
  void rawCommand(uint8_t value) {
    digitalWrite(dc_, LOW); spi_.transfer(value); digitalWrite(dc_, HIGH);
  }
  void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    rawCommand(0x2A);
    spi_.transfer(x0 >> 8); spi_.transfer(x0); spi_.transfer(x1 >> 8); spi_.transfer(x1);
    rawCommand(0x2B);
    spi_.transfer(y0 >> 8); spi_.transfer(y0); spi_.transfer(y1 >> 8); spi_.transfer(y1);
    rawCommand(0x2C);
  }
  void writeColor(uint16_t color, uint32_t count) {
    const uint8_t hi = color >> 8, lo = color;
    while (count--) { spi_.transfer(hi); spi_.transfer(lo); }
  }
};
