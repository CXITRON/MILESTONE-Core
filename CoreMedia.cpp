#include "FirmwareProfile.h"

#if MILESTONE_HAS_MEDIA
#include "CoreMedia.h"

#include <string.h>
#include <stdlib.h>

namespace MilestoneMedia {
namespace {

void setError(Error *error, Error value) {
  if (error) *error = value;
}

bool decodeDelta(const uint8_t *data, size_t size, uint8_t *frame, size_t frameBytes) {
  size_t input = 0;
  size_t output = 0;
  while (input < size && output < frameBytes) {
    const uint8_t control = data[input++];
    const size_t run = static_cast<size_t>(control & 0x7FU) + 1U;
    if (run > frameBytes - output) return false;
    if ((control & 0x80U) == 0) {
      output += run;
      continue;
    }
    if (run > size - input) return false;
    for (size_t i = 0; i < run; ++i) frame[output + i] ^= data[input + i];
    input += run;
    output += run;
  }
  return input == size && output == frameBytes;
}

}  // namespace

uint16_t readLe16(const uint8_t *value) {
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(value[1]) << 8U);
}

uint32_t readLe32(const uint8_t *value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8U) |
         (static_cast<uint32_t>(value[2]) << 16U) |
         (static_cast<uint32_t>(value[3]) << 24U);
}

void writeLe16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void writeLe32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

bool parseHeader(const uint8_t *data, size_t size, Header &header, Error *error) {
  setError(error, Error::NONE);
  if (!data) {
    setError(error, Error::NULL_ARGUMENT);
    return false;
  }
  if (size < HEADER_BYTES || size > MAX_FILE_BYTES) {
    setError(error, Error::FILE_SIZE);
    return false;
  }
  if (memcmp(data, "MSM1", 4) != 0) {
    setError(error, Error::MAGIC);
    return false;
  }
  header.version = data[4];
  header.flags = data[5];
  header.width = data[6];
  header.height = data[7];
  header.frameCount = readLe16(data + 8);
  const uint16_t reserved = readLe16(data + 10);
  header.durationMs = readLe32(data + 12);
  header.payloadSize = readLe32(data + 16);
  header.payloadCrc32 = readLe32(data + 20);
  if (header.version != FORMAT_VERSION || reserved > 1U) {
    setError(error, Error::VERSION);
    return false;
  }
  header.color = reserved == 1U;
  if ((header.flags & ~KNOWN_FLAGS) != 0) {
    setError(error, Error::FLAGS);
    return false;
  }
  if (header.width != WIDTH || header.height != HEIGHT) {
    setError(error, Error::DIMENSIONS);
    return false;
  }
  if (header.frameCount == 0 || header.frameCount > MAX_FRAMES) {
    setError(error, Error::FRAME_COUNT);
    return false;
  }
  const bool animated = (header.flags & FLAG_ANIMATED) != 0;
  if (animated != (header.frameCount > 1)) {
    setError(error, Error::FLAGS);
    return false;
  }
  if (header.payloadSize != size - HEADER_BYTES) {
    setError(error, Error::PAYLOAD_SIZE);
    return false;
  }
  return true;
}

size_t frameBytes(const Header &header) {
  return header.color ? COLOR_FRAME_BYTES : FRAME_BYTES;
}

bool decodeFrame(const uint8_t *data, size_t size, bool firstFrame,
                 uint8_t *frame, uint16_t &delayMs, size_t &consumed,
                 Error *error) {
  return decodeFrameSized(data, size, firstFrame, frame, FRAME_BYTES,
                          delayMs, consumed, error);
}

bool decodeFrameSized(const uint8_t *data, size_t size, bool firstFrame,
                      uint8_t *frame, size_t decodedBytes, uint16_t &delayMs,
                      size_t &consumed, Error *error) {
  setError(error, Error::NONE);
  consumed = 0;
  delayMs = 0;
  if (!data || !frame) {
    setError(error, Error::NULL_ARGUMENT);
    return false;
  }
  if (size < FRAME_HEADER_BYTES) {
    setError(error, Error::FRAME_HEADER);
    return false;
  }
  const uint8_t encodingValue = data[0];
  delayMs = readLe16(data + 1);
  const uint16_t dataSize = readLe16(data + 3);
  if (static_cast<size_t>(dataSize) > size - FRAME_HEADER_BYTES) {
    setError(error, Error::FRAME_SIZE);
    return false;
  }
  if (delayMs > 60000U) {
    setError(error, Error::FRAME_DELAY);
    return false;
  }
  const Encoding encoding = static_cast<Encoding>(encodingValue);
  const uint8_t *payload = data + FRAME_HEADER_BYTES;
  if (encoding == Encoding::RAW) {
    if (dataSize != decodedBytes) {
      setError(error, Error::FRAME_SIZE);
      return false;
    }
    memcpy(frame, payload, decodedBytes);
  } else if (encoding == Encoding::XOR_RLE) {
    const size_t maximumEncoded = decodedBytes == FRAME_BYTES
        ? MAX_ENCODED_FRAME_BYTES : decodedBytes + 256U;
    if (dataSize > maximumEncoded) {
      setError(error, Error::FRAME_SIZE);
      return false;
    }
    if (firstFrame) {
      setError(error, Error::FRAME_ENCODING);
      return false;
    }
    if (!decodeDelta(payload, dataSize, frame, decodedBytes)) {
      setError(error, Error::DELTA_STREAM);
      return false;
    }
  } else {
    setError(error, Error::FRAME_ENCODING);
    return false;
  }
  consumed = FRAME_HEADER_BYTES + dataSize;
  return true;
}

