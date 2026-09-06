#pragma once

#include <Arduino.h>
#include <Wire.h>

struct RtcResult {
  bool found = false;
  bool oscillatorStopped = false;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

struct EnvironmentResult {
  bool found = false;
  bool aht20Present = false;
  bool aht20Found = false;
  bool bmp280Found = false;
  uint8_t aht20Error = 0;
  uint8_t address = 0;
  uint8_t chipId = 0;
  const char *name = "NONE";
  float temperatureC = NAN;
  float pressureHpa = NAN;
  float humidityPct = NAN;
};

inline bool i2cReadBytes(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

inline bool i2cWriteByte(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

inline bool i2cWriteCommand(uint8_t address, const uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(data, length);
  return Wire.endTransmission() == 0;
}

inline bool i2cReadRaw(uint8_t address, uint8_t *data, size_t length) {
  if (Wire.requestFrom(address, static_cast<uint8_t>(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

inline uint8_t aht20Crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x31) : uint8_t(crc << 1);
    }
  }
  return crc;
}

inline bool readAht20(float &temperatureC, float &humidityPct, uint8_t &error) {
  constexpr uint8_t address = 0x38;
  error = 0;
  const uint8_t statusCommand = 0x71;
  uint8_t status = 0;
  if (!i2cWriteCommand(address, &statusCommand, 1)) { error = 1; return false; }
  if (!i2cReadRaw(address, &status, 1)) { error = 2; return false; }
  if ((status & 0x08) == 0) {
    const uint8_t initialize[] = {0xBE, 0x08, 0x00};
    if (!i2cWriteCommand(address, initialize, sizeof(initialize))) { error = 3; return false; }
    delay(10);
  }

  const uint8_t measure[] = {0xAC, 0x33, 0x00};
  if (!i2cWriteCommand(address, measure, sizeof(measure))) { error = 4; return false; }
  delay(85);
  uint8_t raw[7];
  if (!i2cReadRaw(address, raw, sizeof(raw))) { error = 5; return false; }
  if (raw[0] & 0x80) { error = 6; return false; }
  if (aht20Crc8(raw, 6) != raw[6]) { error = 7; return false; }

  const uint32_t rawHumidity = (uint32_t(raw[1]) << 12) |
                               (uint32_t(raw[2]) << 4) | (raw[3] >> 4);
  const uint32_t rawTemperature = (uint32_t(raw[3] & 0x0F) << 16) |
                                  (uint32_t(raw[4]) << 8) | raw[5];
  humidityPct = rawHumidity * 100.0f / 1048576.0f;
  temperatureC = rawTemperature * 200.0f / 1048576.0f - 50.0f;
  const bool valid = isfinite(temperatureC) && isfinite(humidityPct) &&
                     temperatureC >= -50.0f && temperatureC <= 150.0f &&
                     humidityPct >= 0.0f && humidityPct <= 100.0f;
  if (!valid) error = 8;
  return valid;
}

inline int bcdToInt(uint8_t value) { return (value >> 4) * 10 + (value & 0x0F); }

inline RtcResult readDs3231() {
  RtcResult r;
  uint8_t data[7], status;
  if (!i2cReadBytes(0x68, 0x00, data, sizeof(data))) return r;
  r.found = true;
  r.second = bcdToInt(data[0] & 0x7F);
  r.minute = bcdToInt(data[1] & 0x7F);
  r.hour = bcdToInt(data[2] & 0x3F);
  r.day = bcdToInt(data[4] & 0x3F);
  r.month = bcdToInt(data[5] & 0x1F);
  r.year = 2000 + bcdToInt(data[6]);
  if (i2cReadBytes(0x68, 0x0F, &status, 1)) r.oscillatorStopped = status & 0x80;
  return r;
}

inline uint16_t u16le(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
inline int16_t s16le(const uint8_t *p) { return static_cast<int16_t>(u16le(p)); }

inline EnvironmentResult readBmeBmp280() {
  EnvironmentResult out;
  for (uint8_t address : {uint8_t(0x76), uint8_t(0x77)}) {
    uint8_t id;
    if (!i2cReadBytes(address, 0xD0, &id, 1)) continue;
    if (id != 0x60 && id != 0x58) continue;
    out.found = true; out.bmp280Found = true; out.address = address; out.chipId = id;
    out.name = id == 0x60 ? "BME280" : "BMP280";

    uint8_t cal[24], raw[8];
    if (!i2cReadBytes(address, 0x88, cal, sizeof(cal))) return out;
    if (id == 0x60) i2cWriteByte(address, 0xF2, 0x01);
    i2cWriteByte(address, 0xF5, 0xA0);
    i2cWriteByte(address, 0xF4, 0x27);
    delay(15);
    if (!i2cReadBytes(address, 0xF7, raw, id == 0x60 ? 8 : 6)) return out;

    const uint16_t T1 = u16le(cal + 0); const int16_t T2 = s16le(cal + 2);
    const int16_t T3 = s16le(cal + 4); const uint16_t P1 = u16le(cal + 6);
    const int16_t P2 = s16le(cal + 8), P3 = s16le(cal + 10), P4 = s16le(cal + 12);
    const int16_t P5 = s16le(cal + 14), P6 = s16le(cal + 16), P7 = s16le(cal + 18);
    const int16_t P8 = s16le(cal + 20), P9 = s16le(cal + 22);
    const int32_t adcP = (int32_t(raw[0]) << 12) | (int32_t(raw[1]) << 4) | (raw[2] >> 4);
    const int32_t adcT = (int32_t(raw[3]) << 12) | (int32_t(raw[4]) << 4) | (raw[5] >> 4);
    int32_t v1 = ((((adcT >> 3) - (int32_t(T1) << 1))) * int32_t(T2)) >> 11;
    int32_t v2 = (((((adcT >> 4) - int32_t(T1)) * ((adcT >> 4) - int32_t(T1))) >> 12) * int32_t(T3)) >> 14;
    const int32_t fine = v1 + v2;
    out.temperatureC = ((fine * 5 + 128) >> 8) / 100.0f;

    int64_t p1 = int64_t(fine) - 128000;
    int64_t p2 = p1 * p1 * P6; p2 += (p1 * P5) << 17; p2 += int64_t(P4) << 35;
    p1 = ((p1 * p1 * P3) >> 8) + ((p1 * P2) << 12);
    p1 = (((int64_t(1) << 47) + p1) * P1) >> 33;
    if (p1 != 0) {
      int64_t pressure = 1048576 - adcP;
      pressure = (((pressure << 31) - p2) * 3125) / p1;
      p1 = (int64_t(P9) * (pressure >> 13) * (pressure >> 13)) >> 25;
      p2 = (int64_t(P8) * pressure) >> 19;
      pressure = ((pressure + p1 + p2) >> 8) + (int64_t(P7) << 4);
      out.pressureHpa = pressure / 25600.0f;
    }

    if (id == 0x60) {
      uint8_t h1, hc[7];
      if (i2cReadBytes(address, 0xA1, &h1, 1) && i2cReadBytes(address, 0xE1, hc, 7)) {
        const int16_t H2 = s16le(hc); const uint8_t H3 = hc[2];
        const int16_t H4 = int16_t((int16_t(int8_t(hc[3])) << 4) | (hc[4] & 0x0F));
        const int16_t H5 = int16_t((int16_t(int8_t(hc[5])) << 4) | (hc[4] >> 4));
        const int8_t H6 = int8_t(hc[6]);
        const int32_t adcH = (int32_t(raw[6]) << 8) | raw[7];
        int32_t h = fine - 76800;
        h = (((((adcH << 14) - (int32_t(H4) << 20) - (int32_t(H5) * h)) + 16384) >> 15) *
             (((((((h * int32_t(H6)) >> 10) * (((h * int32_t(H3)) >> 11) + 32768)) >> 10) + 2097152) *
                int32_t(H2) + 8192) >> 14));
        h -= (((((h >> 15) * (h >> 15)) >> 7) * h1) >> 4);
        h = constrain(h, 0, 419430400);
        out.humidityPct = (h >> 12) / 1024.0f;
      }
    }
    return out;
  }
  return out;
}


inline EnvironmentResult readEnvironmentSensors() {
  float ahtTemperature = NAN, ahtHumidity = NAN;
  Wire.beginTransmission(0x38);
  const bool ahtPresent = Wire.endTransmission() == 0;
  uint8_t ahtError = 0;
  const bool ahtFound = ahtPresent && readAht20(ahtTemperature, ahtHumidity, ahtError);
  EnvironmentResult out;
  out.aht20Present = ahtPresent;
  out.aht20Found = ahtFound;
  out.aht20Error = ahtError;

  if (ahtFound) {
    out.found = true;
    out.temperatureC = ahtTemperature;
    out.humidityPct = ahtHumidity;
    out.address = 0x38;
    out.name = "AHT20";
  } else if (ahtPresent) {
    out.address = 0x38;
    out.name = "AHT20 ERR";
  }
  return out;
}
