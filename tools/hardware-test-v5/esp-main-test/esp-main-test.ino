#include <Adafruit_NeoPixel.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "HwTestProtocol.h"
#include "SensorTest.h"
#include "SimpleSt7735.h"

static constexpr int PIN_TFT_SCK = 12;
static constexpr int PIN_TFT_MOSI = 11;
static constexpr int PIN_TFT_MISO = 13;  // SD only; TFT has no MISO
static constexpr int PIN_TFT_CS = 47;
static constexpr int PIN_TFT_DC = 21;
static constexpr int PIN_TFT_RESET = 6;
static constexpr int PIN_SD_CS = 10;
static constexpr int PIN_I2C_SDA = 41;
static constexpr int PIN_I2C_SCL = 42;
static constexpr int PIN_LINK_SCK = 8;
static constexpr int PIN_LINK_MOSI = 9;
static constexpr int PIN_LINK_MISO = 16;
static constexpr int PIN_LINK_CS = 17;
static constexpr int PIN_LINK_READY = 18;
static constexpr int PIN_RGB = 48;

struct ButtonState {
  const char *name;
  int pin;
  bool stable;
  bool raw;
  uint32_t changedAt;
  uint32_t flashUntil;
};

ButtonState buttons[] = {
  {"PREV", 4, true, true, 0, 0}, {"NEXT", 7, true, true, 0, 0},
  {"OK", 15, true, true, 0, 0}, {"BACK", 5, true, true, 0, 0},
  {"MODE", 14, true, true, 0, 0},
};

SPIClass linkSpi(HSPI);
SimpleSt7735 tft(SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RESET);
Adafruit_NeoPixel rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

static bool psramPass = false;
static bool sdPass = false;
static bool sdWritePass = false;
static uint64_t sdSizeMiB = 0;
static RtcResult rtc;
static EnvironmentResult environment;
static bool linkPass = false;
static uint32_t linkPassCount = 0, linkFailCount = 0;
static HwTestPacket zeroStatus = {};
static uint16_t linkSequence = 0;
static uint32_t lastSensorRead = 0, lastLinkAttempt = 0, lastLinkSuccess = 0;
static uint32_t lastDraw = 0, lastLog = 0;
static const uint16_t BLACK = 0x0000, WHITE = 0xFFFF, GREEN = 0x07E0;
static const uint16_t RED = 0xF800, YELLOW = 0xFFE0, CYAN = 0x07FF, BLUE = 0x001F;

static void setLed(uint8_t red, uint8_t green, uint8_t blue) {
  rgb.setPixelColor(0, rgb.Color(red, green, blue));
  rgb.show();
}

static bool testPsram() {
  if (!psramFound() || ESP.getPsramSize() == 0) return false;
  const size_t bytes = 256 * 1024;
  uint8_t *memory = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  if (!memory) return false;
  for (size_t i = 0; i < bytes; ++i) memory[i] = uint8_t((i * 37U) ^ (i >> 8));
  bool ok = true;
  for (size_t i = 0; i < bytes; ++i) {
    if (memory[i] != uint8_t((i * 37U) ^ (i >> 8))) { ok = false; break; }
  }
  free(memory);
  return ok;
}

static bool testSdCard() {
  digitalWrite(PIN_TFT_CS, HIGH);
  if (!SD.begin(PIN_SD_CS, SPI, 10000000)) return false;
  sdSizeMiB = SD.cardSize() / (1024ULL * 1024ULL);
  const char *path = "/MILESTONE_HW_TEST.TMP";
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) return true;
  uint8_t written[512];
  for (size_t i = 0; i < sizeof(written); ++i) written[i] = uint8_t(i ^ 0xA5);
  const bool wrote = file.write(written, sizeof(written)) == sizeof(written);
  file.flush(); file.close();
  uint8_t readback[512] = {};
  file = SD.open(path, FILE_READ);
  const bool read = file && file.read(readback, sizeof(readback)) == sizeof(readback);
  if (file) file.close();
  SD.remove(path);
  sdWritePass = wrote && read && memcmp(written, readback, sizeof(written)) == 0;
  return true;
}

static void scanI2c() {
  Serial.print("I2C scan:");
  bool any = false;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address); any = true;
    }
  }
  Serial.println(any ? "" : " NONE");
}

