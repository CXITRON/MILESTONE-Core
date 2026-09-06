#pragma once

#include <Arduino.h>

static constexpr uint32_t HW_TEST_MAGIC = 0x4D485735UL;
static constexpr uint8_t HW_TEST_VERSION = 1;

enum HwTestPacketType : uint8_t { HW_TEST_PING = 1, HW_TEST_STATUS = 2 };
enum HwTestFlags : uint16_t {
  HW_FLAG_PSRAM_FOUND = 1U << 0,
  HW_FLAG_PACKET_VALID = 1U << 1,
};

struct __attribute__((packed)) HwTestPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t sequence;
  uint32_t uptimeMs;
  int16_t temperatureCenti;
  uint16_t flags;
  uint32_t freeHeap;
  uint32_t freePsram;
  uint32_t nonce;
  uint32_t crc32;
};

static_assert(sizeof(HwTestPacket) == 32, "Hardware test packet size changed");

inline uint32_t hwTestCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

inline void hwTestFinalize(HwTestPacket &packet) {
  packet.crc32 = hwTestCrc32(reinterpret_cast<const uint8_t *>(&packet),
                            sizeof(packet) - sizeof(packet.crc32));
}

inline bool hwTestValid(const HwTestPacket &packet, uint8_t expectedType) {
  return packet.magic == HW_TEST_MAGIC && packet.version == HW_TEST_VERSION &&
         packet.type == expectedType &&
         packet.crc32 == hwTestCrc32(reinterpret_cast<const uint8_t *>(&packet),
                                     sizeof(packet) - sizeof(packet.crc32));
}
