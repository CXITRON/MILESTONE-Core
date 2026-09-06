#pragma once
#include <MilestoneV5Features.h>
#include <Wire.h>

// AHT20 forced conversion with bounded, non-blocking service phases.
class V5Environment {
public:
  MilestoneV5::EnvironmentTracker values{15000, {0, 0, 0}};
  uint8_t address = 0, chip = 0;
  uint32_t errors = 0;
  bool enabled = true, useFahrenheit = false;
  uint32_t intervalMs = 5000, logIntervalMs = 60000;
  uint8_t displayMask = 3;
  float warning[3] = {30, 70, 0}, danger[3] = {40, 85, 0};

  uint16_t color(unsigned field, float value) const {
    return value >= danger[field] ? 0xF800
                                  : value >= warning[field] ? 0xFD20 : 0xFFFF;
  }

  // The pressure argument remains only to read existing development settings.
  void configure(bool on, bool fahrenheit, uint32_t interval,
                 uint32_t logInterval, float t, float h, float) {
    enabled = on;
    useFahrenheit = fahrenheit;
    intervalMs = interval;
    logIntervalMs = logInterval;
    values = MilestoneV5::EnvironmentTracker(interval * 3, {t, h, 0});
    rescan();
  }

  void rescan() {
    address = 0;
    chip = 0;
    pending = initializing = searched = sampled = false;
  }

  void service(uint32_t now) {
    if (!enabled)
      return;
    if (!address) {
      if (searched && now - lastSearch < 30000)
        return;
      searched = true;
      lastSearch = now;
      uint8_t status = 0;
      if (!statusByte(status))
        return;
      address = 0x38;
      chip = 0x38;
      if ((status & 0x08) == 0) {
        const uint8_t initialize[] = {0xBE, 0x08, 0x00};
        if (!writeCommand(initialize, sizeof(initialize))) {
          fail();
          return;
        }
        initializing = true;
        started = now;
        return;
      }
    }
    if (initializing) {
      if (now - started < 10)
        return;
      initializing = false;
    }
    if (!pending) {
      if (sampled && now - lastSample < intervalMs)
        return;
      const uint8_t measure[] = {0xAC, 0x33, 0x00};
      if (!writeCommand(measure, sizeof(measure))) {
        fail();
        return;
      }
      sampled = true;
      lastSample = now;
      started = now;
      pending = true;
      return;
    }
    if (now - started < 85)
      return;
    uint8_t raw[7];
    if (!readRaw(raw, sizeof(raw))) {
      fail();
      return;
    }
    if (raw[0] & 0x80) {
      if (now - started > 150)
        fail();
      return;
    }
    pending = false;
    if (crc8(raw, 6) != raw[6]) {
      ++errors;
      return;
    }
    const uint32_t rh = uint32_t(raw[1]) << 12 | uint32_t(raw[2]) << 4 |
                        uint32_t(raw[3]) >> 4;
    const uint32_t rt = uint32_t(raw[3] & 0x0F) << 16 |
                        uint32_t(raw[4]) << 8 | raw[5];
    const MilestoneV5::EnvironmentSample sample{
        rt * 200.0f / 1048576.0f - 50.0f,
        rh * 100.0f / 1048576.0f,
        0.0f,
        true,
    };
    if (!values.accept(sample, MilestoneV5::detectEnvironmentSensor(0x38), now))
      ++errors;
  }

private:
  bool pending = false, initializing = false, searched = false,
       sampled = false;
  uint32_t lastSearch = 0, lastSample = 0, started = 0;

  static uint8_t crc8(const uint8_t *data, size_t length) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x31)
                           : uint8_t(crc << 1);
    }
    return crc;
  }

  void fail() {
    ++errors;
    address = 0;
    pending = initializing = false;
  }

  bool statusByte(uint8_t &status) {
    const uint8_t command = 0x71;
    Wire.beginTransmission(0x38);
    Wire.write(command);
    if (Wire.endTransmission() != 0 || Wire.requestFrom(0x38, uint8_t(1)) != 1)
      return false;
    status = Wire.read();
    return true;
  }

  bool writeCommand(const uint8_t *data, size_t length) {
    Wire.beginTransmission(0x38);
    Wire.write(data, length);
    return Wire.endTransmission() == 0;
  }

  bool readRaw(uint8_t *data, size_t length) {
    if (Wire.requestFrom(0x38, uint8_t(length)) != length)
      return false;
    for (size_t i = 0; i < length; ++i)
      data[i] = Wire.read();
    return true;
  }
};