static bool exchangeWithZero() {
  if (!digitalRead(PIN_LINK_READY)) return false;
  HwTestPacket ping = {}, response = {};
  ping.magic = HW_TEST_MAGIC; ping.version = HW_TEST_VERSION; ping.type = HW_TEST_PING;
  ping.sequence = ++linkSequence; ping.uptimeMs = millis();
  ping.temperatureCenti = int16_t(temperatureRead() * 100.0f);
  ping.flags = psramPass ? HW_FLAG_PSRAM_FOUND : 0;
  ping.freeHeap = ESP.getFreeHeap(); ping.freePsram = ESP.getFreePsram();
  ping.nonce = esp_random(); hwTestFinalize(ping);

  linkSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LINK_CS, LOW);
  linkSpi.transferBytes(reinterpret_cast<uint8_t *>(&ping),
                        reinterpret_cast<uint8_t *>(&response), sizeof(response));
  digitalWrite(PIN_LINK_CS, HIGH);
  linkSpi.endTransaction();
  if (!hwTestValid(response, HW_TEST_STATUS)) return false;
  zeroStatus = response;
  return true;
}

static void updateButtons() {
  const uint32_t now = millis();
  for (ButtonState &button : buttons) {
    const bool raw = digitalRead(button.pin);
    if (raw != button.raw) { button.raw = raw; button.changedAt = now; }
    if (raw != button.stable && now - button.changedAt >= 25) {
      button.stable = raw;
      if (!raw) {
        button.flashUntil = now + 450;
        Serial.printf("BUTTON PASS: %s GPIO%d\n", button.name, button.pin);
      }
    }
  }
}

static void printStatus(const char *label, bool pass, int16_t y) {
  tft.setCursor(2, y); tft.setTextColor(WHITE, BLACK); tft.print(label);
  tft.setTextColor(pass ? GREEN : RED, BLACK); tft.print(pass ? "OK" : "FAIL");
}

static void drawDashboard() {
  tft.fillScreen(BLACK);
  tft.setTextSize(1); tft.setTextWrap(false);
  tft.setCursor(2, 2); tft.setTextColor(CYAN, BLACK); tft.print("MILESTONE HW TEST");
  tft.drawFastHLine(0, 11, 128, BLUE);
  printStatus("PSRAM ", psramPass, 15);
  tft.setTextColor(WHITE, BLACK); tft.printf(" %uMB", ESP.getPsramSize() / 1048576);
  printStatus("SD    ", sdPass && sdWritePass, 25);
  tft.setTextColor(WHITE, BLACK); tft.printf(" %lluMB", sdSizeMiB);
  printStatus("RTC   ", rtc.found, 35);
  if (rtc.found) {
    tft.setTextColor(rtc.oscillatorStopped ? YELLOW : WHITE, BLACK);
    tft.printf(" %02d:%02d:%02d", rtc.hour, rtc.minute, rtc.second);
  }
  printStatus("ENV   ", environment.found, 45);
  if (environment.aht20Present || environment.bmp280Found) {
    tft.setTextColor(WHITE, BLACK); tft.printf(" %s", environment.name);
  }
  if (environment.aht20Found || environment.bmp280Found) {
    tft.setCursor(2, 55); tft.setTextColor(WHITE, BLACK);
    tft.printf("%.1fC %.0f%%", environment.temperatureC,
               environment.humidityPct);
  }
  printStatus("SPI ZERO ", linkPass, 65);
  if (linkPass) {
    tft.setCursor(2, 75); tft.setTextColor(WHITE, BLACK);
    tft.printf("M %.1fC Z %.1fC", temperatureRead(), zeroStatus.temperatureCenti / 100.0f);
    tft.setCursor(2, 85); tft.printf("SEQ %u RX %lu", zeroStatus.sequence,
                                    static_cast<unsigned long>(linkPassCount));
  } else {
    tft.setCursor(2, 75); tft.setTextColor(YELLOW, BLACK); tft.print("Check ZERO/5 wires");
  }
  tft.drawFastHLine(0, 96, 128, BLUE);
  tft.setCursor(2, 100); tft.setTextColor(WHITE, BLACK); tft.print("PRESS ALL BUTTONS");
  int16_t x = 2;
  for (ButtonState &button : buttons) {
    const bool active = !button.stable || int32_t(button.flashUntil - millis()) > 0;
    tft.setCursor(x, 113); tft.setTextColor(active ? BLACK : WHITE, active ? GREEN : BLACK);
    const char shortName = button.name[0];
    tft.write(shortName); tft.write(' '); x += 24;
  }
  tft.setCursor(2, 127); tft.setTextColor(WHITE, BLACK); tft.print("P N O B M = GPIO");
  tft.drawFastHLine(0, 139, 128, BLUE);
  tft.setCursor(2, 144); tft.setTextColor(GREEN, BLACK); tft.print("TOP");
  tft.setCursor(98, 144); tft.setTextColor(YELLOW, BLACK); tft.print("BOTTOM");
}

