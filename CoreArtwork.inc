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
constexpr uint8_t NOW_ART_LOOKUP_ATTEMPTS = 5;
constexpr uint8_t NOW_ART_MAX_CANDIDATES = 3;
constexpr uint32_t NOW_ART_LOOKUP_RETRY_MS = 1200UL;
constexpr uint32_t NOW_ART_TRANSPORT_BACKOFF_MS = 500UL;
constexpr uint32_t NOW_ART_WORKER_BUDGET_MS = 35000UL;
constexpr uint32_t NOW_ART_LOOKUP_CONNECT_TIMEOUT_MS = 5000UL;
constexpr uint32_t NOW_ART_LOOKUP_TIMEOUT_MS = 15000UL;
constexpr size_t NOW_ART_MAX_JPEG_BYTES = 220UL * 1024UL;
constexpr size_t NOW_ART_MAX_LOOKUP_BYTES = 48UL * 1024UL;
constexpr size_t NOW_ART_BLE_MIN_FREE_HEAP = 80UL * 1024UL;
constexpr size_t NOW_ART_BLE_MIN_LARGEST_BLOCK = 32UL * 1024UL;
constexpr uint32_t NOW_ART_WORKER_STACK_BYTES = 14UL * 1024UL;
constexpr uint32_t NOW_ART_RTC_MAGIC = 0x4E415254UL;  // "NART"

enum class NowArtworkWorkerResult : uint8_t {
  NONE,
  READY,
  NETWORK,
  LOOKUP_TIMEOUT,
  LOOKUP_HTTP,
  LOOKUP_TOO_LARGE,
  NO_MATCH,
  NO_ARTWORK,
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
volatile int16_t nowArtworkWorkerAppleHttpCode = 0;
volatile int16_t nowArtworkWorkerHttpCode = 0;
volatile int16_t nowArtworkWorkerDownloadCode = 0;
volatile uint8_t nowArtworkWorkerAttempts = 0;
volatile uint8_t nowArtworkWorkerCandidates = 0;
volatile uint32_t nowArtworkWorkerElapsedMs = 0;
volatile uint32_t nowArtworkGeneration = 1;
RTC_DATA_ATTR uint32_t nowArtworkRtcMagic = 0;
RTC_DATA_ATTR uint8_t nowArtworkRtcStage = 0;
uint64_t nowArtworkWorkerKey = 0;
uint32_t nowArtworkWorkerGeneration = 0;
uint32_t nowArtworkWorkerStartedMs = 0;
uint32_t nowArtworkWorkerDeadlineMs = 0;
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
         thermalSafeMode || temperatureSensorFault ||
         (nowArtworkWorkerDeadlineMs != 0 &&
          deadlineReached(millis(), nowArtworkWorkerDeadlineMs));
}

bool nowArtworkWorkerBudgetExpired() {
  return nowArtworkWorkerDeadlineMs != 0 &&
         deadlineReached(millis(), nowArtworkWorkerDeadlineMs);
}

bool nowArtworkWaitForTransportRetry(uint32_t generation, uint8_t completedAttempt) {
  const uint32_t startedMs = millis();
  const uint32_t waitMs = NOW_ART_TRANSPORT_BACKOFF_MS * completedAttempt;
  while (!elapsed(millis(), startedMs, waitMs)) {
    if (nowArtworkWorkerCancelled(generation)) return false;
    delay(20);
  }
  return true;
}

