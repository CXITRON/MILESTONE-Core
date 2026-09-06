#pragma once
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Video.h>
#include <SD.h>
#include <cstring>

// Rebuildable A/B catalogue. The previous generation remains valid until the
// complete new catalogue has been read back. Cache files remain authoritative.
class V5ArtworkIndex {
public:
  bool known = false;
  uint64_t bytes = 0;
  uint32_t images = 0;
  void service() {
    if (!started) {
      started = true;
      uint32_t a = 0, b = 0;
      bool av = header("/now/art-index-a", a),
           bv = header("/now/art-index-b", b);
      activeB = !av || (bv && int32_t(b - a) > 0);
      if (av || bv) {
        if (!verify(activeB ? "/now/art-index-b" : "/now/art-index-a"))
          invalid();
      } else
        bootDone = true;
      return;
    }
    if (!checking)
      return;
    uint8_t block[1600];
    size_t take = min(uint32_t(sizeof(block)), remaining);
    if (take) {
      if (input.read(block, take) != take) {
        invalid();
        return;
      }
      for (size_t at = 0; at < take; at += 80) {
        for (unsigned i = 0; i < 64; ++i)
          if (!((block[at + i] >= '0' && block[at + i] <= '9') ||
                (block[at + i] >= 'a' && block[at + i] <= 'f'))) {
            invalid();
            return;
          }
        if (block[at + 64] > 7) {
          invalid();
          return;
        }
        uint32_t size = MilestoneV5::readVideoU32(block + at + 68);
        checkedBytes += size;
        if (size)
          ++checkedImages;
      }
      crc = extend(crc, block, take);
      remaining -= take;
      return;
    }
    uint8_t footer[4];
    if (input.read(footer, 4) != 4 ||
        MilestoneV5::readVideoU32(footer) != (crc ^ 0xFFFFFFFFU) ||
        checkedBytes != expectedBytes || checkedImages != expectedImages) {
      invalid();
      return;
    }
    input.close();
    checking = false;
    if (publishing) {
      const char *target = activeB ? "/now/art-index-a" : "/now/art-index-b";
      if ((SD.exists(target) && !SD.remove(target)) ||
          !SD.rename("/now/art-index.tmp", target)) {
        publishing = false;
        return;
      }
      activeB = !activeB;
      publishing = false;
    }
    generation = checkingGeneration;
    bytes = expectedBytes;
    images = expectedImages;
    known = true;
    bootDone = true;
  }
  bool beginSnapshot() {
    if (!bootDone || checking || writing)
      return false;
    const char *path = "/now/art-index.tmp";
    if (SD.exists(path) && !SD.remove(path))
      return false;
    output = SD.open(path, FILE_WRITE);
    if (!output)
      return false;
    uint8_t h[40]{};
    if (output.write(h, sizeof(h)) != sizeof(h)) {
      output.close();
      return false;
    }
    writing = true;
    count = writtenImages = 0;
    writtenBytes = 0;
    writeCrc = 0xFFFFFFFFU;
    return true;
  }
  void add(const String &key, uint8_t state, uint32_t size, uint64_t accessed) {
    if (!writing)
      return;
    if (key.length() != 64 || count >= 200000) {
      abort();
      return;
    }
    uint8_t r[80]{};
    memcpy(r, key.c_str(), 64);
    r[64] = state;
    put(r + 68, size);
    put(r + 72, accessed);
    put(r + 76, accessed >> 32);
    if (output.write(r, sizeof(r)) != sizeof(r)) {
      abort();
      return;
    }
    writeCrc = extend(writeCrc, r, sizeof(r));
    ++count;
    writtenBytes += size;
    if (size)
      ++writtenImages;
  }
  void finish() {
    if (!writing)
      return;
    uint8_t h[40]{}, footer[4];
    put(footer, writeCrc ^ 0xFFFFFFFFU);
    bool ok = output.write(footer, 4) == 4;
    memcpy(h, "VI01", 4);
    put(h + 4, generation + 1);
    put(h + 8, count);
    put(h + 12, writtenImages);
    put(h + 16, writtenBytes);
    put(h + 20, writtenBytes >> 32);
    put(h + 36, MilestoneV5::crc32(h, 36));
    ok = ok && output.seek(0) && output.write(h, sizeof(h)) == sizeof(h);
    output.flush();
    output.close();
    writing = false;
    if (ok) {
      publishing = true;
      if (!verify("/now/art-index.tmp"))
        publishing = false;
    }
  }
  void abort() {
    output.close();
    writing = false;
  }

private:
  File input, output;
  bool started = false, bootDone = false, activeB = false, checking = false,
       publishing = false, writing = false, fallbackTried = false;
  uint32_t generation = 0, checkingGeneration = 0, remaining = 0, crc = 0,
           writeCrc = 0, count = 0, writtenImages = 0, checkedImages = 0,
           expectedImages = 0;
  uint64_t writtenBytes = 0, checkedBytes = 0, expectedBytes = 0;
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (i * 8);
  }
  static uint32_t extend(uint32_t crc, const uint8_t *p, size_t n) {
    while (n--) {
      crc ^= *p++;
      for (unsigned i = 0; i < 8; ++i)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0);
    }
    return crc;
  }
  bool header(const char *path, uint32_t &gen) {
    File f = SD.open(path, FILE_READ);
    uint8_t h[40];
    if (!f || f.read(h, 40) != 40 || memcmp(h, "VI01", 4) ||
        MilestoneV5::crc32(h, 36) != MilestoneV5::readVideoU32(h + 36))
      return false;
    uint32_t n = MilestoneV5::readVideoU32(h + 8);
    if (n > 200000 || f.size() != 44ULL + uint64_t(n) * 80)
      return false;
    gen = MilestoneV5::readVideoU32(h + 4);
    return true;
  }
  bool verify(const char *path) {
    if (!header(path, checkingGeneration))
      return false;
    input = SD.open(path, FILE_READ);
    uint8_t h[40];
    if (input.read(h, 40) != 40) {
      input.close();
      return false;
    }
    remaining = MilestoneV5::readVideoU32(h + 8) * 80;
    expectedImages = MilestoneV5::readVideoU32(h + 12);
    expectedBytes = uint64_t(MilestoneV5::readVideoU32(h + 16)) |
                    (uint64_t(MilestoneV5::readVideoU32(h + 20)) << 32);
    checkedBytes = checkedImages = 0;
    crc = 0xFFFFFFFFU;
    checking = true;
    return true;
  }
  void invalid() {
    input.close();
    checking = false;
    if (publishing) {
      publishing = false;
      return;
    }
    if (!fallbackTried) {
      fallbackTried = true;
      activeB = !activeB;
      if (verify(activeB ? "/now/art-index-b" : "/now/art-index-a"))
        return;
    }
    bootDone = true;
    known = false;
  }
};
