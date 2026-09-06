#pragma once
#include <HTTPClient.h>
#include <MilestoneV5Artwork.h>
#include <MilestoneV5Now.h>
#include <WiFi.h>
#include <atomic>
#include <esp_heap_caps.h>

namespace V5ArtworkWorker {
// Acquire/release publishes the completed PSRAM buffer; no worker owns SPI/SD.
std::atomic<int> state{0}; // 0 idle, 1 running, 2 ready, 3 failed
std::atomic<bool> cancel{false};
MilestoneV5::NowMetadata request{};
uint8_t *packet = nullptr;
String encode(const char *s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (const uint8_t *p = reinterpret_cast<const uint8_t *>(s); *p; ++p) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
        (*p >= '0' && *p <= '9'))
      out += char(*p);
    else {
      out += '%';
      out += hex[*p >> 4];
      out += hex[*p & 15];
    }
  }
  return out;
}
void run(void *) {
  bool ok = false;
  {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(6000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (http.begin(client,
                   "http://milestone-artwork.typhoon-individual.workers.dev/v3/"
                   "artwork")) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.addHeader("User-Agent", "MILESTONE-v5-development");
      String body = "title=" + encode(request.title) +
                    "&artist=" + encode(request.artist) +
                    "&album=" + encode(request.album);
      if (http.POST(body) == 200 &&
          http.getSize() == int(MilestoneV5::kArtworkBytes)) {
        size_t received = 0;
        uint32_t started = millis();
        auto *stream = http.getStreamPtr();
        while (received < MilestoneV5::kArtworkBytes &&
               millis() - started < 8000 && !cancel.load()) {
          const int available = stream->available();
          if (available > 0) {
            size_t take =
                min(size_t(available), MilestoneV5::kArtworkBytes - received);
            int got = stream->read(packet + received, take);
            if (got <= 0)
              break;
            received += got;
          } else if (!http.connected())
            break;
          else
            vTaskDelay(1);
        }
        ok = received == MilestoneV5::kArtworkBytes &&
             MilestoneV5::validArtwork(packet, received);
      }
      http.end();
    }
  }
  state.store(ok ? 2 : 3, std::memory_order_release);
  vTaskDelete(nullptr);
}
bool start(const uint8_t *payload, size_t size) {
  if (state.load(std::memory_order_acquire) == 1 ||
      WiFi.status() != WL_CONNECTED || ESP.getFreeHeap() < 80000 ||
      ESP.getMaxAllocHeap() < 32768)
    return false;
  MilestoneV5::NowMetadata next{};
  if (!MilestoneV5::decodeNow(payload, size, next) || !next.title[0])
    return false;
  const float t = temperatureRead();
  if (!isfinite(t) || t >= 75)
    return false;
  if (!packet)
    packet = static_cast<uint8_t *>(heap_caps_malloc(
        MilestoneV5::kArtworkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!packet)
    return false;
  request = next;
  cancel.store(false);
  state.store(1, std::memory_order_release);
  if (xTaskCreatePinnedToCore(run, "v5-artwork", 14336, nullptr, 1, nullptr,
                              0) != pdPASS) {
    state.store(3);
    return false;
  }
  return true;
}
} // namespace V5ArtworkWorker