uint8_t nowArtworkReadReleaseGroups(HTTPClient &http, String *mbids, uint8_t capacity,
                                    bool &complete, bool &timedOut, uint32_t generation) {
  WiFiClient *stream = http.getStreamPtr();
  const int announced = http.getSize();
  complete = false;
  timedOut = false;
  if (!stream || !mbids || !capacity) return 0;
  size_t total = 0;
  uint8_t found = 0;
  MilestoneCoreLogic::MusicBrainzReleaseGroupParser parser;
  MilestoneCoreLogic::resetMusicBrainzReleaseGroupParser(parser);
  char uuid[37] = {};
  uint8_t chunk[256];
  uint32_t lastDataMs = millis();
  while (total < NOW_ART_MAX_LOOKUP_BYTES &&
         (announced < 0 || total < static_cast<size_t>(announced))) {
    if (nowArtworkWorkerCancelled(generation)) return 0;
    const size_t available = stream->available();
    if (!available) {
      if (!http.connected()) break;
      if (elapsed(millis(), lastDataMs, NOW_ART_LOOKUP_TIMEOUT_MS)) {
        timedOut = true;
        return found;
      }
      delay(1);
      continue;
    }
    const size_t take = min(available, min(sizeof(chunk), NOW_ART_MAX_LOOKUP_BYTES - total));
    const int received = stream->readBytes(chunk, take);
    if (received <= 0) {
      timedOut = http.connected();
      return found;
    }
    lastDataMs = millis();
    total += static_cast<size_t>(received);
    for (int i = 0; i < received; ++i) {
      if (!MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(parser, chunk + i, 1, uuid)) {
        continue;
      }
      bool duplicate = false;
      for (uint8_t candidate = 0; candidate < found; ++candidate) {
        if (mbids[candidate] == uuid) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) mbids[found++] = uuid;
      if (found == capacity) {
        complete = true;
        return found;
      }
    }
  }
  complete = announced >= 0 ? total == static_cast<size_t>(announced) : !http.connected();
  return found;
}

enum class NowArtworkDownloadResult : uint8_t { READY, NOT_FOUND, FAILED, CANCELLED };

