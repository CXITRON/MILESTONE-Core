// NOW-only, best-effort album-art lookup. MusicBrainz supplies the release
// group and Cover Art Archive supplies a thumbnail. JPEG decode and 1-bit
// conversion happen on the ESP32; failures never block AMS text/progress.

const char NOW_ARTWORK_RUNTIME_MARKER[] __attribute__((used)) = "MILESTONE_NOW_ARTWORK_RUNTIME_V1";
constexpr uint16_t NOW_ART_SMALL_W = 60;
constexpr uint16_t NOW_ART_SMALL_H = 60;
constexpr uint16_t NOW_ART_LARGE_W = 88;
constexpr uint16_t NOW_ART_LARGE_H = 88;
constexpr size_t NOW_ART_SMALL_BYTES = ((NOW_ART_SMALL_W + 7U) / 8U) * NOW_ART_SMALL_H;
constexpr size_t NOW_ART_LARGE_BYTES = ((NOW_ART_LARGE_W + 7U) / 8U) * NOW_ART_LARGE_H;
constexpr uint8_t NOW_ART_CACHE_SLOTS = 6;
constexpr uint32_t NOW_ART_TRACK_SETTLE_MS = 1400UL;
constexpr uint32_t NOW_ART_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t NOW_ART_REQUEST_SPACING_MS = 1200UL;
constexpr size_t NOW_ART_MAX_JPEG_BYTES = 220UL * 1024UL;

struct NowArtworkCacheSlot {
  uint64_t key = 0;
  uint32_t used = 0;
  bool valid = false;
};

NowArtworkCacheSlot nowArtworkCache[NOW_ART_CACHE_SLOTS];
uint8_t *nowArtworkCacheBytes = nullptr;
uint64_t nowArtworkTrackKey = 0;
uint64_t nowArtworkVisibleKey = 0;
String nowArtworkObservedTitle;
String nowArtworkObservedArtist;
String nowArtworkObservedAlbum;
uint32_t nowArtworkTrackChangedMs = 0;
uint32_t nowArtworkRetryAtMs = 0;
uint32_t nowArtworkNextRequestMs = 0;
uint32_t nowArtworkUseCounter = 0;
volatile bool nowArtworkWorkerReady = false;
volatile bool nowArtworkWorkerSuccess = false;
volatile bool nowArtworkWorkerRunning = false;
uint64_t nowArtworkWorkerKey = 0;
String nowArtworkState = "idle";
char nowArtworkWorkerTitle[192] = {};
char nowArtworkWorkerArtist[192] = {};
char nowArtworkWorkerAlbum[192] = {};
uint8_t nowArtworkWorkerSmall[NOW_ART_SMALL_BYTES] = {};
uint8_t nowArtworkWorkerLarge[NOW_ART_LARGE_BYTES] = {};
portMUX_TYPE nowArtworkMux = portMUX_INITIALIZER_UNLOCKED;