static void showDisplaySelfTest() {
  const uint16_t colors[] = {RED, GREEN, BLUE, WHITE};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (size_t i = 0; i < 4; ++i) {
    tft.fillScreen(colors[i]);
    tft.setTextSize(2); tft.setTextColor(i == 3 ? BLACK : WHITE, colors[i]);
    tft.setCursor(30, 70); tft.print(names[i]); delay(350);
  }
}

void setup() {
  Serial.begin(115200); delay(700);
  Serial.println(); Serial.println("MILESTONE v5 ESP MAIN hardware test");
  Serial.println("This sketch does not modify production NVS settings.");

  rgb.begin(); rgb.setBrightness(20); setLed(0, 0, 24);
  pinMode(PIN_TFT_CS, OUTPUT); pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH); digitalWrite(PIN_SD_CS, HIGH);
  for (ButtonState &button : buttons) pinMode(button.pin, INPUT_PULLUP);
  pinMode(PIN_LINK_CS, OUTPUT); digitalWrite(PIN_LINK_CS, HIGH);
  pinMode(PIN_LINK_READY, INPUT_PULLDOWN);

  SPI.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI, -1);
  tft.begin(); showDisplaySelfTest();
  linkSpi.begin(PIN_LINK_SCK, PIN_LINK_MISO, PIN_LINK_MOSI, PIN_LINK_CS);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);

  psramPass = testPsram();
  sdPass = testSdCard();
  scanI2c();
  rtc = readDs3231();
  environment = readEnvironmentSensors();
  Serial.printf("PSRAM: %s size=%u free=%u\n", psramPass ? "PASS" : "FAIL",
                ESP.getPsramSize(), ESP.getFreePsram());
  Serial.printf("SD: mount=%s write/read=%s size=%llu MiB\n", sdPass ? "PASS" : "FAIL",
                sdWritePass ? "PASS" : "FAIL", sdSizeMiB);
  Serial.printf("RTC DS3231: %s OSF=%d %04d-%02d-%02d %02d:%02d:%02d\n",
                rtc.found ? "PASS" : "FAIL", rtc.oscillatorStopped, rtc.year, rtc.month,
                rtc.day, rtc.hour, rtc.minute, rtc.second);
  Serial.printf("ENV: %s AHT20=%s/error%u addr=0x%02X T=%.2fC H=%.1f%%\n",
                environment.name, environment.aht20Found ? "PASS" :
                                  (environment.aht20Present ? "PRESENT" : "FAIL"),
                environment.aht20Error, environment.address,
                environment.temperatureC, environment.humidityPct);
  setLed(24, 10, 0);
}

void loop() {
  const uint32_t now = millis();
  updateButtons();
  if (now - lastLinkAttempt >= 100) {
    lastLinkAttempt = now;
    const bool success = exchangeWithZero();
    if (success) { linkPass = true; lastLinkSuccess = now; ++linkPassCount; }
    else if (digitalRead(PIN_LINK_READY)) { ++linkFailCount; }
    if (linkPass && now - lastLinkSuccess > 3000) linkPass = false;
  }
  if (now - lastSensorRead >= 1000) {
    lastSensorRead = now; rtc = readDs3231(); environment = readEnvironmentSensors();
  }
  if (now - lastDraw >= 500) { lastDraw = now; drawDashboard(); }
  if (now - lastLog >= 2000) {
    lastLog = now;
    Serial.printf("LIVE main=%.1fC zero=%s%.1fC link=%lu/%lu RTC=%s ENV=%s AHT=%s/E%u T=%.1fC H=%.0f%% SD=%s buttons=",
                  temperatureRead(), linkPass ? "" : "OFF/", zeroStatus.temperatureCenti / 100.0f,
                  static_cast<unsigned long>(linkPassCount), static_cast<unsigned long>(linkFailCount),
                  rtc.found ? "OK" : "FAIL", environment.found ? environment.name : "FAIL",
                  environment.aht20Found ? "OK" : (environment.aht20Present ? "PRESENT" : "FAIL"),
                  environment.aht20Error, environment.temperatureC,
                  environment.humidityPct,
                  sdPass && sdWritePass ? "OK" : "FAIL");
    for (ButtonState &button : buttons) Serial.printf("%s:%d ", button.name, !button.stable);
    Serial.println();
    setLed(linkPass && psramPass ? 0 : 24, sdPass && rtc.found ? 20 : 4,
           linkPass ? 0 : 12);
  }
  delay(1);
}
