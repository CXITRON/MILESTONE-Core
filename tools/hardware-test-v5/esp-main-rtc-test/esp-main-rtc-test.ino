#include <Wire.h>

static constexpr int PIN_SDA = 41;
static constexpr int PIN_SCL = 42;
static constexpr uint8_t DS3231_ADDRESS = 0x68;

static bool readRegisters(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

static int fromBcd(uint8_t value) {
  return (value >> 4) * 10 + (value & 0x0F);
}

static void scanBus() {
  Serial.print("I2C scan:");
  bool found = false;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      found = true;
    }
  }
  Serial.println(found ? "" : " NONE");
  Serial.println("Expected: DS3231=0x68, optional 24C32 EEPROM=0x57");
}

static void printRtc() {
  uint8_t clockData[7];
  uint8_t status = 0;
  uint8_t temperatureData[2];
  if (!readRegisters(DS3231_ADDRESS, 0x00, clockData, sizeof(clockData))) {
    Serial.println("RTC FAIL: no response from 0x68 - check 3V3/GND/SDA/SCL soldering");
    return;
  }

  const bool statusRead = readRegisters(DS3231_ADDRESS, 0x0F, &status, 1);
  const bool temperatureRead = readRegisters(DS3231_ADDRESS, 0x11, temperatureData, 2);
  float temperature = NAN;
  if (temperatureRead) {
    temperature = static_cast<int8_t>(temperatureData[0]) +
                  ((temperatureData[1] >> 6) * 0.25f);
  }

  const int second = fromBcd(clockData[0] & 0x7F);
  const int minute = fromBcd(clockData[1] & 0x7F);
  const int hour = fromBcd(clockData[2] & 0x3F);
  const int day = fromBcd(clockData[4] & 0x3F);
  const int month = fromBcd(clockData[5] & 0x1F);
  const int year = 2000 + fromBcd(clockData[6]);

  Serial.printf("RTC PASS  %04d-%02d-%02d %02d:%02d:%02d  temp=%.2fC  OSF=%s\n",
                year, month, day, hour, minute, second, temperature,
                statusRead && (status & 0x80) ? "1 (time not trusted yet)" : "0");
}

void setup() {
  Serial.begin(115200);
  delay(700);
  Serial.println();
  Serial.println("MILESTONE v5 - ESP MAIN RTC soldering test");
  Serial.println("Wiring: GPIO41=SDA, GPIO42=SCL, 3V3=VCC, GND=GND");
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  scanBus();
}

void loop() {
  printRtc();
  delay(1000);
}
