#include "CoreMedia.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { \
  std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expr << '\n'; ++failures; \
} } while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_EQ(a, b) do { const auto av = (a); const auto bv = (b); if (!(av == bv)) { \
  std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #a " == " #b \
            << " (" << static_cast<unsigned long long>(av) << " vs " \
            << static_cast<unsigned long long>(bv) << ")\n"; ++failures; \
} } while (0)

void appendLe16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

void appendFrame(std::vector<uint8_t> &payload, uint8_t encoding, uint16_t delay,
                 const std::vector<uint8_t> &data) {
  payload.push_back(encoding);
  appendLe16(payload, delay);
  appendLe16(payload, static_cast<uint16_t>(data.size()));
  payload.insert(payload.end(), data.begin(), data.end());
}

std::vector<uint8_t> makeFile(const std::vector<uint8_t> &payload, uint16_t frames,
                              uint32_t duration, uint8_t flags, bool color = false) {
  std::vector<uint8_t> file(MilestoneMedia::HEADER_BYTES, 0);
  std::memcpy(file.data(), "MSM1", 4);
  file[4] = MilestoneMedia::FORMAT_VERSION;
  file[5] = flags;
  file[6] = MilestoneMedia::WIDTH;
  file[7] = MilestoneMedia::HEIGHT;
  MilestoneMedia::writeLe16(file.data() + 8, frames);
  MilestoneMedia::writeLe16(file.data() + 10, color ? 1 : 0);
  MilestoneMedia::writeLe32(file.data() + 12, duration);
  MilestoneMedia::writeLe32(file.data() + 16, static_cast<uint32_t>(payload.size()));
  MilestoneMedia::writeLe32(file.data() + 20, MilestoneMedia::crc32(payload.data(), payload.size()));
  file.insert(file.end(), payload.begin(), payload.end());
  return file;
}

void testColorStillImage() {
  std::vector<uint8_t> pixels(MilestoneMedia::COLOR_FRAME_BYTES, 0xE3);
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 0, pixels);
  const std::vector<uint8_t> file = makeFile(payload, 1, 0, 0, true);
  MilestoneMedia::Header header{};
  MilestoneMedia::Error error = MilestoneMedia::Error::MAGIC;
  EXPECT_TRUE(MilestoneMedia::validateFile(file.data(), file.size(), &header, &error));
  EXPECT_TRUE(header.color);
  EXPECT_EQ(MilestoneMedia::frameBytes(header), MilestoneMedia::COLOR_FRAME_BYTES);
  std::vector<uint8_t> decoded(MilestoneMedia::COLOR_FRAME_BYTES);
  uint16_t delay = 1;
  size_t consumed = 0;
  EXPECT_TRUE(MilestoneMedia::decodeFrameSized(
      payload.data(), payload.size(), true, decoded.data(), decoded.size(),
      delay, consumed, &error));
  EXPECT_EQ(decoded[0], 0xE3);
  EXPECT_EQ(decoded.back(), 0xE3);
}

std::vector<uint8_t> rawFrame(uint8_t value) {
  return std::vector<uint8_t>(MilestoneMedia::FRAME_BYTES, value);
}

void testStillImage() {
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 0, rawFrame(0xA5));
  const std::vector<uint8_t> file = makeFile(payload, 1, 0, 0);
  MilestoneMedia::Header header{};
  MilestoneMedia::Error error = MilestoneMedia::Error::MAGIC;
  EXPECT_TRUE(MilestoneMedia::validateFile(file.data(), file.size(), &header, &error));
  EXPECT_EQ(header.frameCount, 1);
  EXPECT_EQ(header.payloadSize, payload.size());
  EXPECT_EQ(error, MilestoneMedia::Error::NONE);
}

void testAnimationDelta() {
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 125, rawFrame(0));
  std::vector<uint8_t> delta;
  // 128-byte zero runs cover the first 1024 bytes, one literal changes two
  // bytes, then the remaining bytes are skipped.
  for (int i = 0; i < 8; ++i) delta.push_back(127);
  delta.push_back(0x81);
  delta.push_back(0x01);
  delta.push_back(0x80);
  for (int i = 0; i < 7; ++i) delta.push_back(127);
  delta.push_back(125);
  appendFrame(payload, 1, 125, delta);
  const std::vector<uint8_t> file = makeFile(payload, 2, 250,
      MilestoneMedia::FLAG_ANIMATED | MilestoneMedia::FLAG_LOOP);
  EXPECT_TRUE(MilestoneMedia::validateFile(file.data(), file.size()));

  uint8_t frame[MilestoneMedia::FRAME_BYTES] = {};
  uint16_t delay = 0;
  size_t consumed = 0;
  EXPECT_TRUE(MilestoneMedia::decodeFrame(payload.data(), payload.size(), true,
                                          frame, delay, consumed));
  size_t second = consumed;
  EXPECT_TRUE(MilestoneMedia::decodeFrame(payload.data() + second, payload.size() - second,
                                          false, frame, delay, consumed));
  EXPECT_EQ(frame[1024], 0x01);
  EXPECT_EQ(frame[1025], 0x80);
}

