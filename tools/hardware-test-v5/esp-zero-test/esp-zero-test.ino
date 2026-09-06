#include <Adafruit_NeoPixel.h>
#include <esp_heap_caps.h>
#include <driver/spi_slave.h>

#include "HwTestProtocol.h"

static constexpr int PIN_LINK_SCK = 9;
static constexpr int PIN_LINK_MOSI = 8;
static constexpr int PIN_LINK_MISO = 10;
static constexpr int PIN_LINK_CS = 7;
static constexpr int PIN_LINK_READY = 6;
static constexpr int PIN_RGB = 21;

Adafruit_NeoPixel rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
alignas(4) static HwTestPacket rxPacket;
alignas(4) static HwTestPacket txPacket;
static uint16_t responseSequence = 0;
static bool lastMasterPacketValid = false;
static uint32_t validPackets = 0;
static uint32_t badPackets = 0;

static void setLed(uint8_t red, uint8_t green, uint8_t blue) {
  rgb.setPixelColor(0, rgb.Color(red, green, blue));
  rgb.show();
}

static void prepareStatusPacket() {
  memset(&txPacket, 0, sizeof(txPacket));
  txPacket.magic = HW_TEST_MAGIC;
  txPacket.version = HW_TEST_VERSION;
  txPacket.type = HW_TEST_STATUS;
  txPacket.sequence = ++responseSequence;
  txPacket.uptimeMs = millis();
  txPacket.temperatureCenti = static_cast<int16_t>(temperatureRead() * 100.0f);
  txPacket.flags = (psramFound() ? HW_FLAG_PSRAM_FOUND : 0) |
                   (lastMasterPacketValid ? HW_FLAG_PACKET_VALID : 0);
  txPacket.freeHeap = ESP.getFreeHeap();
  txPacket.freePsram = ESP.getFreePsram();
  txPacket.nonce = rxPacket.nonce;
  hwTestFinalize(txPacket);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  rgb.begin();
  rgb.setBrightness(20);
  setLed(0, 0, 24);

  pinMode(PIN_LINK_READY, OUTPUT);
  digitalWrite(PIN_LINK_READY, LOW);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_LINK_MOSI;
  bus.miso_io_num = PIN_LINK_MISO;
  bus.sclk_io_num = PIN_LINK_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = sizeof(HwTestPacket);

  spi_slave_interface_config_t slave = {};
  slave.spics_io_num = PIN_LINK_CS;
  slave.flags = 0;
  slave.queue_size = 1;
  slave.mode = 0;

  const esp_err_t result = spi_slave_initialize(SPI2_HOST, &bus, &slave, SPI_DMA_CH_AUTO);
  Serial.println();
  Serial.println("MILESTONE v5 ESP ZERO hardware test");
  Serial.printf("SPI slave init: %s\n", esp_err_to_name(result));
  Serial.printf("PSRAM: %s, size=%u, free=%u\n", psramFound() ? "PASS" : "FAIL",
                ESP.getPsramSize(), ESP.getFreePsram());
  Serial.printf("Chip temperature: %.1f C\n", temperatureRead());
  Serial.println("Waiting for ESP MAIN packets...");
  if (result != ESP_OK) {
    setLed(28, 0, 0);
    while (true) delay(1000);
  }
}

void loop() {
  prepareStatusPacket();
  memset(&rxPacket, 0, sizeof(rxPacket));

  spi_slave_transaction_t transaction = {};
  transaction.length = sizeof(HwTestPacket) * 8;
  transaction.tx_buffer = &txPacket;
  transaction.rx_buffer = &rxPacket;

  digitalWrite(PIN_LINK_READY, HIGH);
  const esp_err_t result = spi_slave_transmit(SPI2_HOST, &transaction, pdMS_TO_TICKS(1000));
  digitalWrite(PIN_LINK_READY, LOW);

  if (result == ESP_OK) {
    lastMasterPacketValid = hwTestValid(rxPacket, HW_TEST_PING);
    if (lastMasterPacketValid) {
      ++validPackets;
      setLed(0, 24, 0);
      if ((validPackets % 20) == 1 && Serial.availableForWrite() >= 96) {
        Serial.printf("LINK PASS seq=%u nonce=%08lX valid=%lu bad=%lu\n",
                      rxPacket.sequence, static_cast<unsigned long>(rxPacket.nonce),
                      static_cast<unsigned long>(validPackets),
                      static_cast<unsigned long>(badPackets));
      }
    } else {
      ++badPackets;
      setLed(28, 8, 0);
      if (Serial.availableForWrite() >= 64) {
        Serial.printf("LINK BAD magic=%08lX type=%u crc=%08lX\n",
                      static_cast<unsigned long>(rxPacket.magic), rxPacket.type,
                      static_cast<unsigned long>(rxPacket.crc32));
      }
    }
  } else if (result != ESP_ERR_TIMEOUT) {
    setLed(28, 0, 0);
    if (Serial.availableForWrite() >= 48) {
      Serial.printf("SPI slave error: %s\n", esp_err_to_name(result));
    }
  } else {
    setLed(0, 0, 18);
  }
}
