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
constexpr size_t NOW_ART_MAX_LOOKUP_BYTES = 48UL * 1024UL;
constexpr uint32_t NOW_ART_WORKER_STACK_BYTES = 14UL * 1024UL;
constexpr uint32_t NOW_ART_RTC_MAGIC = 0x4E415254UL;  // "NART"

enum class NowArtworkWorkerResult : uint8_t {
  NONE,
  READY,
  NETWORK,
  LOOKUP_HTTP,
  LOOKUP_TOO_LARGE,
  NO_MATCH,
  DOWNLOAD,
  DECODE,
  CANCELLED,
};

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
volatile NowArtworkWorkerResult nowArtworkWorkerResult = NowArtworkWorkerResult::NONE;
volatile uint32_t nowArtworkWorkerStackFreeBytes = 0;
volatile uint32_t nowArtworkGeneration = 1;
RTC_DATA_ATTR uint32_t nowArtworkRtcMagic = 0;
RTC_DATA_ATTR uint8_t nowArtworkRtcStage = 0;
uint64_t nowArtworkWorkerKey = 0;
uint32_t nowArtworkWorkerGeneration = 0;
String nowArtworkState = "idle";
String nowArtworkInterruptedStage;
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

String nowArtworkLuceneEscape(const char *text) {
  String escaped;
  if (!text) return escaped;
  escaped.reserve(strlen(text) * 2U);
  for (const char *cursor = text; *cursor; ++cursor) {
    const char c = *cursor;
    if (c == '+' || c == '-' || c == '!' || c == '(' || c == ')' || c == '{' || c == '}' ||
        c == '[' || c == ']' || c == '^' || c == '"' || c == '~' || c == '*' || c == '?' ||
        c == ':' || c == '\\' || c == '/') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

bool nowArtworkWorkerCancelled(uint32_t generation) {
  return generation != nowArtworkGeneration || thermalWarning || thermalThrottled ||
         thermalSafeMode || temperatureSensorFault;
}

bool nowArtworkReadReleaseGroup(HTTPClient &http, String &mbid, bool &complete,
                                uint32_t generation) {
  WiFiClient *stream = http.getStreamPtr();
  const int announced = http.getSize();
  complete = false;
  if (!stream || announced > static_cast<int>(NOW_ART_MAX_LOOKUP_BYTES)) return false;
  size_t total = 0;
  MilestoneCoreLogic::MusicBrainzReleaseGroupParser parser;
  MilestoneCoreLogic::resetMusicBrainzReleaseGroupParser(parser);
  char uuid[37] = {};
  uint8_t chunk[256];
  uint32_t lastDataMs = millis();
  while (total < NOW_ART_MAX_LOOKUP_BYTES &&
         (announced < 0 || total < static_cast<size_t>(announced))) {
    if (nowArtworkWorkerCancelled(generation)) return false;
    const size_t available = stream->available();
    if (!available) {
      if (!http.connected()) break;
      if (elapsed(millis(), lastDataMs, 6000UL)) return false;
      delay(1);
      continue;
    }
    const size_t take = min(available, min(sizeof(chunk), NOW_ART_MAX_LOOKUP_BYTES - total));
    const int received = stream->readBytes(chunk, take);
    if (received <= 0) return false;
    lastDataMs = millis();
    total += static_cast<size_t>(received);
    if (MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(
            parser, chunk, static_cast<size_t>(received), uuid)) {
      mbid = uuid;
      complete = true;
      return true;
    }
  }
  complete = announced >= 0 ? total == static_cast<size_t>(announced) : !http.connected();
  return false;
}

bool nowArtworkReadJpeg(const String &url, uint8_t *&jpeg, size_t &jpegSize,
                        uint32_t generation) {
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
  http.addHeader("User-Agent", String("MILESTONE-Now/") + FIRMWARE_VERSION +
                               " (https://github.com/CXITRON/MILESTONE-Core)");
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
    if (nowArtworkWorkerCancelled(generation)) break;
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
  const uint32_t generation = nowArtworkWorkerGeneration;
  bool success = false;
  NowArtworkWorkerResult result = NowArtworkWorkerResult::NETWORK;
  nowArtworkRtcMagic = NOW_ART_RTC_MAGIC;
  nowArtworkRtcStage = 1;
  const String primary = nowArtworkLuceneEscape(nowArtworkWorkerAlbum[0]
      ? nowArtworkWorkerAlbum : nowArtworkWorkerTitle);
  const String artist = nowArtworkLuceneEscape(nowArtworkWorkerArtist);
  String query = nowArtworkWorkerAlbum[0]
      ? "https://musicbrainz.org/ws/2/release-group/?fmt=xml&limit=3&query=release%3A%22"
      : "https://musicbrainz.org/ws/2/recording/?fmt=xml&limit=3&query=recording%3A%22";
  query += nowArtworkUrlEncode(primary.c_str());
  query += "%22%20AND%20artist%3A%22";
  query += nowArtworkUrlEncode(artist.c_str());
  query += "%22";
  NetworkClientSecure client;
  client.setCACert(MILESTONE_UPDATE_ROOT_CA);
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  if (!nowArtworkWorkerCancelled(generation) && http.begin(client, query)) {
    http.addHeader("User-Agent", String("MILESTONE-Now/") + FIRMWARE_VERSION +
                                 " (https://github.com/CXITRON/MILESTONE-Core)");
    const int response = http.GET();
    if (response == HTTP_CODE_OK) {
      const int lookupSize = http.getSize();
      String mbid;
      bool lookupComplete = false;
      if (nowArtworkReadReleaseGroup(http, mbid, lookupComplete, generation)) {
        http.end();
        nowArtworkRtcStage = 2;
        uint8_t *jpeg = nullptr;
        size_t jpegSize = 0;
        if (!nowArtworkWorkerCancelled(generation) &&
            nowArtworkReadJpeg("https://coverartarchive.org/release-group/" + mbid + "/front-250",
                               jpeg, jpegSize, generation)) {
          nowArtworkRtcStage = 3;
          success = !nowArtworkWorkerCancelled(generation) && nowArtworkDecodeJpeg(jpeg, jpegSize);
          result = success ? NowArtworkWorkerResult::READY : NowArtworkWorkerResult::DECODE;
          heap_caps_free(jpeg);
        } else result = NowArtworkWorkerResult::DOWNLOAD;
      } else {
        http.end();
        result = lookupComplete ? NowArtworkWorkerResult::NO_MATCH
                                : (lookupSize > static_cast<int>(NOW_ART_MAX_LOOKUP_BYTES)
                                       ? NowArtworkWorkerResult::LOOKUP_TOO_LARGE
                                       : NowArtworkWorkerResult::NETWORK);
      }
    } else {
      http.end();
      result = response > 0 ? NowArtworkWorkerResult::LOOKUP_HTTP
                            : NowArtworkWorkerResult::NETWORK;
    }
  }
  if (nowArtworkWorkerCancelled(generation)) {
    success = false;
    result = NowArtworkWorkerResult::CANCELLED;
  }
  const uint32_t stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr);
  nowArtworkRtcStage = 0;
  portENTER_CRITICAL(&nowArtworkMux);
  nowArtworkWorkerSuccess = success;
  nowArtworkWorkerResult = result;
  nowArtworkWorkerStackFreeBytes = stackFreeBytes;
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
  NowArtworkWorkerResult result;
  uint32_t stackFreeBytes;
  portENTER_CRITICAL(&nowArtworkMux);
  ready = nowArtworkWorkerReady;
  success = nowArtworkWorkerSuccess;
  result = nowArtworkWorkerResult;
  stackFreeBytes = nowArtworkWorkerStackFreeBytes;
  nowArtworkWorkerReady = false;
  portEXIT_CRITICAL(&nowArtworkMux);
  if (!ready) return;
  if (nowArtworkWorkerKey != nowArtworkTrackKey) {
    nowArtworkState = "settling";
    return;
  }
  if (!success || !nowArtworkCacheBytes) {
    switch (result) {
      case NowArtworkWorkerResult::NO_MATCH: nowArtworkState = "no-match"; break;
      case NowArtworkWorkerResult::LOOKUP_HTTP: nowArtworkState = "lookup-http"; break;
      case NowArtworkWorkerResult::LOOKUP_TOO_LARGE: nowArtworkState = "lookup-large"; break;
      case NowArtworkWorkerResult::DOWNLOAD: nowArtworkState = "download-failed"; break;
      case NowArtworkWorkerResult::DECODE: nowArtworkState = "decode-failed"; break;
      case NowArtworkWorkerResult::CANCELLED:
        nowArtworkState = thermalWarning || thermalThrottled ? "temperature-paused" : "settling";
        return;
      default: nowArtworkState = "network-failed"; break;
    }
    logLine(String("NOW artwork failed: ") + nowArtworkState +
            " stack_free=" + String(stackFreeBytes));
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
    uint32_t nextGeneration = nowArtworkGeneration + 1U;
    if (nextGeneration == 0) nextGeneration = 1U;
    nowArtworkGeneration = nextGeneration;
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
  if (!nowArtworkLayoutUsesImage() || !nowArtworkTrackKey ||
      nowArtworkVisibleKey == nowArtworkTrackKey || nowArtworkWorkerRunning) return;
  const uint32_t now = millis();
  if (!deadlineReached(now, nowArtworkTrackChangedMs + NOW_ART_TRACK_SETTLE_MS)) {
    nowArtworkState = "settling";
    return;
  }
  const bool portalStationStable = stationNetworkReady() &&
      wifiTestState == WifiTestState::IDLE && !portalWifiScanActive &&
      !savedWifiScanActive && !ntpRequestActive && !portalCloseRequested;
  if (portalActive && !portalStationStable) {
    nowArtworkState = "portal-paused";
    return;
  }
  if (firmwareTransferActive() || updateState == UpdateState::CHECKING) {
    nowArtworkState = "ota-paused";
    return;
  }
  if (thermalWarning || thermalThrottled || thermalSafeMode || temperatureSensorFault) {
    nowArtworkState = "temperature-paused";
    return;
  }
  if ((nowArtworkRetryAtMs && !deadlineReached(now, nowArtworkRetryAtMs)) ||
      !deadlineReached(now, nowArtworkNextRequestMs)) return;
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
  nowArtworkWorkerGeneration = nowArtworkGeneration;
  nowArtworkWorkerRunning = true;
  nowArtworkState = "lookup";
  nowArtworkNextRequestMs = millis() + NOW_ART_REQUEST_SPACING_MS;
  if (xTaskCreate(nowArtworkWorker, "now-artwork", NOW_ART_WORKER_STACK_BYTES,
                  nullptr, 1, nullptr) != pdPASS) {
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
const char *nowArtworkInterruptedStageName() { return nowArtworkInterruptedStage.c_str(); }

void consumeNowArtworkResetBreadcrumb() {
  const uint8_t interruptedStage = nowArtworkRtcMagic == NOW_ART_RTC_MAGIC ? nowArtworkRtcStage : 0;
  nowArtworkRtcMagic = NOW_ART_RTC_MAGIC;
  nowArtworkRtcStage = 0;
  if (!interruptedStage || esp_reset_reason() == ESP_RST_POWERON) return;
  nowArtworkInterruptedStage = interruptedStage == 1 ? "lookup"
      : interruptedStage == 2 ? "download"
      : interruptedStage == 3 ? "decode" : "unknown";
  logLine(String("previous reset interrupted NOW artwork stage=") +
          nowArtworkInterruptedStage + " reset=" + resetReasonName(esp_reset_reason()));
}