bool parseLiveJpegRecord(const uint8_t *data, size_t size, size_t maximumBytes,
                         const uint8_t *&payload, uint16_t &payloadBytes,
                         size_t &consumed, Error *error) {
  setError(error, Error::NONE);
  payload = nullptr;
  payloadBytes = 0;
  consumed = 0;
  if (!data) {
    setError(error, Error::NULL_ARGUMENT);
    return false;
  }
  if (size < FRAME_HEADER_BYTES) {
    setError(error, Error::FRAME_HEADER);
    return false;
  }
  const uint16_t delayMs = readLe16(data + 1);
  const uint16_t dataSize = readLe16(data + 3);
  if (data[0] != LIVE_JPEG_ENCODING) {
    setError(error, Error::FRAME_ENCODING);
    return false;
  }
  if (delayMs != 0) {
    setError(error, Error::FRAME_DELAY);
    return false;
  }
  if (dataSize < 4U || dataSize > maximumBytes ||
      static_cast<size_t>(dataSize) > size - FRAME_HEADER_BYTES) {
    setError(error, Error::FRAME_SIZE);
    return false;
  }
  const uint8_t *jpeg = data + FRAME_HEADER_BYTES;
  if (jpeg[0] != 0xFFU || jpeg[1] != 0xD8U ||
      jpeg[dataSize - 2U] != 0xFFU || jpeg[dataSize - 1U] != 0xD9U) {
    setError(error, Error::FRAME_ENCODING);
    return false;
  }
  payload = jpeg;
  payloadBytes = dataSize;
  consumed = FRAME_HEADER_BYTES + dataSize;
  return true;
}

bool validateFile(const uint8_t *data, size_t size, Header *headerOut, Error *error) {
  Header header;
  if (!parseHeader(data, size, header, error)) return false;
  const uint8_t *payload = data + HEADER_BYTES;
  if (crc32(payload, header.payloadSize) != header.payloadCrc32) {
    setError(error, Error::PAYLOAD_CRC);
    return false;
  }

  const size_t decodedBytes = frameBytes(header);
  uint8_t *frame = static_cast<uint8_t *>(calloc(decodedBytes, 1U));
  if (!frame) {
    setError(error, Error::NULL_ARGUMENT);
    return false;
  }
  size_t offset = 0;
  uint64_t duration = 0;
  for (uint16_t i = 0; i < header.frameCount; ++i) {
    uint16_t delay = 0;
    size_t consumed = 0;
    if (!decodeFrameSized(payload + offset, header.payloadSize - offset, i == 0,
                          frame, decodedBytes, delay, consumed, error)) {
      free(frame);
      return false;
    }
    offset += consumed;
    duration += delay;
    if (duration > UINT32_MAX) {
      setError(error, Error::DURATION);
      return false;
    }
  }
  if (offset != header.payloadSize) {
    free(frame);
    setError(error, Error::PAYLOAD_SIZE);
    return false;
  }
  if (duration != header.durationMs) {
    free(frame);
    setError(error, Error::DURATION);
    return false;
  }
  if (header.frameCount == 1 && header.durationMs != 0) {
    free(frame);
    setError(error, Error::DURATION);
    return false;
  }
  if (header.frameCount > 1 && header.durationMs == 0) {
    free(frame);
    setError(error, Error::DURATION);
    return false;
  }
  free(frame);
  if (headerOut) *headerOut = header;
  setError(error, Error::NONE);
  return true;
}

bool shouldRotateStoredItem(size_t enabledItemCount, bool displayDeadlineReached,
                            bool animated, bool playbackFinished,
                            bool loopBoundaryReached) {
  if (enabledItemCount <= 1U || !displayDeadlineReached) return false;
  if (!animated) return true;
  return playbackFinished || loopBoundaryReached;
}

const char *errorName(Error error) {
  switch (error) {
    case Error::NONE: return "none";
    case Error::NULL_ARGUMENT: return "null_argument";
    case Error::FILE_SIZE: return "file_size";
    case Error::MAGIC: return "magic";
    case Error::VERSION: return "version";
    case Error::FLAGS: return "flags";
    case Error::DIMENSIONS: return "dimensions";
    case Error::FRAME_COUNT: return "frame_count";
    case Error::PAYLOAD_SIZE: return "payload_size";
    case Error::PAYLOAD_CRC: return "payload_crc";
    case Error::FRAME_HEADER: return "frame_header";
    case Error::FRAME_ENCODING: return "frame_encoding";
    case Error::FRAME_DELAY: return "frame_delay";
    case Error::FRAME_SIZE: return "frame_size";
    case Error::DELTA_STREAM: return "delta_stream";
    case Error::DURATION: return "duration";
  }
  return "unknown";
}

}  // namespace MilestoneMedia
#endif
