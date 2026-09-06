#include <Adafruit_NeoPixel.h>
#include <SPI.h>

#include "HwTestProtocol.h"

static constexpr int PIN_LINK_SCK = 8;
static constexpr int PIN_LINK_MOSI = 9;
static constexpr int PIN_LINK_MISO = 16;
static constexpr int PIN_LINK_CS = 17;
static constexpr int PIN_LINK_READY = 18;
static constexpr int PIN_RGB = 48;

SPIClass linkSpi(HSPI);
Adafruit_NeoPixel rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

static uint16_t sequence = 0;
static uint32_t passCount = 0;
static uint32_t failCount = 0;
static uint32_t readyTimeoutCount = 0;
static uint32_t lastReportMs = 0;

static void setLed(uint8_t red, uint8_t green, uint8_t blue) {
  rgb.setPixelColor(0, rgb.Color(red, green, blue));
  rgb.show();
}

static bool exchangeWithZero(HwTestPacket &response) {
  if (!digitalRead(PIN_LINK_READY)) {
    ++readyTimeoutCount;
    return false;
  }

  HwTestPacket ping = {};
  ping.magic = HW_TEST_MAGIC;
  ping.version = HW_TEST_VERSION;
  ping.type = HW_TEST_PING;
  ping.sequence = ++sequence;
  ping.uptimeMs = millis();
  ping.temperatureCenti = static_cast<int16_t>(temperatureRead() * 100.0f);
  ping.flags = psramFound() ? HW_FLAG_PSRAM_FOUND : 0;
  ping.freeHeap = ESP.getFreeHeap();
  ping.freePsram = ESP.getFreePsram();
  ping.nonce = esp_random();
  hwTestFinalize(ping);

  linkSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LINK_CS, LOW);
  linkSpi.transferBytes(reinterpret_cast<uint8_t *>(&ping),
                        reinterpret_cast<uint8_t *>(&response), sizeof(response));
  digitalWrite(PIN_LINK_CS, HIGH);
  linkSpi.endTransaction();

  return hwTestValid(response, HW_TEST_STATUS);
}

void setup() {
  Serial.begin(115200);
  delay(700);

  rgb.begin();
  rgb.setBrightness(20);
  setLed(0, 0, 24);

  pinMode(PIN_LINK_CS, OUTPUT);
  digitalWrite(PIN_LINK_CS, HIGH);
  pinMode(PIN_LINK_READY, INPUT_PULLDOWN);
  linkSpi.begin(PIN_LINK_SCK, PIN_LINK_MISO, PIN_LINK_MOSI, PIN_LINK_CS);

  Serial.println();
  Serial.println("MILESTONE v5 ESP MAIN SPI-only test");
  Serial.println("SCK=8 MOSI=9 MISO=16 CS=17 READY=18");
  Serial.println("Waiting for ESP ZERO READY...");
}

void loop() {
  static uint32_t lastAttemptMs = 0;
  const uint32_t now = millis();
  if (now - lastAttemptMs < 100) {
    delay(1);
    return;
  }
  lastAttemptMs = now;

  HwTestPacket response = {};
  if (exchangeWithZero(response)) {
    ++passCount;
    setLed(0, 24, 0);
    Serial.printf("SPI PASS txSeq=%u zeroSeq=%u zeroTemp=%.2fC nonce=%08lX "
                  "pass=%lu fail=%lu readyWait=%lu\n",
                  sequence, response.sequence, response.temperatureCenti / 100.0f,
                  static_cast<unsigned long>(response.nonce),
                  static_cast<unsigned long>(passCount),
                  static_cast<unsigned long>(failCount),
                  static_cast<unsigned long>(readyTimeoutCount));
  } else {
    ++failCount;
    if (digitalRead(PIN_LINK_READY)) {
      setLed(28, 8, 0);
      Serial.printf("SPI FAIL invalid packet pass=%lu fail=%lu\n",
                    static_cast<unsigned long>(passCount),
                    static_cast<unsigned long>(failCount));
    } else if (now - lastReportMs >= 1000) {
      lastReportMs = now;
      setLed(24, 0, 0);
      Serial.printf("WAIT READY GPIO18=LOW pass=%lu fail=%lu readyWait=%lu\n",
                    static_cast<unsigned long>(passCount),
                    static_cast<unsigned long>(failCount),
                    static_cast<unsigned long>(readyTimeoutCount));
    }
  }
}
