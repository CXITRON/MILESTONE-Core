#pragma once
#include "../../../../UpdateCertificates.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <atomic>
#include <esp_heap_caps.h>

// One producer / one consumer. The network task never touches SD, SPI or Flash.
// A bounded PSRAM ring applies backpressure while MAIN services the display.
namespace V5DownloadWorker {
constexpr uint32_t capacity = 16384;
std::atomic<int> state{0}; // idle, running, complete, failed
std::atomic<bool> cancel{false};
std::atomic<uint32_t> produced{0}, consumed{0}, length{0};
uint8_t *ring = nullptr;
String url;
uint32_t limit = 0;
bool trustedUrl(const String &value) {
  if (value.length() > 4096 || value.indexOf('\r') >= 0 ||
      value.indexOf('\n') >= 0 || value.indexOf('@') >= 0)
    return false;
  return value.startsWith(
             "https://github.com/CXITRON/MILESTONE-Core/releases/") ||
         value.startsWith("https://release-assets.githubusercontent.com/") ||
         value.startsWith("https://objects.githubusercontent.com/");
}
void run(void *) {
  bool ok = false;
  {
    NetworkClientSecure client;
    client.setCACert(MILESTONE_UPDATE_ROOT_CA);
    client.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(6000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char *headers[] = {"Location"};
    http.collectHeaders(headers, 1);
    String current = url;
    int code = 0;
    for (unsigned redirects = 0; redirects < 4 && !cancel.load(); ++redirects) {
      if (!trustedUrl(current) || !http.begin(client, current))
        break;
      http.addHeader("User-Agent", "MILESTONE-v5");
      http.addHeader("Accept-Encoding", "identity");
      code = http.GET();
      if (code == 200)
        break;
      if (code != 301 && code != 302 && code != 303 && code != 307 &&
          code != 308)
        break;
      current = http.header("Location");
      http.end();
      code = 0;
    }
    const int size = http.getSize();
    if (code == 200 && size > 0 && uint32_t(size) <= limit) {
      length.store(size, std::memory_order_release);
      uint32_t offset = 0, lastData = millis(), started = millis();
      auto *stream = http.getStreamPtr();
      while (offset < uint32_t(size) && !cancel.load() &&
             millis() - started < 600000) {
        const uint32_t used = offset - consumed.load(std::memory_order_acquire);
        if (used >= capacity) {
          vTaskDelay(1);
          lastData = millis();
          continue;
        }
        int available = stream->available();
        if (available > 0) {
          uint32_t take =
              min(uint32_t(available),
                  min(uint32_t(size) - offset,
                      min(capacity - used, capacity - offset % capacity)));
          int n = stream->read(ring + offset % capacity, take);
          if (n <= 0)
            break;
          offset += n;
          produced.store(offset, std::memory_order_release);
          lastData = millis();
        } else if (!http.connected() || millis() - lastData > 10000)
          break;
        else
          vTaskDelay(1);
      }
      ok = offset == uint32_t(size) && !cancel.load();
    }
    http.end();
  }
  state.store(ok ? 2 : 3, std::memory_order_release);
  vTaskDelete(nullptr);
}
bool start(const String &source, uint32_t maximum) {
  if (state.load(std::memory_order_acquire) == 1 || source.length() > 420 ||
      !trustedUrl(source) || !maximum || maximum > 8 * 1024 * 1024 ||
      WiFi.status() != WL_CONNECTED || time(nullptr) < 1704067200 ||
      ESP.getFreeHeap() < 90000 || ESP.getMaxAllocHeap() < 40000)
    return false;
  if (!ring)
    ring = static_cast<uint8_t *>(
        heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ring)
    return false;
  url = source;
  limit = maximum;
  produced.store(0);
  consumed.store(0);
  length.store(0);
  cancel.store(false);
  state.store(1, std::memory_order_release);
  if (xTaskCreatePinnedToCore(run, "v5-download", 16384, nullptr, 1, nullptr,
                              0) != pdPASS) {
    state.store(3);
    return false;
  }
  return true;
}
size_t read(uint32_t offset, uint8_t *out, size_t maximum) {
  if (!out || offset != consumed.load(std::memory_order_acquire))
    return 0;
  const uint32_t available = produced.load(std::memory_order_acquire) - offset;
  const size_t n = min(maximum, size_t(available));
  for (size_t i = 0; i < n; ++i)
    out[i] = ring[(offset + i) % capacity];
  consumed.store(offset + n, std::memory_order_release);
  return n;
}
} // namespace V5DownloadWorker