NowArtworkDownloadResult nowArtworkReadJpeg(const String &url, uint8_t *&jpeg, size_t &jpegSize,
                                            int16_t &httpCode, uint32_t generation) {
  jpeg = nullptr;
  jpegSize = 0;
  httpCode = 0;
  NetworkClientSecure client;
  client.setCACert(MILESTONE_UPDATE_ROOT_CA);
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setRedirectLimit(4);
  if (!http.begin(client, url)) return NowArtworkDownloadResult::FAILED;
  http.addHeader("User-Agent", String("MILESTONE-Now/") + FIRMWARE_VERSION +
                               " (https://github.com/CXITRON/MILESTONE-Core)");
  const int response = http.GET();
  httpCode = static_cast<int16_t>(response);
  if (response != HTTP_CODE_OK) {
    http.end();
    return response == HTTP_CODE_NOT_FOUND ? NowArtworkDownloadResult::NOT_FOUND
                                           : NowArtworkDownloadResult::FAILED;
  }
  const int announced = http.getSize();
  if (announced > static_cast<int>(NOW_ART_MAX_JPEG_BYTES)) {
    http.end();
    return NowArtworkDownloadResult::FAILED;
  }
  const size_t capacity = announced > 0 ? static_cast<size_t>(announced) : NOW_ART_MAX_JPEG_BYTES;
  jpeg = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg) {
    http.end();
    return NowArtworkDownloadResult::FAILED;
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
  const bool cancelled = nowArtworkWorkerCancelled(generation);
  const bool complete = !cancelled && jpegSize > 128U &&
      (announced < 0 || jpegSize == static_cast<size_t>(announced));
  if (!complete) {
    heap_caps_free(jpeg);
    jpeg = nullptr;
    jpegSize = 0;
  }
  return cancelled ? NowArtworkDownloadResult::CANCELLED
                   : complete ? NowArtworkDownloadResult::READY
                              : NowArtworkDownloadResult::FAILED;
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

String nowArtworkSearchUrl(bool albumQuery, const String &value, const String &artist) {
  String query = albumQuery
      ? "https://musicbrainz.org/ws/2/release-group/?fmt=xml&limit=3&query=release%3A%22"
      : "https://musicbrainz.org/ws/2/recording/?fmt=xml&limit=3&query=recording%3A%22";
  query += nowArtworkUrlEncode(value.c_str());
  query += "%22%20AND%20artist%3A%22";
  query += nowArtworkUrlEncode(artist.c_str());
  query += "%22";
  return query;
}

bool nowArtworkReadAppleUrl(HTTPClient &http, String &url, bool &complete, bool &timedOut,
                            uint32_t generation) {
  WiFiClient *stream = http.getStreamPtr();
  const int announced = http.getSize();
  complete = false;
  timedOut = false;
  if (!stream) return false;
  size_t total = 0;
  url = "";
  MilestoneCoreLogic::AppleArtworkUrlParser parser;
  MilestoneCoreLogic::resetAppleArtworkUrlParser(parser);
  uint8_t chunk[256];
  uint32_t lastDataMs = millis();
  while (total < NOW_ART_MAX_LOOKUP_BYTES &&
         (announced < 0 || total < static_cast<size_t>(announced))) {
    if (nowArtworkWorkerCancelled(generation)) return false;
    const size_t available = stream->available();
    if (!available) {
      if (!http.connected()) break;
      if (elapsed(millis(), lastDataMs, NOW_ART_LOOKUP_TIMEOUT_MS)) {
        timedOut = true;
        return false;
      }
      delay(1);
      continue;
    }
    const size_t take = min(available, min(sizeof(chunk), NOW_ART_MAX_LOOKUP_BYTES - total));
    const int received = stream->readBytes(chunk, take);
    if (received <= 0) {
      timedOut = http.connected();
      return false;
    }
    lastDataMs = millis();
    total += static_cast<size_t>(received);
    if (MilestoneCoreLogic::feedAppleArtworkUrlParser(
            parser, chunk, static_cast<size_t>(received))) {
      url = parser.value;
      complete = true;
      return url.startsWith("https://");
    }
  }
  complete = announced >= 0 ? total == static_cast<size_t>(announced) : !http.connected();
  return false;
}

void nowArtworkWorker(void *) {
  const uint32_t generation = nowArtworkWorkerGeneration;
  const uint32_t workerStartedMs = nowArtworkWorkerStartedMs;
  bool success = false;
  NowArtworkWorkerResult result = NowArtworkWorkerResult::NETWORK;
  int16_t finalAppleHttpCode = 0;
  int16_t finalHttpCode = 0;
  int16_t finalDownloadCode = 0;
  uint8_t attemptsUsed = 0;
  String candidates[NOW_ART_MAX_CANDIDATES];
  uint8_t candidateCount = 0;
  uint32_t lastLookupRequestMs = 0;
  nowArtworkRtcMagic = NOW_ART_RTC_MAGIC;
  nowArtworkRtcStage = 1;
  {
    String appleArtworkUrl;
    String appleTerm = String(nowArtworkWorkerTitle) + " " + nowArtworkWorkerArtist;
    if (nowArtworkWorkerAlbum[0]) appleTerm += String(" ") + nowArtworkWorkerAlbum;
    String appleQuery = "https://itunes.apple.com/search?country=KR&media=music&entity=song&limit=1&term=";
    appleQuery += nowArtworkUrlEncode(appleTerm.c_str());
    for (uint8_t attempt = 1; attempt <= NOW_ART_LOOKUP_ATTEMPTS; ++attempt) {
      if (nowArtworkWorkerCancelled(generation)) break;
      bool responseComplete = false;
      {
        NetworkClientSecure appleClient;
        appleClient.setCACert(MILESTONE_UPDATE_ROOT_CA);
        appleClient.setHandshakeTimeout(5);
        HTTPClient appleHttp;
        appleHttp.setConnectTimeout(NOW_ART_LOOKUP_CONNECT_TIMEOUT_MS);
        appleHttp.setTimeout(NOW_ART_LOOKUP_TIMEOUT_MS);
        appleHttp.setAcceptEncoding("identity");
        if (appleHttp.begin(appleClient, appleQuery)) {
          appleHttp.addHeader("User-Agent", String("MILESTONE-Now/") + FIRMWARE_VERSION);
          const int response = appleHttp.GET();
          finalAppleHttpCode = static_cast<int16_t>(response);
          if (response == HTTP_CODE_OK) {
            bool appleTimedOut = false;
            nowArtworkReadAppleUrl(
                appleHttp, appleArtworkUrl, responseComplete, appleTimedOut, generation);
            if (appleTimedOut) responseComplete = false;
          }
          appleHttp.end();
        } else {
          finalAppleHttpCode = HTTPC_ERROR_CONNECTION_REFUSED;
        }
      }
      if (appleArtworkUrl.length() || nowArtworkWorkerCancelled(generation) ||
          !MilestoneCoreLogic::shouldRetryTransientHttpFailure(
              finalAppleHttpCode, responseComplete, attempt, NOW_ART_LOOKUP_ATTEMPTS)) break;
      logLine(String("NOW Apple artwork retry after code=") + finalAppleHttpCode +
              " attempt=" + attempt);
      if (!nowArtworkWaitForTransportRetry(generation, attempt)) break;
    }
    // The Apple search TLS client is now destroyed before the image TLS starts.
    if (appleArtworkUrl.length() && !nowArtworkWorkerCancelled(generation)) {
      nowArtworkRtcStage = 2;
      uint8_t *jpeg = nullptr;
      size_t jpegSize = 0;
      const NowArtworkDownloadResult download = nowArtworkReadJpeg(
          appleArtworkUrl, jpeg, jpegSize, finalDownloadCode, generation);
      if (download == NowArtworkDownloadResult::READY) {
        nowArtworkRtcStage = 3;
        success = !nowArtworkWorkerCancelled(generation) &&
                  nowArtworkDecodeJpeg(jpeg, jpegSize);
        heap_caps_free(jpeg);
        if (success) {
          result = NowArtworkWorkerResult::READY;
          candidateCount = 1;
        }
      }
    }
  }

  nowArtworkRtcStage = 1;
  if (!success) {
    const String title = nowArtworkLuceneEscape(nowArtworkWorkerTitle);
    const String album = nowArtworkLuceneEscape(nowArtworkWorkerAlbum);
    const String artist = nowArtworkLuceneEscape(nowArtworkWorkerArtist);
    String queries[2];
    uint8_t queryCount = 0;
    if (album.length()) queries[queryCount++] = nowArtworkSearchUrl(true, album, artist);
    if (title.length()) queries[queryCount++] = nowArtworkSearchUrl(false, title, artist);
    for (uint8_t queryIndex = 0; queryIndex < queryCount && !candidateCount; ++queryIndex) {
      bool definitiveNoMatch = false;
      for (uint8_t attempt = 1; attempt <= NOW_ART_LOOKUP_ATTEMPTS; ++attempt) {
      if (nowArtworkWorkerCancelled(generation)) break;
      ++attemptsUsed;
      bool retryable = false;
      NetworkClientSecure client;
      client.setCACert(MILESTONE_UPDATE_ROOT_CA);
      client.setHandshakeTimeout(5);
      HTTPClient http;
      http.setConnectTimeout(NOW_ART_LOOKUP_CONNECT_TIMEOUT_MS);
      http.setTimeout(NOW_ART_LOOKUP_TIMEOUT_MS);
      http.setAcceptEncoding("identity");
      if (!http.begin(client, queries[queryIndex])) {
        finalHttpCode = HTTPC_ERROR_CONNECTION_REFUSED;
        result = NowArtworkWorkerResult::NETWORK;
        retryable = true;
      } else {
        http.addHeader("User-Agent", String("MILESTONE-Now/") + FIRMWARE_VERSION +
                                     " (https://github.com/CXITRON/MILESTONE-Core)");
        while (lastLookupRequestMs &&
               !elapsed(millis(), lastLookupRequestMs, NOW_ART_LOOKUP_RETRY_MS)) {
          if (nowArtworkWorkerCancelled(generation)) break;
          delay(20);
        }
        if (nowArtworkWorkerCancelled(generation)) {
          http.end();
          break;
        }
        lastLookupRequestMs = millis();
        const int response = http.GET();
        finalHttpCode = static_cast<int16_t>(response);
        if (response == HTTP_CODE_OK) {
          const int lookupSize = http.getSize();
          bool lookupComplete = false;
          bool lookupTimedOut = false;
          candidateCount = nowArtworkReadReleaseGroups(
              http, candidates, NOW_ART_MAX_CANDIDATES, lookupComplete, lookupTimedOut, generation);
          http.end();
          if (candidateCount) break;
          result = lookupComplete ? NowArtworkWorkerResult::NO_MATCH
              : lookupTimedOut ? NowArtworkWorkerResult::LOOKUP_TIMEOUT
              : lookupSize > static_cast<int>(NOW_ART_MAX_LOOKUP_BYTES)
                  ? NowArtworkWorkerResult::LOOKUP_TOO_LARGE
                  : NowArtworkWorkerResult::NETWORK;
          definitiveNoMatch = result == NowArtworkWorkerResult::NO_MATCH;
          retryable = result == NowArtworkWorkerResult::LOOKUP_TIMEOUT ||
                      result == NowArtworkWorkerResult::NETWORK;
        } else {
          http.end();
          result = response == HTTPC_ERROR_READ_TIMEOUT ? NowArtworkWorkerResult::LOOKUP_TIMEOUT
              : response > 0 ? NowArtworkWorkerResult::LOOKUP_HTTP
                             : NowArtworkWorkerResult::NETWORK;
          retryable = response < 0 || response == HTTP_CODE_TOO_MANY_REQUESTS || response >= 500;
        }
      }
      if (!retryable || attempt == NOW_ART_LOOKUP_ATTEMPTS ||
          nowArtworkWorkerCancelled(generation)) break;
      logLine(String("NOW artwork lookup retry after code=") + finalHttpCode +
              " attempt=" + attempt);
      if (!nowArtworkWaitForTransportRetry(generation, attempt)) break;
      }
      // Only a real zero-result response may advance from album matching to
      // title + artist matching. Transport and server failures keep their diagnosis.
      if (!candidateCount && !definitiveNoMatch) break;
    }
  }

  if (!success && candidateCount && !nowArtworkWorkerCancelled(generation)) {
    bool sawDownloadFailure = false;
    bool sawDecodeFailure = false;
    bool allNotFound = true;
    nowArtworkRtcStage = 2;
    for (uint8_t candidate = 0; candidate < candidateCount; ++candidate) {
      uint8_t *jpeg = nullptr;
      size_t jpegSize = 0;
      int16_t downloadCode = 0;
      const NowArtworkDownloadResult download = nowArtworkReadJpeg(
          "https://coverartarchive.org/release-group/" + candidates[candidate] + "/front-250",
          jpeg, jpegSize, downloadCode, generation);
      finalDownloadCode = downloadCode;
      if (download == NowArtworkDownloadResult::CANCELLED) break;
      if (download == NowArtworkDownloadResult::NOT_FOUND) continue;
      allNotFound = false;
      if (download != NowArtworkDownloadResult::READY) {
        sawDownloadFailure = true;
        continue;
      }
      nowArtworkRtcStage = 3;
      success = !nowArtworkWorkerCancelled(generation) && nowArtworkDecodeJpeg(jpeg, jpegSize);
      heap_caps_free(jpeg);
      if (success) {
        result = NowArtworkWorkerResult::READY;
        break;
      }
      sawDecodeFailure = true;
      nowArtworkRtcStage = 2;
    }
    if (!success && !nowArtworkWorkerCancelled(generation)) {
      result = allNotFound ? NowArtworkWorkerResult::NO_ARTWORK
          : sawDownloadFailure ? NowArtworkWorkerResult::DOWNLOAD
          : sawDecodeFailure ? NowArtworkWorkerResult::DECODE
                             : NowArtworkWorkerResult::DOWNLOAD;
    }
  }
  if (nowArtworkWorkerBudgetExpired()) {
    success = false;
    result = NowArtworkWorkerResult::LOOKUP_TIMEOUT;
  } else if (nowArtworkWorkerCancelled(generation)) {
    success = false;
    result = NowArtworkWorkerResult::CANCELLED;
  }
  const uint32_t stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr);
  nowArtworkRtcStage = 0;
  portENTER_CRITICAL(&nowArtworkMux);
  nowArtworkWorkerSuccess = success;
  nowArtworkWorkerResult = result;
  nowArtworkWorkerStackFreeBytes = stackFreeBytes;
  nowArtworkWorkerAppleHttpCode = finalAppleHttpCode;
  nowArtworkWorkerHttpCode = finalHttpCode;
  nowArtworkWorkerDownloadCode = finalDownloadCode;
  nowArtworkWorkerAttempts = attemptsUsed;
  nowArtworkWorkerCandidates = candidateCount;
  nowArtworkWorkerElapsedMs = millis() - workerStartedMs;
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
  int16_t appleHttpCode;
  int16_t httpCode;
  int16_t downloadCode;
  uint8_t attempts;
  uint8_t candidates;
  uint32_t workerElapsedMs;
  portENTER_CRITICAL(&nowArtworkMux);
  ready = nowArtworkWorkerReady;
  success = nowArtworkWorkerSuccess;
  result = nowArtworkWorkerResult;
  stackFreeBytes = nowArtworkWorkerStackFreeBytes;
  appleHttpCode = nowArtworkWorkerAppleHttpCode;
  httpCode = nowArtworkWorkerHttpCode;
  downloadCode = nowArtworkWorkerDownloadCode;
  attempts = nowArtworkWorkerAttempts;
  candidates = nowArtworkWorkerCandidates;
  workerElapsedMs = nowArtworkWorkerElapsedMs;
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
      case NowArtworkWorkerResult::LOOKUP_TIMEOUT: nowArtworkState = "lookup-timeout"; break;
      case NowArtworkWorkerResult::LOOKUP_HTTP: nowArtworkState = "lookup-http"; break;
      case NowArtworkWorkerResult::LOOKUP_TOO_LARGE: nowArtworkState = "lookup-large"; break;
      case NowArtworkWorkerResult::DOWNLOAD: nowArtworkState = "download-failed"; break;
      case NowArtworkWorkerResult::NO_ARTWORK: nowArtworkState = "art-not-found"; break;
      case NowArtworkWorkerResult::DECODE: nowArtworkState = "decode-failed"; break;
      case NowArtworkWorkerResult::CANCELLED:
        nowArtworkState = thermalWarning || thermalThrottled ? "temperature-paused" : "settling";
        return;
      default: nowArtworkState = "network-failed"; break;
    }
    logLine(String("NOW artwork failed: ") + nowArtworkState +
            " apple_http=" + appleHttpCode + " http=" + httpCode +
            " attempts=" + attempts +
            " candidates=" + candidates + " download_http=" + downloadCode +
            " elapsed_ms=" + String(workerElapsedMs) +
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
  logLine(String("NOW artwork loaded elapsed_ms=") + workerElapsedMs +
          " apple_http=" + appleHttpCode + " http=" + httpCode +
          " attempts=" + attempts + " candidates=" + candidates +
          " download_http=" + downloadCode);
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
  const bool liveBluetooth = bluetoothNowPlayingHasLiveConnection();
  const size_t minimumFreeHeap = liveBluetooth ? NOW_ART_BLE_MIN_FREE_HEAP : 55UL * 1024UL;
  const size_t minimumLargestBlock = liveBluetooth ? NOW_ART_BLE_MIN_LARGEST_BLOCK : 24UL * 1024UL;
  if (ESP.getFreeHeap() < minimumFreeHeap || ESP.getMaxAllocHeap() < minimumLargestBlock ||
      !nowArtworkCacheBytes) {
    nowArtworkState = "low-memory";
    nowArtworkRetryAtMs = millis() + NOW_ART_RETRY_MS;
    return;
  }
  strlcpy(nowArtworkWorkerTitle, title.c_str(), sizeof(nowArtworkWorkerTitle));
  strlcpy(nowArtworkWorkerArtist, artist.c_str(), sizeof(nowArtworkWorkerArtist));
  strlcpy(nowArtworkWorkerAlbum, album.c_str(), sizeof(nowArtworkWorkerAlbum));
  nowArtworkWorkerKey = nowArtworkTrackKey;
  nowArtworkWorkerGeneration = nowArtworkGeneration;
  nowArtworkWorkerStartedMs = millis();
  nowArtworkWorkerDeadlineMs = nowArtworkWorkerStartedMs + NOW_ART_WORKER_BUDGET_MS;
  nowArtworkWorkerRunning = true;
  nowArtworkState = "lookup";
  nowArtworkNextRequestMs = millis() + NOW_ART_REQUEST_SPACING_MS;
  if (xTaskCreate(nowArtworkWorker, "now-artwork", NOW_ART_WORKER_STACK_BYTES,
                  nullptr, 1, nullptr) != pdPASS) {
    nowArtworkWorkerRunning = false;
    nowArtworkWorkerDeadlineMs = 0;
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
int nowArtworkLastAppleHttpCode() { return nowArtworkWorkerAppleHttpCode; }
int nowArtworkLastHttpCode() { return nowArtworkWorkerHttpCode; }
int nowArtworkLastDownloadCode() { return nowArtworkWorkerDownloadCode; }
uint8_t nowArtworkLastAttempts() { return nowArtworkWorkerAttempts; }
uint8_t nowArtworkLastCandidates() { return nowArtworkWorkerCandidates; }
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