void testCorruptionRejected() {
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 0, rawFrame(0x3C));
  std::vector<uint8_t> file = makeFile(payload, 1, 0, 0);
  MilestoneMedia::Error error = MilestoneMedia::Error::NONE;

  file[0] = 'X';
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::MAGIC);
  file[0] = 'M';

  file.back() ^= 0x01;
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::PAYLOAD_CRC);
  file.back() ^= 0x01;

  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size() - 1, nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::PAYLOAD_SIZE);
}

void testInvalidDeltaRejected() {
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 100, rawFrame(0));
  appendFrame(payload, 1, 100, std::vector<uint8_t>{0xFF, 0x01});
  const std::vector<uint8_t> file = makeFile(payload, 2, 200, MilestoneMedia::FLAG_ANIMATED);
  MilestoneMedia::Error error = MilestoneMedia::Error::NONE;
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::DELTA_STREAM);
}

void testMetadataRules() {
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 0, rawFrame(0));
  std::vector<uint8_t> file = makeFile(payload, 1, 0, MilestoneMedia::FLAG_ANIMATED);
  MilestoneMedia::Error error = MilestoneMedia::Error::NONE;
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::FLAGS);

  file = makeFile(payload, 1, 1, 0);
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::DURATION);
}

void testHeaderAndFrameBounds() {
  std::vector<uint8_t> payload;
  appendFrame(payload, 0, 0, rawFrame(0));
  std::vector<uint8_t> file = makeFile(payload, 1, 0, 0);
  MilestoneMedia::Error error = MilestoneMedia::Error::NONE;

  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), MilestoneMedia::HEADER_BYTES - 1,
                                            nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::FILE_SIZE);

  file = makeFile(payload, 1, 0, 0);
  file[4] = 2;
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::VERSION);

  file = makeFile(payload, 1, 0, 0);
  file[5] = 0x80;
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::FLAGS);

  file = makeFile(payload, 1, 0, 0);
  file[6] = 127;
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::DIMENSIONS);

  file = makeFile(payload, 1, 0, 0);
  MilestoneMedia::writeLe16(file.data() + 8, 0);
  EXPECT_FALSE(MilestoneMedia::validateFile(file.data(), file.size(), nullptr, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::FRAME_COUNT);

  std::vector<uint8_t> oversized(MilestoneMedia::MAX_ENCODED_FRAME_BYTES + 1U, 0);
  uint8_t frame[MilestoneMedia::FRAME_BYTES] = {};
  uint16_t delay = 0;
  size_t consumed = 0;
  std::vector<uint8_t> record;
  appendFrame(record, 1, 100, oversized);
  EXPECT_FALSE(MilestoneMedia::decodeFrame(record.data(), record.size(), false, frame,
                                           delay, consumed, &error));
  EXPECT_EQ(error, MilestoneMedia::Error::FRAME_SIZE);
}

void testStoredPlaybackRotationBoundary() {
  using MilestoneMedia::shouldRotateStoredItem;
  EXPECT_FALSE(shouldRotateStoredItem(1, true, true, false, true));
  EXPECT_FALSE(shouldRotateStoredItem(2, false, true, false, true));
  EXPECT_FALSE(shouldRotateStoredItem(2, true, true, false, false));
  EXPECT_TRUE(shouldRotateStoredItem(2, true, true, false, true));
  EXPECT_TRUE(shouldRotateStoredItem(2, true, true, true, false));
  EXPECT_TRUE(shouldRotateStoredItem(2, true, false, false, false));
}

}  // namespace

int main() {
  testStillImage();
  testColorStillImage();
  testAnimationDelta();
  testCorruptionRejected();
  testInvalidDeltaRejected();
  testMetadataRules();
  testHeaderAndFrameBounds();
  testStoredPlaybackRotationBoundary();
  if (failures) return EXIT_FAILURE;
  std::cout << "All MILESTONE media tests passed\n";
  return EXIT_SUCCESS;
}
