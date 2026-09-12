#pragma once
#include "V5ArtworkIndex.h"
#ifndef MILESTONE_V5_TFT_DECLARED
#include "V5Tft.h"
#endif
#include <MilestoneV5Artwork.h>
#include <MilestoneV5Now.h>
#include <MilestoneV5Video.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>

class V5Artwork {
public:
  bool visible = false;
  uint32_t received = 0;
  uint8_t *packet = nullptr;
  uint8_t stage = 0; // 0 idle, 1 request, 2 polling/chunks
  String key;
  uint32_t generation = 0;
  uint64_t cacheBytes = 0;
  uint32_t cacheCount = 0;
  bool cacheKnown = false;
  bool manual = false;
  String lastError;
  String lastRequestKey, lastRequestResult;
  bool persisted = false;
  String storageStatus;
  uint32_t saveFailures = 0;
  V5ArtworkIndex index;
  static bool validKey(const String &s) {
    if (s.length() != 64)
      return false;
    for (unsigned i = 0; i < 64; ++i)
      if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
        return false;
    return true;
  }
  void invalidate() {
    if (manual)
      lastRequestResult = lastError.length() ? lastError : String("완료");
    key = "";
    stage = 0;
    visible = false;
    attempted = false;
    cacheRetryAt = 0;
    manual = false;
    received = 0;
    persisted = false;
    if (++generation == 0)
      ++generation;
  }
  void requestRecount() {
    scan.close();
    index.abort();
    cacheKnown = false;
  }
  bool queueRefresh(const String &id) {
    if (!validKey(id) || manual || !cacheKnown)
      return false;
    MilestoneV5::NowMetadata next{};
    if (id == key)
      next = track;
    else {
      File f = SD.open(String("/now/art-cache/") + id + ".meta", FILE_READ);
      uint8_t bytes[444];
      if (!f || f.size() > sizeof(bytes))
        return false;
      size_t n = f.size();
      if (f.read(bytes, n) != n || !MilestoneV5::decodeNow(bytes, n, next))
        return false;
    }
    if (!next.title[0])
      return false;
    if (!packet)
      packet = static_cast<uint8_t *>(heap_caps_malloc(
          MilestoneV5::kArtworkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!packet)
      return false;
    key = id;
    track = next;
    stage = 1;
    received = 0;
    persisted = false;
    visible = false;
    manual = true;
    attempted = true;
    lastError = "";
    lastRequestKey = id;
    lastRequestResult = "대기 / 다운로드";
    requestStarted = millis();
    if (++generation == 0)
      ++generation;
    return true;
  }
  void maintain(uint32_t now, bool mounted) {
    if (manual && now - requestStarted > 60000) {
      lastError = "Manual artwork timeout";
      invalidate();
    }
    if (!mounted) {
      scan.close();
      cacheKnown = false;
      return;
    }
    if (visible && !persisted && packet && received == MilestoneV5::kArtworkBytes &&
        now - lastSaveAttempt >= 2000)
      commit();
    index.service();
    if (!indexRestored && index.known) {
      indexRestored = true;
      if (!cacheKnown) {
        cacheBytes = index.bytes;
        cacheCount = index.images;
      }
    }
    if (!scan) {
      if (cacheKnown && now - lastScan < 60000)
        return;
      scan = SD.open("/now/art-cache");
      if (!scan)
        return;
      index.beginSnapshot();
      scannedBytes = 0;
      scannedCount = 0;
      oldest = "";
      oldestTime = UINT64_MAX;
    }
    for (unsigned i = 0; i < 4; ++i) {
      File f = scan.openNextFile();
      if (!f) {
        scan.close();
        index.finish();
        cacheBytes = scannedBytes;
        cacheCount = scannedCount;
        cacheKnown = true;
        lastScan = now;
        // Only the artwork cache budget permits automatic eviction. Low free
        // SD space blocks new cache writes; it must not delete existing art.
        if ((cacheBytes + (needSpace ? MilestoneV5::kArtworkBytes : 0) >
                 2ULL * 1024 * 1024 * 1024) &&
            oldest.length()) {
          String base = String("/now/art-cache/") + oldest;
          if (!SD.exists(base + ".custom") && SD.remove(base + ".mac")) {
            Serial0.println(String("ART cache evicted: limit exceeded key=") + oldest);
            SD.remove(base + ".use");
            cacheKnown = false;
          }
        }
        if ((visible || (manual && stage == 3)) && packet &&
            received == MilestoneV5::kArtworkBytes) {
          commit();
          if (manual && SD.exists(path(".mac")))
            invalidate();
        }
        return;
      }
      String name = f.name();
      bool metadata = name.endsWith(".meta");
      if (f.isDirectory() || (!name.endsWith(".mac") && !metadata)) {
        f.close();
        continue;
      }
      String id = name.substring(0, name.length() - (metadata ? 5 : 4));
      if (!validKey(id)) {
        f.close();
        continue;
      }
      uint32_t size = metadata ? 0 : f.size();
      uint64_t accessed = f.getLastWrite();
      f.close();
      String base = String("/now/art-cache/") + id;
      if (metadata && SD.exists(base + ".mac"))
        continue;
      scannedBytes += size;
      if (size)
        ++scannedCount;
      bool custom = SD.exists(base + ".custom");
      bool blocked = SD.exists(base + ".blocked");
      File used = SD.open(base + ".use", FILE_READ);
      if (used)
        accessed = used.getLastWrite();
      used.close();
      index.add(id, (size ? 1 : 0) | (custom ? 2 : 0) | (blocked ? 4 : 0), size,
                accessed);
      if (!size || id == key || custom)
        continue;
      if (accessed < oldestTime) {
        oldestTime = accessed;
        oldest = id;
      }
    }
  }
  void observe(const MilestoneV5::NowMetadata &m, uint32_t now, bool mounted) {
    if (manual)
      return;
    uint8_t bytes[444];
    size_t size;
    MilestoneV5::NowMetadata identity = m;
    identity.elapsedSeconds = identity.durationSeconds = 0;
    identity.connected = identity.ready = identity.playing = false;
    if (!MilestoneV5::encodeNow(identity, bytes, sizeof(bytes), size))
      return;
    uint8_t digest[32];
    mbedtls_sha256(bytes, size, digest, 0);
    char hex[65];
    for (unsigned i = 0; i < 32; ++i)
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (key != hex) {
      // A rendered download is not necessarily durable yet. Retry its save
      // before the next track takes ownership of the single display buffer.
      if (mounted && visible && !persisted && received == MilestoneV5::kArtworkBytes)
        commit();
      key = hex;
      changed = now;
      stage = 0;
      visible = false;
      received = 0;
      persisted = false;
      storageStatus = "";
      attempted = false;
      cacheRetryAt = 0;
      track = m;
      if (++generation == 0)
        ++generation;
    }
    if (!mounted || !m.ready || !m.title[0] || now - changed < 1500 || stage ||
        attempted || (cacheRetryAt && int32_t(now - cacheRetryAt) < 0))
      return;
    attempted = true;
    if (!SD.exists(path(".meta"))) {
      uint8_t data[444];
      size_t n;
      if (MilestoneV5::encodeNow(track, data, sizeof(data), n)) {
        File f = SD.open(path(".meta"), FILE_WRITE);
        if (f) {
          f.write(data, n);
          f.flush();
        }
      }
    }
    if (!packet)
      packet = static_cast<uint8_t *>(heap_caps_malloc(
          MilestoneV5::kArtworkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!packet) {
      attempted = false;
      cacheRetryAt = now + 5000;
      lastError = "앨범 이미지 메모리 부족; 재시도 대기";
      return;
    }
    File file = SD.open(path(".mac"), FILE_READ);
    if (file && file.size() == MilestoneV5::kArtworkBytes &&
        file.read(packet, MilestoneV5::kArtworkBytes) ==
            MilestoneV5::kArtworkBytes &&
        MilestoneV5::validArtwork(packet, MilestoneV5::kArtworkBytes)) {
      visible = true;
      persisted = true;
      storageStatus = "SD 캐시 사용";
      lastError = "";
      cacheRetryAt = 0;
      touch();
      return;
    }
    file.close();
    file = SD.open(path(".bak"), FILE_READ);
    if (file && file.size() == MilestoneV5::kArtworkBytes &&
        file.read(packet, MilestoneV5::kArtworkBytes) ==
            MilestoneV5::kArtworkBytes &&
        MilestoneV5::validArtwork(packet, MilestoneV5::kArtworkBytes)) {
      visible = true;
      persisted = true;
      storageStatus = "SD 백업 캐시 사용";
      lastError = "";
      cacheRetryAt = 0;
      return;
    }
    file.close();
    // A power loss or failed rename can leave a verified download in .tmp.
    // Recover it before treating this song as a cache miss.
    file = SD.open(path(".tmp"), FILE_READ);
    if (file && file.size() == MilestoneV5::kArtworkBytes &&
        file.read(packet, MilestoneV5::kArtworkBytes) == MilestoneV5::kArtworkBytes &&
        MilestoneV5::validArtwork(packet, MilestoneV5::kArtworkBytes)) {
      file.close();
      visible = true;
      received = MilestoneV5::kArtworkBytes;
      commit();
      return;
    }
    file.close();
    if (SD.exists(path(".mac")) || SD.exists(path(".bak"))) {
      // A transient SD read/CRC failure is not permission to delete or replace
      // persistent artwork. Keep it and retry without a server download.
      lastError = "저장된 앨범 이미지 읽기 실패; 캐시 유지 후 재시도";
      attempted = false;
      cacheRetryAt = now + 5000;
      return;
    }
    if (SD.exists(path(".blocked")))
      return;
    stage = 1;
    requestStarted = now;
  }
  bool request(uint8_t *out, size_t capacity, size_t &size) {
    if ((stage != 1 && stage != 2) || capacity < 9)
      return false;
    if (millis() - requestStarted > 30000) {
      lastError = "Artwork request timeout";
      if (manual)
        invalidate();
      else
        stage = 0;
      return false;
    }
    for (unsigned i = 0; i < 4; ++i)
      out[i + 1] = generation >> (i * 8);
    if (stage == 1) {
      out[0] = 3;
      size_t n;
      if (!MilestoneV5::encodeNow(track, out + 5, capacity - 5, n))
        return false;
      size = n + 5;
      return true;
    }
    out[0] = 4;
    for (unsigned i = 0; i < 4; ++i)
      out[i + 5] = received >> (i * 8);
    size = 9;
    return true;
  }
  void result(const uint8_t *p, size_t n) {
    if (n != 6 || MilestoneV5::readVideoU32(p + 1) != generation)
      return;
    if (p[0] == 3 && stage == 1) {
      if (p[5] == 0)
        stage = 2;
      else if (p[5] != 1) {
        lastError = "Artwork request rejected";
        if (manual)
          invalidate();
        else
          stage = 0;
      }
    }
    if (p[0] == 4 && p[5] == 2) {
      lastError = "Artwork download failed";
      if (manual)
        invalidate();
      else
        stage = 0;
    }
  }
  bool chunk(const uint8_t *p, size_t n) {
    if (stage != 2 || n < 9 || n > 468 || !packet)
      return false;
    if (MilestoneV5::readVideoU32(p) != generation)
      return false;
    uint32_t offset = MilestoneV5::readVideoU32(p + 4);
    size_t take = n - 8;
    if (offset < received)
      return uint64_t(offset) + take <= received &&
             !memcmp(packet + offset, p + 8, take);
    if (offset != received ||
        uint64_t(offset) + take > MilestoneV5::kArtworkBytes)
      return false;
    memcpy(packet + received, p + 8, take);
    received += take;
    if (received == MilestoneV5::kArtworkBytes) {
      bool valid = MilestoneV5::validArtwork(packet, received);
      visible = valid && !manual;
      stage = manual && valid ? 3 : 0;
      if (valid) {
        commit();
        if (manual && SD.exists(path(".mac")))
          invalidate();
      } else {
        lastError = "Artwork CRC/format mismatch";
        if (manual)
          invalidate();
      }
    }
    return true;
  }
  void draw(SimpleSt7735 &d, bool large = false, int top = 64) {
    if (!visible || !packet)
      return;
    // MAC1 RGB565 is big-endian. Small artwork fits under title/artist.
    d.startWrite();
    int side = large ? 88 : 60;
    const uint8_t *pixels = packet + 16 + (large ? 7200 : 0);
    for (int y = 0; y < side; ++y)
      for (int x = 0; x < side; ++x) {
        const uint8_t *p = pixels + (y * side + x) * 2;
        d.writePixel(x + (128 - side) / 2, y + top, uint16_t(p[0]) << 8 | p[1]);
      }
    d.endWrite();
  }

private:
  uint32_t changed = 0, requestStarted = 0, cacheRetryAt = 0, lastSaveAttempt = 0;
  bool attempted = false, indexRestored = false;
  bool needSpace = false;
  File scan;
  uint64_t scannedBytes = 0, oldestTime = UINT64_MAX;
  uint32_t scannedCount = 0, lastScan = 0;
  String oldest;
  MilestoneV5::NowMetadata track{};
  String path(const char *suffix) {
    return String("/now/art-cache/") + key + suffix;
  }
  void touch() {
    File f = SD.open(path(".use"), FILE_WRITE);
    if (f) {
      f.write(uint8_t(1));
      f.flush();
    }
  }
  void commit() {
    if (!validKey(key) || received != MilestoneV5::kArtworkBytes)
      return;
    lastSaveAttempt = millis();
    // Never overwrite a custom entry. Atomic new-name publication keeps older
    // data.
    if (SD.exists(path(".custom")) || SD.exists(path(".mac"))) {
      if (matchesPacket(path(".mac"))) {
        persisted = true;
        storageStatus = "SD 캐시 저장·재검증 완료";
      }
      return;
    }
    if (!cacheKnown) {
      storageStatus = "화면 표시 중 · SD 저장 대기 (캐시 집계 중)";
      return;
    }
    if (cacheBytes + MilestoneV5::kArtworkBytes > 2ULL * 1024 * 1024 * 1024) {
      needSpace = true;
      cacheKnown = false;
      storageStatus = "화면 표시 중 · SD 저장 대기 (캐시 한도)";
      return;
    }
    needSpace = false;
    if (SD.totalBytes() < SD.usedBytes() + 1024ULL * 1024 * 1024) {
      saveFailed("SD 여유 공간 확인 실패/부족");
      return;
    }
    String temporary = path(".tmp"), final = path(".mac");
    // Retain an already verified staging file on retry instead of deleting it
    // and depending on another successful write of the same bytes.
    bool ok = matchesPacket(temporary);
    if (!ok) {
      if (SD.exists(temporary) && !SD.remove(temporary)) {
        saveFailed("임시 파일 준비 실패");
        return;
      }
      File file = SD.open(temporary, FILE_WRITE);
      if (!file) {
        saveFailed("임시 파일 열기 실패");
        return;
      }
      ok = file.write(packet, received) == received;
      file.flush();
      file.close();
      ok = ok && matchesPacket(temporary);
    }
    if (!ok) {
      saveFailed("임시 파일 기록/재검증 실패");
      return;
    }
    if (!SD.rename(temporary, final)) {
      saveFailed("최종 파일 이름 확정 실패; 임시 캐시 보존");
      return;
    }
    if (!matchesPacket(final)) {
      saveFailed("최종 파일 재검증 실패; 파일 보존");
      return;
    }
    persisted = true;
    storageStatus = "SD 캐시 저장·재검증 완료";
    Serial0.println(String("ART cache saved key=") + key);
    cacheBytes += received;
    ++cacheCount;
    touch();
    uint8_t metadata[444];
    size_t n;
    if (MilestoneV5::encodeNow(track, metadata, sizeof(metadata), n)) {
      File meta = SD.open(path(".meta"), FILE_WRITE);
      if (meta) {
        meta.write(metadata, n);
        meta.flush();
      }
    }
  }
  bool matchesPacket(const String &name) {
    File file = SD.open(name, FILE_READ);
    if (!file || file.size() != received)
      return false;
    uint8_t check[512];
    size_t offset = 0;
    while (offset < received) {
      size_t take = min(sizeof(check), size_t(received - offset));
      if (file.read(check, take) != take || memcmp(check, packet + offset, take))
        return false;
      offset += take;
    }
    return true;
  }
  void saveFailed(const char *reason) {
    ++saveFailures;
    storageStatus = String("화면 표시 중 · SD 저장 미완료: ") + reason;
    Serial0.println(String("ART cache save failed key=") + key + " reason=" + reason);
  }
};