uint64_t nowArtworkHash(const String &title, const String &artist, const String &album) {
  uint64_t hash = 1469598103934665603ULL;
  const String combined = title + "\x1f" + artist + "\x1f" + album;
  for (size_t i = 0; i < combined.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(combined[i]);
    if (c >= 'A' && c <= 'Z') c = static_cast<uint8_t>(c + ('a' - 'A'));
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

String nowArtworkUrlEncode(const char *text) {
  static const char hex[] = "0123456789ABCDEF";
  String encoded;
  if (!text) return encoded;
  encoded.reserve(strlen(text) * 3U);
  for (const uint8_t *p = reinterpret_cast<const uint8_t *>(text); *p; ++p) {
    const uint8_t c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

bool nowArtworkFindReleaseGroup(const String &xml, String &mbid) {
  int pos = xml.indexOf("<release-group id=\"");
  if (pos < 0) return false;
  pos += 19;
  const int end = xml.indexOf('"', pos);
  if (end - pos != 36) return false;
  mbid = xml.substring(pos, end);
  for (size_t i = 0; i < mbid.length(); ++i) {
    const char c = mbid[i];
    if (!(isxdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
  }
  return true;
}

bool nowArtworkReadJpeg(const String &url, uint8_t *&jpeg, size_t &jpegSize) {
  jpeg = nullptr;
  jpegSize = 0;
  NetworkClientSecure client;
  client.setCACert(MILESTONE_UPDATE_ROOT_CA);
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setRedirectLimit(4);
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "MILESTONE-Core/2.2.0 (https://github.com/CXITRON/MILESTONE-Core)");
  const int response = http.GET();
  if (response != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const int announced = http.getSize();
  if (announced > static_cast<int>(NOW_ART_MAX_JPEG_BYTES)) {
    http.end();
    return false;
  }
  const size_t capacity = announced > 0 ? static_cast<size_t>(announced) : NOW_ART_MAX_JPEG_BYTES;
  jpeg = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  uint32_t lastDataMs = millis();
  while (http.connected() && jpegSize < capacity &&
         (announced < 0 || jpegSize < static_cast<size_t>(announced))) {
    const size_t available = stream->available();
    if (available) {
      const size_t take = min(available, capacity - jpegSize);
      const int received = stream->readBytes(jpeg + jpegSize, take);
      if (received <= 0) break;
      jpegSize += static_cast<size_t>(received);
      lastDataMs = millis();
    } else {
      if (elapsed(millis(), lastDataMs, 6000UL)) break;
      delay(1);
    }
  }
  http.end();
  const bool complete = jpegSize > 128U &&
      (announced < 0 || jpegSize == static_cast<size_t>(announced));
  if (!complete) {
    heap_caps_free(jpeg);
    jpeg = nullptr;
    jpegSize = 0;
  }
  return complete;
}

void nowArtworkMakeBitmap(const uint16_t *rgb, uint16_t sourceW, uint16_t sourceH,
                          uint8_t *output, uint16_t targetW, uint16_t targetH) {
  memset(output, 0, ((targetW + 7U) / 8U) * targetH);
  static const int8_t bayer[16] = {-8, 0, -6, 2, 4, -4, 6, -2,
                                    -5, 3, -7, 1, 7, -1, 5, -3};
  const uint16_t stride = (targetW + 7U) / 8U;
  for (uint16_t y = 0; y < targetH; ++y) {
    const uint16_t sy = static_cast<uint32_t>(y) * sourceH / targetH;
    for (uint16_t x = 0; x < targetW; ++x) {
      const uint16_t sx = static_cast<uint32_t>(x) * sourceW / targetW;
      const uint16_t pixel = rgb[static_cast<size_t>(sy) * sourceW + sx];
      const int r = ((pixel >> 11) & 0x1F) * 255 / 31;
      const int g = ((pixel >> 5) & 0x3F) * 255 / 63;
      const int b = (pixel & 0x1F) * 255 / 31;
      const int luminance = (r * 54 + g * 183 + b * 19) >> 8;
      if (luminance >= 128 + bayer[(y & 3U) * 4U + (x & 3U)] * 5) {
        output[static_cast<size_t>(y) * stride + (x >> 3)] |= 1U << (x & 7U);
      }
    }
  }
}

bool nowArtworkDecodeJpeg(uint8_t *jpeg, size_t jpegSize) {
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = jpeg;
  cfg.indata_size = jpegSize;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
  esp_jpeg_image_output_t info = {};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || !info.width || !info.height ||
      info.output_len > 160UL * 1024UL) return false;
  uint8_t *rgb = static_cast<uint8_t *>(heap_caps_malloc(info.output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rgb) return false;
  cfg.outbuf = rgb;
  cfg.outbuf_size = info.output_len;
  const bool decoded = esp_jpeg_decode(&cfg, &info) == ESP_OK;
  if (decoded) {
    nowArtworkMakeBitmap(reinterpret_cast<const uint16_t *>(rgb), info.width, info.height,
                         nowArtworkWorkerSmall, NOW_ART_SMALL_W, NOW_ART_SMALL_H);
    nowArtworkMakeBitmap(reinterpret_cast<const uint16_t *>(rgb), info.width, info.height,
                         nowArtworkWorkerLarge, NOW_ART_LARGE_W, NOW_ART_LARGE_H);
  }
  heap_caps_free(rgb);
  return decoded;
}

void nowArtworkWorker(void *) {
  bool success = false;
  String query = nowArtworkWorkerAlbum[0]
      ? "https://musicbrainz.org/ws/2/release-group/?fmt=xml&limit=3&query=release%3A%22"
      : "https://musicbrainz.org/ws/2/recording/?fmt=xml&limit=3&query=recording%3A%22";
  query += nowArtworkUrlEncode(nowArtworkWorkerAlbum[0] ? nowArtworkWorkerAlbum
                                                        : nowArtworkWorkerTitle);
  query += "%22%20AND%20artist%3A%22";
  query += nowArtworkUrlEncode(nowArtworkWorkerArtist);
  query += "%22";
  NetworkClientSecure client;
  client.setCACert(MILESTONE_UPDATE_ROOT_CA);
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  if (http.begin(client, query)) {
    http.addHeader("User-Agent", "MILESTONE-Core/2.2.0 (https://github.com/CXITRON/MILESTONE-Core)");
    const int response = http.GET();
    if (response == HTTP_CODE_OK && http.getSize() <= 48 * 1024) {
      const String xml = http.getString();
      String mbid;
      if (nowArtworkFindReleaseGroup(xml, mbid)) {
        http.end();
        uint8_t *jpeg = nullptr;
        size_t jpegSize = 0;
        if (nowArtworkReadJpeg("https://coverartarchive.org/release-group/" + mbid + "/front-250",
                              jpeg, jpegSize)) {
          success = nowArtworkDecodeJpeg(jpeg, jpegSize);
          heap_caps_free(jpeg);
        }
      } else {
        http.end();
      }
    } else {
      http.end();
    }
  }
  portENTER_CRITICAL(&nowArtworkMux);
  nowArtworkWorkerSuccess = success;
  nowArtworkWorkerReady = true;
  nowArtworkWorkerRunning = false;
  portEXIT_CRITICAL(&nowArtworkMux);
  vTaskDelete(nullptr);
}

uint8_t *nowArtworkSlotBytes(uint8_t slot) {
  return nowArtworkCacheBytes + static_cast<size_t>(slot) *
      (NOW_ART_SMALL_BYTES + NOW_ART_LARGE_BYTES);
}

int nowArtworkFindCache(uint64_t key) {
  if (!nowArtworkCacheBytes) return -1;
  for (uint8_t i = 0; i < NOW_ART_CACHE_SLOTS; ++i) {
    if (nowArtworkCache[i].valid && nowArtworkCache[i].key == key) return i;
  }
  return -1;
}

void nowArtworkAcceptWorkerResult() {
  bool ready;
  bool success;
  portENTER_CRITICAL(&nowArtworkMux);
  ready = nowArtworkWorkerReady;
  success = nowArtworkWorkerSuccess;
  nowArtworkWorkerReady = false;
  portEXIT_CRITICAL(&nowArtworkMux);
  if (!ready) return;
  if (nowArtworkWorkerKey != nowArtworkTrackKey) {
    nowArtworkState = "settling";
    return;
  }
  if (!success || !nowArtworkCacheBytes) {
    nowArtworkState = "not-found";
    nowArtworkRetryAtMs = millis() + NOW_ART_RETRY_MS;
    return;
  }
  uint8_t slot = 0;
  uint32_t oldest = UINT32_MAX;
  for (uint8_t i = 0; i < NOW_ART_CACHE_SLOTS; ++i) {
    if (!nowArtworkCache[i].valid) { slot = i; oldest = 0; break; }
    if (nowArtworkCache[i].used < oldest) { oldest = nowArtworkCache[i].used; slot = i; }
  }
  uint8_t *destination = nowArtworkSlotBytes(slot);
  memcpy(destination, nowArtworkWorkerSmall, NOW_ART_SMALL_BYTES);
  memcpy(destination + NOW_ART_SMALL_BYTES, nowArtworkWorkerLarge, NOW_ART_LARGE_BYTES);
  nowArtworkCache[slot].key = nowArtworkWorkerKey;
  nowArtworkCache[slot].used = ++nowArtworkUseCounter;
  nowArtworkCache[slot].valid = true;
  nowArtworkVisibleKey = nowArtworkWorkerKey;
  nowArtworkState = "ready";
  wakeDisplay();
}

bool nowArtworkLayoutUsesImage() {
  return config.nowLayout == NowLayout::TITLE_ARTWORK ||
         config.nowLayout == NowLayout::ARTWORK_ONLY;
}

bool nowArtworkNeedsNetwork() {
  if (nowArtworkWorkerRunning) return true;
  return nowArtworkLayoutUsesImage() && nowArtworkTrackKey != 0 &&
         nowArtworkVisibleKey != nowArtworkTrackKey &&
         (!nowArtworkRetryAtMs || deadlineReached(millis(), nowArtworkRetryAtMs));
}

bool nowArtworkTransferBusy() { return nowArtworkWorkerRunning; }

void processNowArtwork() {
  if (!nowArtworkCacheBytes && psramFound()) {
    nowArtworkCacheBytes = static_cast<uint8_t *>(heap_caps_calloc(
        NOW_ART_CACHE_SLOTS, NOW_ART_SMALL_BYTES + NOW_ART_LARGE_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (nowArtworkCacheBytes) logLine(String("NOW artwork ready: ") + NOW_ARTWORK_RUNTIME_MARKER);
  }
  nowArtworkAcceptWorkerResult();
  const String title = bluetoothNowPlayingTitle();
  const String artist = bluetoothNowPlayingArtist();
  const String album = bluetoothNowPlayingAlbum();
  if (title != nowArtworkObservedTitle || artist != nowArtworkObservedArtist ||
      album != nowArtworkObservedAlbum) {
    nowArtworkObservedTitle = title;
    nowArtworkObservedArtist = artist;
    nowArtworkObservedAlbum = album;
    nowArtworkTrackKey = title.length() ? nowArtworkHash(title, artist, album) : 0;
    nowArtworkVisibleKey = 0;
    nowArtworkTrackChangedMs = millis();
    nowArtworkRetryAtMs = 0;
    nowArtworkState = title.length() ? "settling" : "idle";
    const int cached = nowArtworkFindCache(nowArtworkTrackKey);
    if (cached >= 0) {
      nowArtworkCache[cached].used = ++nowArtworkUseCounter;
      nowArtworkVisibleKey = nowArtworkTrackKey;
      nowArtworkState = "cached";
    }
  }
  if (!nowArtworkLayoutUsesImage() || !nowArtworkTrackKey || nowArtworkVisibleKey == nowArtworkTrackKey ||
      nowArtworkWorkerRunning || portalActive || firmwareTransferActive() || updateState == UpdateState::CHECKING ||
      !deadlineReached(millis(), nowArtworkTrackChangedMs + NOW_ART_TRACK_SETTLE_MS) ||
      (nowArtworkRetryAtMs && !deadlineReached(millis(), nowArtworkRetryAtMs)) ||
      !deadlineReached(millis(), nowArtworkNextRequestMs)) return;
  if (!stationNetworkReady()) {
    nowArtworkState = "waiting-wifi";
    if (config.savedNetworkCount > 0 && !portalActive &&
        (runtimeState == RuntimeState::WIFI_SLEEP || runtimeState == RuntimeState::RUNNING_OFFLINE)) {
      startSavedWifiSequence(false);
    }
    return;
  }
  if (ESP.getFreeHeap() < 55 * 1024UL || ESP.getMaxAllocHeap() < 24 * 1024UL || !nowArtworkCacheBytes) {
    nowArtworkState = "low-memory";
    nowArtworkRetryAtMs = millis() + NOW_ART_RETRY_MS;
    return;
  }
  strlcpy(nowArtworkWorkerTitle, title.c_str(), sizeof(nowArtworkWorkerTitle));
  strlcpy(nowArtworkWorkerArtist, artist.c_str(), sizeof(nowArtworkWorkerArtist));
  strlcpy(nowArtworkWorkerAlbum, album.c_str(), sizeof(nowArtworkWorkerAlbum));
  nowArtworkWorkerKey = nowArtworkTrackKey;
  nowArtworkWorkerRunning = true;
  nowArtworkState = "lookup";
  nowArtworkNextRequestMs = millis() + NOW_ART_REQUEST_SPACING_MS;
  if (xTaskCreate(nowArtworkWorker, "now-artwork", 10240, nullptr, 1, nullptr) != pdPASS) {
    nowArtworkWorkerRunning = false;
    nowArtworkState = "task-failed";
    nowArtworkRetryAtMs = millis() + NOW_ART_RETRY_MS;
  }
}

bool nowArtworkAvailable() {
  return nowArtworkTrackKey != 0 && nowArtworkVisibleKey == nowArtworkTrackKey &&
         nowArtworkFindCache(nowArtworkTrackKey) >= 0;
}

const uint8_t *nowArtworkSmallBitmap() {
  const int slot = nowArtworkFindCache(nowArtworkTrackKey);
  return slot >= 0 ? nowArtworkSlotBytes(slot) : nullptr;
}

const uint8_t *nowArtworkLargeBitmap() {
  const int slot = nowArtworkFindCache(nowArtworkTrackKey);
  return slot >= 0 ? nowArtworkSlotBytes(slot) + NOW_ART_SMALL_BYTES : nullptr;
}

const char *nowArtworkStatus() { return nowArtworkState.c_str(); }
