#pragma once

#include <stddef.h>
#include <stdint.h>

namespace MilestoneMedia {

constexpr uint8_t FORMAT_VERSION = 1;
constexpr uint8_t WIDTH = 128;
constexpr uint8_t HEIGHT = 128;
constexpr size_t FRAME_BYTES = static_cast<size_t>(WIDTH) * HEIGHT / 8U;
constexpr size_t HEADER_BYTES = 24;
constexpr size_t FRAME_HEADER_BYTES = 5;
constexpr size_t MAX_ENCODED_FRAME_BYTES = FRAME_BYTES + 32U;
constexpr size_t MAX_FILE_BYTES = 160U * 1024U;
constexpr uint16_t MAX_FRAMES = 1024;

constexpr uint8_t FLAG_ANIMATED = 0x01;
constexpr uint8_t FLAG_LOOP = 0x02;
constexpr uint8_t KNOWN_FLAGS = FLAG_ANIMATED | FLAG_LOOP;

enum class Encoding : uint8_t {
  RAW = 0,
  XOR_RLE = 1
};

enum class Error : uint8_t {
  NONE,
  NULL_ARGUMENT,
  FILE_SIZE,
  MAGIC,
  VERSION,
  FLAGS,
  DIMENSIONS,
  FRAME_COUNT,
  PAYLOAD_SIZE,
  PAYLOAD_CRC,
  FRAME_HEADER,
  FRAME_ENCODING,
  FRAME_DELAY,
  FRAME_SIZE,
  DELTA_STREAM,
  DURATION
};

struct Header {
  uint8_t version;
  uint8_t flags;
  uint8_t width;
  uint8_t height;
  uint16_t frameCount;
  uint32_t durationMs;
  uint32_t payloadSize;
  uint32_t payloadCrc32;
};

uint16_t readLe16(const uint8_t *value);
uint32_t readLe32(const uint8_t *value);
void writeLe16(uint8_t *out, uint16_t value);
void writeLe32(uint8_t *out, uint32_t value);
uint32_t crc32(const uint8_t *data, size_t size);

bool parseHeader(const uint8_t *data, size_t size, Header &header, Error *error = nullptr);

bool decodeFrame(const uint8_t *data, size_t size, bool firstFrame,
                 uint8_t *frame, uint16_t &delayMs, size_t &consumed,
                 Error *error = nullptr);

bool validateFile(const uint8_t *data, size_t size, Header *header = nullptr,
                  Error *error = nullptr);

// Stored animations must never be replaced halfway through a frame cycle.
// A single enabled item is left to its own loop/finished state instead of
// being needlessly reopened at the configured catalog display interval.
bool shouldRotateStoredItem(size_t enabledItemCount, bool displayDeadlineReached,
                            bool animated, bool playbackFinished,
                            bool loopBoundaryReached);

const char *errorName(Error error);

}  // namespace MilestoneMedia
