#pragma once
#include <MilestoneV5DownloadWorker.h>
#include <MilestoneV5Video.h>

class V5RemoteDownload {
public:
  uint32_t id = 0, lastRequest = 0, startedAt = 0;
  bool active = false, launched = false, failed = false;
  uint8_t failureCode = V5DownloadWorker::kFailureNone;
  String source;
  uint32_t limit = 0;
  void service(bool healthy) {
    if (!active)
      return;
    if (!healthy || millis() - lastRequest > 15000 ||
        millis() - startedAt > 600000) {
      V5DownloadWorker::cancel.store(true);
      failureCode = !healthy ? V5DownloadWorker::kFailureCancelled
                             : V5DownloadWorker::kFailureStalled;
      failed = true;
    }
    if (failed) {
      if (V5DownloadWorker::state.load() != 1)
        active = false;
      return;
    }
    if (!launched) {
      if (millis() - startedAt > 60000) {
        if (failureCode == V5DownloadWorker::kFailureNone)
          failureCode = V5DownloadWorker::kFailureWifi;
        failed = true;
        return;
      }
      if (!V5Ams::bluetoothNowPlayingHasLiveConnection() &&
          V5ArtworkWorker::state.load() != 1) {
        launched = V5DownloadWorker::start(source, limit);
        if (!launched)
          failureCode = V5DownloadWorker::failure.load();
      }
    }
  }
  bool request(const uint8_t *p, size_t n, uint8_t *out, size_t &size,
               bool healthy) {
    if (n < 5 || p[0] < 16 || p[0] > 18)
      return false;
    const uint8_t op = p[0];
    const uint32_t incoming = MilestoneV5::readVideoU32(p + 1);
    if (!incoming)
      return false;
    memset(out, 0, 14);
    out[0] = op;
    memcpy(out + 1, p + 1, 4);
    out[5] = 2;
    size = 14;
    if (op == 16) {
      if (n <= 9 || n > 429 || !healthy)
        return true;
      String next;
      for (size_t i = 9; i < n; ++i) {
        if (!p[i])
          return true;
        next += char(p[i]);
      }
      uint32_t maximum = MilestoneV5::readVideoU32(p + 5);
      if (!V5DownloadWorker::trustedUrl(next) || !maximum ||
          maximum > 8 * 1024 * 1024)
        return true;
      if (incoming != id) {
        if (active || V5DownloadWorker::state.load() == 1) {
          out[5] = 1;
          return true;
        }
        id = incoming;
        source = next;
        limit = maximum;
        active = true;
        launched = failed = false;
        failureCode = V5DownloadWorker::kFailureNone;
        startedAt = millis();
      }
      lastRequest = millis();
      out[5] = failed ? 2 : 0;
      return true;
    }
    if (incoming != id)
      return true;
    lastRequest = millis();
    if (op == 18 && n == 5) {
      failed = true;
      failureCode = V5DownloadWorker::kFailureCancelled;
      V5DownloadWorker::cancel.store(true);
      out[5] = 0;
      return true;
    }
    if (op != 17 || n != 9)
      return false;
    uint32_t offset = MilestoneV5::readVideoU32(p + 5);
    memcpy(out + 6, p + 5, 4);
    if (failed) {
      out[14] = failureCode;
      size = 15;
      return true;
    }
    if (!launched) {
      out[5] = 1;
      return true;
    }
    uint32_t total = V5DownloadWorker::length.load(std::memory_order_acquire);
    for (unsigned i = 0; i < 4; ++i)
      out[10 + i] = total >> (8 * i);
    if (V5DownloadWorker::state.load() == 3) {
      failed = true;
      failureCode = V5DownloadWorker::failure.load(std::memory_order_acquire);
      out[14] = failureCode;
      size = 15;
      return true;
    }
    size_t got = V5DownloadWorker::read(offset, out + 14, 460);
    if (got) {
      out[5] = 0;
      size += got;
    } else if (total && offset == total &&
               V5DownloadWorker::state.load() == 2) {
      out[5] = 3;
      active = false;
    } else if (offset == V5DownloadWorker::consumed.load()) {
      out[5] = 1;
    }
    return true;
  }
};
