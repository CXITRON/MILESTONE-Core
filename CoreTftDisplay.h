#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

// Keeps the existing 128x128 U8g2 drawing surface and sends that monochrome
// framebuffer to the centered 128x128 region of a 128x160 ST7735 TFT.
class MilestoneTftDisplay : public U8G2 {
 public:
  MilestoneTftDisplay(const u8g2_cb_t *rotation, uint8_t sck, uint8_t mosi,
                      uint8_t cs, uint8_t dc, uint8_t reset);

  bool begin();
  void sendBuffer();
  void updateDisplayArea(uint8_t tileX, uint8_t tileY,
                         uint8_t tileWidth, uint8_t tileHeight);
  void setPowerSave(uint8_t enabled);
  void setProfileLabel(const char *label);
  void setInkColor(uint32_t rgb888);
  void clearBuffer();
  void setDrawColor(uint8_t color);
  void drawHLine(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w);
  void drawFrame(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h);
  void drawBox(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h);
  void drawCircle(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t rad,
                  uint8_t opt = U8G2_DRAW_ALL);
  void drawDisc(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t rad,
                uint8_t opt = U8G2_DRAW_ALL);
  void drawLine(u8g2_uint_t x1, u8g2_uint_t y1,
                u8g2_uint_t x2, u8g2_uint_t y2);
  void drawXBMP(u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w,
                u8g2_uint_t h, const uint8_t *bitmap);
  u8g2_uint_t drawStr(u8g2_uint_t x, u8g2_uint_t y, const char *text);
  u8g2_uint_t drawUTF8(u8g2_uint_t x, u8g2_uint_t y, const char *text);
  u8g2_uint_t drawUTF8X2(u8g2_uint_t x, u8g2_uint_t y, const char *text);
  void colorizeMonochrome(uint32_t rgb888 = 0xFFFFFFUL);
  void loadRgb332(const uint8_t *pixels);
  void loadRgb565(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  const uint8_t *bigEndianPixels);
  void drawRgb332(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  const uint8_t *pixels);
  void drawRgb565(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  const uint8_t *bigEndianPixels);

 private:
  static constexpr uint16_t TFT_WIDTH = 128;
  static constexpr uint16_t TFT_HEIGHT = 160;
  static constexpr uint16_t FRAME_WIDTH = 128;
  static constexpr uint16_t FRAME_HEIGHT = 128;
  static constexpr uint16_t FRAME_Y = (TFT_HEIGHT - FRAME_HEIGHT) / 2;
  static constexpr uint32_t SPI_HZ = 16000000;

  uint8_t sck_;
  uint8_t mosi_;
  uint8_t cs_;
  uint8_t dc_;
  uint8_t reset_;
  bool powered_ = true;
  bool initialized_ = false;
  char profileLabel_[6] = "CORE";
  uint16_t *colorBuffer_ = nullptr;
  uint8_t previousMono_[FRAME_WIDTH * FRAME_HEIGHT / 8U] = {};
  uint16_t inkColor_ = 0xFFFF;
  uint8_t tone5_[32] = {};
  uint8_t tone6_[64] = {};
  SPISettings spiSettings_{SPI_HZ, MSBFIRST, SPI_MODE0};

  void hardwareReset();
  void command(uint8_t value, const uint8_t *data = nullptr, size_t length = 0);
  void initializeController();
  void clearPanel();
  void beginTransfer();
  void endTransfer();
  void setAddressWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
  void writeBufferArea(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
  void drawFrameChrome();
  static uint16_t glyph3x5(char value);
  static bool labelPixel(const char *text, int16_t originX,
                         uint16_t x, uint16_t y);
  static uint8_t toneChannel8(uint8_t value);
  static uint16_t rgb888To565(uint32_t rgb888);
  void initializeToneTables();
  uint16_t toneRgb565(uint16_t color) const;
  void beginColorCapture();
  void finishColorCapture();
};
