// MILESTONE Core v1.9.0 custom media storage and playback layer.
// Original JPEG/GIF/video files are converted in the setup browser. The
// device accepts only bounded, checksummed MSM1 128x128 one-bit files.

constexpr uint8_t MEDIA_MAX_ITEMS = 64;
constexpr uint8_t MEDIA_NAME_BYTES = 49;
constexpr uint8_t MEDIA_ENTRY_ENABLED = 0x01;
constexpr uint8_t MEDIA_ENTRY_ANIMATED = 0x02;
constexpr uint8_t MEDIA_ENTRY_LOOP = 0x04;
constexpr uint8_t MEDIA_ENTRY_KNOWN_FLAGS =
    MEDIA_ENTRY_ENABLED | MEDIA_ENTRY_ANIMATED | MEDIA_ENTRY_LOOP;
constexpr size_t MEDIA_INDEX_HEADER_BYTES = 20;
constexpr size_t MEDIA_INDEX_ENTRY_BYTES = 65;
constexpr uint8_t MEDIA_INDEX_VERSION = 1;
constexpr size_t MEDIA_FS_RESERVE_BYTES = 16U * 1024U;
constexpr size_t MEDIA_FRAME_RECORD_MAX_BYTES =
    MilestoneMedia::FRAME_HEADER_BYTES + MilestoneMedia::FRAME_BYTES + 32U;
constexpr char MEDIA_DIRECTORY[] = "/media";
constexpr char MEDIA_INDEX_A[] = "/media/index.a";
constexpr char MEDIA_INDEX_B[] = "/media/index.b";
constexpr char MEDIA_UPLOAD_TEMP[] = "/media/upload.tmp";

struct MediaEntry {
  uint32_t id = 0;
  uint32_t fileSize = 0;
  uint32_t durationMs = 0;
  uint16_t frameCount = 0;
  uint8_t displaySeconds = 8;
  uint8_t flags = MEDIA_ENTRY_ENABLED;
  char name[MEDIA_NAME_BYTES] = {};
};

struct MediaCatalog {
  uint32_t generation = 0;
  uint8_t count = 0;
  MediaEntry entries[MEDIA_MAX_ITEMS];
};

MediaCatalog mediaCatalog;
MediaCatalog mediaCatalogScratch;
bool mediaStorageReady = false;
char mediaIndexActiveSlot = 0;
uint8_t *mediaFrameBuffer = nullptr;
uint8_t *mediaFrameRecordBuffer = nullptr;
File mediaPlaybackFile;
MilestoneMedia::Header mediaPlaybackHeader{};
uint16_t mediaPlaybackFrameIndex = 0;
uint8_t mediaPlaybackCatalogIndex = 0xFF;
uint8_t mediaRotationCursor = 0;
uint32_t mediaPlaybackNextFrameMs = 0;
bool mediaPlaybackFrameReady = false;
bool mediaPlaybackFinished = false;
bool mediaPlaybackFailed = false;

File mediaUploadFile;
bool mediaUploadActive = false;
bool mediaUploadComplete = false;
bool mediaUploadFailed = false;
size_t mediaUploadExpectedSize = 0;
size_t mediaUploadWritten = 0;
uint8_t mediaUploadDisplaySeconds = 8;
String mediaUploadName;
String mediaUploadError;

String mediaPath(uint32_t id) {
  char path[32];
  snprintf(path, sizeof(path), "/media/%08lx.msm", static_cast<unsigned long>(id));
  return String(path);
}

size_t mediaCatalogBytesUsed(const MediaCatalog &catalog) {
  size_t used = 0;
  for (uint8_t i = 0; i < catalog.count; ++i) used += catalog.entries[i].fileSize;
  return used;
}

size_t mediaStorageLimit() {
  if (!mediaStorageReady) return 0;
  const size_t total = LittleFS.totalBytes();
  if (total <= MEDIA_FS_RESERVE_BYTES) return 0;
  return min(static_cast<size_t>(MilestoneMedia::MAX_FILE_BYTES), total - MEDIA_FS_RESERVE_BYTES);
}

bool mediaHasEnabledItems() {
  if (!mediaStorageReady) return false;
  for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
    if ((mediaCatalog.entries[i].flags & MEDIA_ENTRY_ENABLED) != 0) return true;
  }
  return false;
}

int findMediaEntry(uint32_t id) {
  for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
    if (mediaCatalog.entries[i].id == id) return i;
  }
  return -1;
}

bool mediaGenerationNewer(uint32_t lhs, uint32_t rhs) {
  return static_cast<int32_t>(lhs - rhs) > 0;
}

void serializeMediaEntry(const MediaEntry &entry, uint8_t *out) {
  memset(out, 0, MEDIA_INDEX_ENTRY_BYTES);
  MilestoneMedia::writeLe32(out, entry.id);
  MilestoneMedia::writeLe32(out + 4, entry.fileSize);
  MilestoneMedia::writeLe32(out + 8, entry.durationMs);
  MilestoneMedia::writeLe16(out + 12, entry.frameCount);
  out[14] = entry.displaySeconds;
  out[15] = entry.flags;
  memcpy(out + 16, entry.name, MEDIA_NAME_BYTES);
}

bool deserializeMediaEntry(const uint8_t *data, MediaEntry &entry) {
  entry = MediaEntry();
  entry.id = MilestoneMedia::readLe32(data);
  entry.fileSize = MilestoneMedia::readLe32(data + 4);
  entry.durationMs = MilestoneMedia::readLe32(data + 8);
  entry.frameCount = MilestoneMedia::readLe16(data + 12);
  entry.displaySeconds = data[14];
  entry.flags = data[15];
  memcpy(entry.name, data + 16, MEDIA_NAME_BYTES);
  entry.name[MEDIA_NAME_BYTES - 1] = '\0';
  if (entry.id == 0 || entry.fileSize < MilestoneMedia::HEADER_BYTES ||
      entry.fileSize > MilestoneMedia::MAX_FILE_BYTES || entry.frameCount == 0 ||
      entry.frameCount > MilestoneMedia::MAX_FRAMES || entry.displaySeconds < 3 ||
      entry.displaySeconds > 60 || (entry.flags & ~MEDIA_ENTRY_KNOWN_FLAGS) != 0 ||
      entry.name[0] == '\0') {
    return false;
  }
  return true;
}

bool readMediaIndex(const char *path, MediaCatalog &catalog) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const size_t size = file.size();
  const size_t maximum = MEDIA_INDEX_HEADER_BYTES +
                         static_cast<size_t>(MEDIA_MAX_ITEMS) * MEDIA_INDEX_ENTRY_BYTES;
  if (size < MEDIA_INDEX_HEADER_BYTES || size > maximum) {
    file.close();
    return false;
  }
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer) {
    file.close();
    return false;
  }
  const size_t read = file.read(buffer, size);
  file.close();
  if (read != size || memcmp(buffer, "MSI1", 4) != 0 ||
      buffer[4] != MEDIA_INDEX_VERSION || buffer[6] != 0 || buffer[7] != 0) {
    free(buffer);
    return false;
  }
  const uint8_t count = buffer[5];
  const uint32_t payloadSize = MilestoneMedia::readLe32(buffer + 12);
  const uint32_t expectedCrc = MilestoneMedia::readLe32(buffer + 16);
  if (count > MEDIA_MAX_ITEMS || payloadSize != static_cast<uint32_t>(count) * MEDIA_INDEX_ENTRY_BYTES ||
      size != MEDIA_INDEX_HEADER_BYTES + payloadSize ||
      MilestoneMedia::crc32(buffer + MEDIA_INDEX_HEADER_BYTES, payloadSize) != expectedCrc) {
    free(buffer);
    return false;
  }
  catalog = MediaCatalog();
  catalog.generation = MilestoneMedia::readLe32(buffer + 8);
  catalog.count = count;
  for (uint8_t i = 0; i < count; ++i) {
    if (!deserializeMediaEntry(buffer + MEDIA_INDEX_HEADER_BYTES +
                                static_cast<size_t>(i) * MEDIA_INDEX_ENTRY_BYTES,
                               catalog.entries[i])) {
      free(buffer);
      return false;
    }
    for (uint8_t previous = 0; previous < i; ++previous) {
      if (catalog.entries[previous].id == catalog.entries[i].id) {
        free(buffer);
        return false;
      }
    }
  }
  free(buffer);
  return true;
}

bool writeMediaIndex(const char *path, const MediaCatalog &catalog) {
  const uint32_t expectedGeneration = catalog.generation;
  const uint8_t expectedCount = catalog.count;
  const size_t payloadSize = static_cast<size_t>(catalog.count) * MEDIA_INDEX_ENTRY_BYTES;
  const size_t size = MEDIA_INDEX_HEADER_BYTES + payloadSize;
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer) return false;
  memset(buffer, 0, size);
  memcpy(buffer, "MSI1", 4);
  buffer[4] = MEDIA_INDEX_VERSION;
  buffer[5] = catalog.count;
  MilestoneMedia::writeLe32(buffer + 8, catalog.generation);
  MilestoneMedia::writeLe32(buffer + 12, static_cast<uint32_t>(payloadSize));
  for (uint8_t i = 0; i < catalog.count; ++i) {
    serializeMediaEntry(catalog.entries[i], buffer + MEDIA_INDEX_HEADER_BYTES +
                        static_cast<size_t>(i) * MEDIA_INDEX_ENTRY_BYTES);
  }
  MilestoneMedia::writeLe32(buffer + 16,
                            MilestoneMedia::crc32(buffer + MEDIA_INDEX_HEADER_BYTES, payloadSize));
  LittleFS.remove(path);
  File file = LittleFS.open(path, "w");
  bool success = file && file.write(buffer, size) == size;
  if (file) {
    file.flush();
    file.close();
  }
  free(buffer);
  if (!success) return false;
  MediaCatalog &verified = mediaCatalogScratch;
  return readMediaIndex(path, verified) && verified.generation == expectedGeneration &&
         verified.count == expectedCount;
}

bool commitMediaCatalog(const MediaCatalog &candidate) {
  if (!mediaStorageReady || candidate.count > MEDIA_MAX_ITEMS) return false;
  mediaCatalogScratch = candidate;
  mediaCatalogScratch.generation = mediaCatalog.generation + 1U;
  const char nextSlot = mediaIndexActiveSlot == 'a' ? 'b' : 'a';
  const char *path = nextSlot == 'a' ? MEDIA_INDEX_A : MEDIA_INDEX_B;
  if (!writeMediaIndex(path, mediaCatalogScratch)) return false;
  mediaCatalog = mediaCatalogScratch;
  mediaIndexActiveSlot = nextSlot;
  return true;
}

bool allocateMediaBuffers() {
  if (!mediaFrameBuffer) {
    mediaFrameBuffer = static_cast<uint8_t *>(
        heap_caps_malloc(MilestoneMedia::FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!mediaFrameBuffer) mediaFrameBuffer = static_cast<uint8_t *>(malloc(MilestoneMedia::FRAME_BYTES));
  }
  if (!mediaFrameRecordBuffer) {
    mediaFrameRecordBuffer = static_cast<uint8_t *>(
        heap_caps_malloc(MEDIA_FRAME_RECORD_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!mediaFrameRecordBuffer) mediaFrameRecordBuffer = static_cast<uint8_t *>(malloc(MEDIA_FRAME_RECORD_MAX_BYTES));
  }
  return mediaFrameBuffer && mediaFrameRecordBuffer;
}

bool validateMediaFile(const String &path, MilestoneMedia::Header &header,
                       MilestoneMedia::Error &error) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    error = MilestoneMedia::Error::FILE_SIZE;
    return false;
  }
  const size_t size = file.size();
  if (size < MilestoneMedia::HEADER_BYTES || size > MilestoneMedia::MAX_FILE_BYTES) {
    file.close();
    error = MilestoneMedia::Error::FILE_SIZE;
    return false;
  }
  uint8_t *data = static_cast<uint8_t *>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data && size <= 8192U) data = static_cast<uint8_t *>(malloc(size));
  if (!data) {
    file.close();
    error = MilestoneMedia::Error::NULL_ARGUMENT;
    return false;
  }
  const size_t read = file.read(data, size);
  file.close();
  const bool valid = read == size && MilestoneMedia::validateFile(data, size, &header, &error);
  free(data);
  return valid;
}

void closeMediaPlayback() {
  if (mediaPlaybackFile) mediaPlaybackFile.close();
  mediaPlaybackFrameIndex = 0;
  mediaPlaybackCatalogIndex = 0xFF;
  mediaPlaybackNextFrameMs = 0;
  mediaPlaybackFrameReady = false;
  mediaPlaybackFinished = false;
  mediaPlaybackFailed = false;
}

bool readMediaFrameRecord(bool firstFrame, uint16_t &delayMs) {
  if (!mediaPlaybackFile || !mediaFrameBuffer || !mediaFrameRecordBuffer) return false;
  if (mediaPlaybackFile.available() < static_cast<int>(MilestoneMedia::FRAME_HEADER_BYTES)) return false;
  if (mediaPlaybackFile.read(mediaFrameRecordBuffer, MilestoneMedia::FRAME_HEADER_BYTES) !=
      MilestoneMedia::FRAME_HEADER_BYTES) return false;
  const uint16_t dataSize = MilestoneMedia::readLe16(mediaFrameRecordBuffer + 3);
  if (dataSize > MilestoneMedia::FRAME_BYTES + 32U ||
      mediaPlaybackFile.available() < static_cast<int>(dataSize)) return false;
  if (mediaPlaybackFile.read(mediaFrameRecordBuffer + MilestoneMedia::FRAME_HEADER_BYTES, dataSize) != dataSize) {
    return false;
  }
  size_t consumed = 0;
  MilestoneMedia::Error error;
  return MilestoneMedia::decodeFrame(mediaFrameRecordBuffer,
      MilestoneMedia::FRAME_HEADER_BYTES + dataSize, firstFrame, mediaFrameBuffer,
      delayMs, consumed, &error) && consumed == MilestoneMedia::FRAME_HEADER_BYTES + dataSize;
}

bool beginMediaPlayback(uint8_t catalogIndex) {
  closeMediaPlayback();
  if (!mediaStorageReady || catalogIndex >= mediaCatalog.count || !allocateMediaBuffers()) return false;
  const MediaEntry &entry = mediaCatalog.entries[catalogIndex];
  if (entry.frameCount > 1 && !psramFound()) return false;
  mediaPlaybackFile = LittleFS.open(mediaPath(entry.id), "r");
  if (!mediaPlaybackFile) return false;
  uint8_t headerBytes[MilestoneMedia::HEADER_BYTES];
  if (mediaPlaybackFile.read(headerBytes, sizeof(headerBytes)) != sizeof(headerBytes)) {
    closeMediaPlayback();
    return false;
  }
  MilestoneMedia::Error error;
  if (!MilestoneMedia::parseHeader(headerBytes, entry.fileSize, mediaPlaybackHeader, &error) ||
      mediaPlaybackHeader.frameCount != entry.frameCount ||
      mediaPlaybackHeader.durationMs != entry.durationMs) {
    closeMediaPlayback();
    return false;
  }
  uint16_t delayMs = 0;
  if (!readMediaFrameRecord(true, delayMs)) {
    closeMediaPlayback();
    return false;
  }
  mediaPlaybackCatalogIndex = catalogIndex;
  mediaPlaybackFrameIndex = 1;
  mediaPlaybackFrameReady = true;
  const uint16_t minimumDelay = thermalThrottled ? 250U : 100U;
  mediaPlaybackNextFrameMs = millis() + (delayMs < minimumDelay ? minimumDelay : delayMs);
  return true;
}

bool selectNextMediaItem() {
  if (!mediaHasEnabledItems()) {
    closeMediaPlayback();
    return false;
  }
  for (uint8_t step = 0; step < mediaCatalog.count; ++step) {
    const uint8_t index = (mediaRotationCursor + step) % mediaCatalog.count;
    if ((mediaCatalog.entries[index].flags & MEDIA_ENTRY_ENABLED) == 0) continue;
    mediaRotationCursor = (index + 1U) % mediaCatalog.count;
    if (beginMediaPlayback(index)) return true;
    logLine(String("custom media skipped: id=") + String(mediaCatalog.entries[index].id));
    recordDiagnostic(MilestoneDiagnostics::Event::MEDIA_FAILED,
                     static_cast<int16_t>(MilestoneDiagnostics::MediaFailure::PLAYBACK),
                     static_cast<int32_t>(mediaCatalog.entries[index].id & 0x7FFFFFFFUL), false);
  }
  closeMediaPlayback();
  mediaPlaybackFailed = true;
  return false;
}

uint8_t currentMediaDisplaySeconds() {
  if (mediaPlaybackCatalogIndex >= mediaCatalog.count) return config.cycleIntervalSec == 0 ? 8 : config.cycleIntervalSec;
  return mediaCatalog.entries[mediaPlaybackCatalogIndex].displaySeconds;
}

bool advanceMediaFrameIfDue(uint32_t now) {
  if (!mediaPlaybackFile || mediaPlaybackFinished || mediaPlaybackHeader.frameCount <= 1 ||
      !deadlineReached(now, mediaPlaybackNextFrameMs)) return false;
  if (mediaPlaybackFrameIndex >= mediaPlaybackHeader.frameCount) {
    const bool loop = (mediaPlaybackHeader.flags & MilestoneMedia::FLAG_LOOP) != 0;
    if (!loop) {
      mediaPlaybackFinished = true;
      return false;
    }
    if (!mediaPlaybackFile.seek(MilestoneMedia::HEADER_BYTES)) {
      mediaPlaybackFailed = true;
      return false;
    }
    mediaPlaybackFrameIndex = 0;
    memset(mediaFrameBuffer, 0, MilestoneMedia::FRAME_BYTES);
  }
  uint16_t delayMs = 0;
  if (!readMediaFrameRecord(mediaPlaybackFrameIndex == 0, delayMs)) {
    mediaPlaybackFailed = true;
    return false;
  }
  ++mediaPlaybackFrameIndex;
  mediaPlaybackFrameReady = true;
  const uint16_t minimumDelay = thermalThrottled ? 250U : 100U;
  mediaPlaybackNextFrameMs = now + (delayMs < minimumDelay ? minimumDelay : delayMs);
  return true;
}

bool renderMediaFrameIfNeeded(uint32_t now) {
  if (mediaPlaybackCatalogIndex == 0xFF && !selectNextMediaItem()) return false;
  advanceMediaFrameIfDue(now);
  if (mediaPlaybackFailed || !mediaPlaybackFrameReady || !mediaFrameBuffer) return false;
  memcpy(display.getBufferPtr(), mediaFrameBuffer, MilestoneMedia::FRAME_BYTES);
  display.sendBuffer();
  mediaPlaybackFrameReady = false;
  return true;
}

bool copyCurrentMediaFrameToDisplayBuffer() {
  if (!mediaFrameBuffer || mediaPlaybackCatalogIndex == 0xFF || mediaPlaybackFailed) return false;
  memcpy(display.getBufferPtr(), mediaFrameBuffer, MilestoneMedia::FRAME_BYTES);
  return true;
}

void drawMediaUnavailable() {
  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  drawCenteredStr("NO MEDIA", 54, 0);
  display.setFont(u8g2_font_6x10_tf);
  drawCenteredStr(mediaStorageReady ? "OPEN SETUP TO ADD" : "STORAGE UNAVAILABLE", 82, 0);
  display.sendBuffer();
}

bool mediaDisplayOwnsFrame() {
  return currentView == View::CUSTOM_MEDIA && !thermalSafeMode && !bootSplashActive() &&
         !firmwareTransferActive() && updateState != UpdateState::ERROR_STATE &&
         !(updateState == UpdateState::AVAILABLE && updatePromptVisible) && !portalActive &&
         !resetConfirmation && !buttonStablePressed;
}

bool clearAllMediaFiles() {
  closeMediaPlayback();
  if (!mediaStorageReady) return true;
  bool success = true;
  for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
    const String path = mediaPath(mediaCatalog.entries[i].id);
    if (LittleFS.exists(path) && !LittleFS.remove(path)) success = false;
  }
  if (LittleFS.exists(MEDIA_UPLOAD_TEMP) && !LittleFS.remove(MEDIA_UPLOAD_TEMP)) success = false;
  if (LittleFS.exists(MEDIA_INDEX_A) && !LittleFS.remove(MEDIA_INDEX_A)) success = false;
  if (LittleFS.exists(MEDIA_INDEX_B) && !LittleFS.remove(MEDIA_INDEX_B)) success = false;
  mediaCatalog = MediaCatalog();
  mediaIndexActiveSlot = 0;
  mediaRotationCursor = 0;
  return success;
}

bool repairMediaStorage() {
  closeMediaPlayback();
  if (mediaUploadFile) mediaUploadFile.close();
  mediaUploadActive = false;
  LittleFS.end();
  mediaStorageReady = false;
  if (!LittleFS.format() || !LittleFS.begin(false)) return false;
  if (!LittleFS.exists(MEDIA_DIRECTORY) && !LittleFS.mkdir(MEDIA_DIRECTORY)) {
    LittleFS.end();
    return false;
  }
  mediaStorageReady = true;
  mediaCatalog = MediaCatalog();
  mediaCatalogScratch = MediaCatalog();
  mediaIndexActiveSlot = 0;
  mediaRotationCursor = 0;
  if (prefsReady) prefs.putBool("media_fs", true);
  return true;
}

bool initializeMediaStorage() {
  mediaStorageReady = LittleFS.begin(false);
  const bool initializedBefore = prefsReady && prefs.getBool("media_fs", false);
  if (!mediaStorageReady && !initializedBefore) {
    logLine("initializing custom media filesystem");
    if (LittleFS.format()) mediaStorageReady = LittleFS.begin(false);
  }
  if (!mediaStorageReady) {
    logLine("custom media filesystem unavailable; core functions continue");
    recordDiagnostic(MilestoneDiagnostics::Event::MEDIA_FAILED,
                     static_cast<int16_t>(MilestoneDiagnostics::MediaFailure::STORAGE), 0, false);
    return false;
  }
  if (!LittleFS.exists(MEDIA_DIRECTORY) && !LittleFS.mkdir(MEDIA_DIRECTORY)) {
    mediaStorageReady = false;
    logLine("custom media directory creation failed");
    recordDiagnostic(MilestoneDiagnostics::Event::MEDIA_FAILED,
                     static_cast<int16_t>(MilestoneDiagnostics::MediaFailure::STORAGE), 0, false);
    return false;
  }
  if (prefsReady && !initializedBefore &&
      (prefs.putBool("media_fs", true) != sizeof(bool) || !prefs.getBool("media_fs", false))) {
    logLine("custom media initialization marker save failed");
  }
  const bool validA = readMediaIndex(MEDIA_INDEX_A, mediaCatalog);
  const bool validB = readMediaIndex(MEDIA_INDEX_B, mediaCatalogScratch);
  if (validA && validB) {
    if (mediaGenerationNewer(mediaCatalogScratch.generation, mediaCatalog.generation)) {
      mediaCatalog = mediaCatalogScratch;
      mediaIndexActiveSlot = 'b';
    } else {
      mediaIndexActiveSlot = 'a';
    }
  } else if (validA) {
    mediaIndexActiveSlot = 'a';
  } else if (validB) {
    mediaCatalog = mediaCatalogScratch;
    mediaIndexActiveSlot = 'b';
  } else {
    mediaCatalog = MediaCatalog();
    mediaIndexActiveSlot = 0;
  }
  mediaCatalogScratch = mediaCatalog;
  mediaCatalogScratch.count = 0;
  for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
    const MediaEntry &entry = mediaCatalog.entries[i];
    MilestoneMedia::Header header;
    MilestoneMedia::Error error;
    File stored = LittleFS.open(mediaPath(entry.id), "r");
    const bool sizeMatches = stored && stored.size() == entry.fileSize;
    if (stored) stored.close();
    const bool valid = sizeMatches && validateMediaFile(mediaPath(entry.id), header, error) &&
                       header.frameCount == entry.frameCount && header.durationMs == entry.durationMs;
    if (valid) {
      mediaCatalogScratch.entries[mediaCatalogScratch.count++] = entry;
    } else {
      logLine(String("custom media quarantined: id=") + String(entry.id) +
              ", validation=" + (sizeMatches ? MilestoneMedia::errorName(error) : "size"));
      recordDiagnostic(MilestoneDiagnostics::Event::MEDIA_FAILED,
                       static_cast<int16_t>(MilestoneDiagnostics::MediaFailure::VALIDATION),
                       static_cast<int32_t>(entry.id & 0x7FFFFFFFUL), false);
    }
  }
  if (mediaCatalogScratch.count != mediaCatalog.count && !commitMediaCatalog(mediaCatalogScratch)) {
    logLine("custom media quarantine index save failed");
  }
  if (LittleFS.exists(MEDIA_UPLOAD_TEMP)) LittleFS.remove(MEDIA_UPLOAD_TEMP);
  allocateMediaBuffers();
  logLine(String("custom media ready: items=") + String(mediaCatalog.count) +
          ", total=" + String(LittleFS.totalBytes()) + ", used=" + String(LittleFS.usedBytes()));
  return true;
}

uint32_t nextMediaId() {
  for (uint8_t attempt = 0; attempt < 32; ++attempt) {
    uint32_t id = esp_random();
    if (id == 0) continue;
    bool duplicate = false;
    for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
      if (mediaCatalog.entries[i].id == id) duplicate = true;
    }
    if (!duplicate && !LittleFS.exists(mediaPath(id))) return id;
  }
  uint32_t id = mediaCatalog.generation + 1U;
  while (id == 0 || findMediaEntry(id) >= 0 || LittleFS.exists(mediaPath(id))) ++id;
  return id;
}

bool addUploadedMedia(const String &name, uint8_t displaySeconds,
                      MilestoneMedia::Header &header, String &errorText) {
  if (!mediaStorageReady || mediaCatalog.count >= MEDIA_MAX_ITEMS) {
    errorText = "미디어 저장 목록이 가득 찼습니다.";
    return false;
  }
  MilestoneMedia::Error validationError;
  if (!validateMediaFile(MEDIA_UPLOAD_TEMP, header, validationError)) {
    errorText = String("변환 파일 검증 실패: ") + MilestoneMedia::errorName(validationError);
    return false;
  }
  if (header.frameCount > 1 && !psramFound()) {
    errorText = "PSRAM을 사용할 수 없어 애니메이션을 저장할 수 없습니다.";
    return false;
  }
  File uploaded = LittleFS.open(MEDIA_UPLOAD_TEMP, "r");
  const size_t actualSize = uploaded ? uploaded.size() : 0;
  if (uploaded) uploaded.close();
  if (mediaCatalogBytesUsed(mediaCatalog) + actualSize > mediaStorageLimit()) {
    errorText = "미디어 저장 한도를 초과합니다.";
    return false;
  }
  const uint32_t id = nextMediaId();
  const String finalPath = mediaPath(id);
  if (LittleFS.exists(finalPath)) LittleFS.remove(finalPath);
  if (!LittleFS.rename(MEDIA_UPLOAD_TEMP, finalPath)) {
    errorText = "검증된 미디어 파일을 확정하지 못했습니다.";
    return false;
  }
  mediaCatalogScratch = mediaCatalog;
  MediaEntry &entry = mediaCatalogScratch.entries[mediaCatalogScratch.count++];
  entry.id = id;
  entry.fileSize = actualSize;
  entry.durationMs = header.durationMs;
  entry.frameCount = header.frameCount;
  entry.displaySeconds = displaySeconds;
  entry.flags = MEDIA_ENTRY_ENABLED;
  if ((header.flags & MilestoneMedia::FLAG_ANIMATED) != 0) entry.flags |= MEDIA_ENTRY_ANIMATED;
  if ((header.flags & MilestoneMedia::FLAG_LOOP) != 0) entry.flags |= MEDIA_ENTRY_LOOP;
  memset(entry.name, 0, sizeof(entry.name));
  memcpy(entry.name, name.c_str(), min(name.length(), sizeof(entry.name) - 1U));
  if (!commitMediaCatalog(mediaCatalogScratch)) {
    LittleFS.remove(finalPath);
    errorText = "미디어 목록을 안전하게 저장하지 못했습니다.";
    return false;
  }
  return true;
}

bool updateMediaEntry(uint32_t id, const String &name, uint8_t displaySeconds, bool enabled) {
  const int index = findMediaEntry(id);
  if (index < 0 || name.length() == 0 || name.length() >= MEDIA_NAME_BYTES ||
      displaySeconds < 3 || displaySeconds > 60) return false;
  mediaCatalogScratch = mediaCatalog;
  MediaEntry &entry = mediaCatalogScratch.entries[index];
  memset(entry.name, 0, sizeof(entry.name));
  memcpy(entry.name, name.c_str(), name.length());
  entry.displaySeconds = displaySeconds;
  if (enabled) entry.flags |= MEDIA_ENTRY_ENABLED;
  else entry.flags &= ~MEDIA_ENTRY_ENABLED;
  return commitMediaCatalog(mediaCatalogScratch);
}

bool moveMediaEntry(uint32_t id, int direction) {
  const int index = findMediaEntry(id);
  if (index < 0) return false;
  const int target = index + direction;
  if (target < 0 || target >= mediaCatalog.count) return true;
  mediaCatalogScratch = mediaCatalog;
  const MediaEntry temporary = mediaCatalogScratch.entries[index];
  mediaCatalogScratch.entries[index] = mediaCatalogScratch.entries[target];
  mediaCatalogScratch.entries[target] = temporary;
  if (!commitMediaCatalog(mediaCatalogScratch)) return false;
  closeMediaPlayback();
  return true;
}

bool deleteMediaEntry(uint32_t id) {
  const int index = findMediaEntry(id);
  if (index < 0) return false;
  mediaCatalogScratch = mediaCatalog;
  for (uint8_t i = static_cast<uint8_t>(index); i + 1U < mediaCatalogScratch.count; ++i) {
    mediaCatalogScratch.entries[i] = mediaCatalogScratch.entries[i + 1U];
  }
  --mediaCatalogScratch.count;
  mediaCatalogScratch.entries[mediaCatalogScratch.count] = MediaEntry();
  if (!commitMediaCatalog(mediaCatalogScratch)) return false;
  if (mediaPlaybackCatalogIndex == static_cast<uint8_t>(index)) closeMediaPlayback();
  const String path = mediaPath(id);
  return !LittleFS.exists(path) || LittleFS.remove(path);
}

bool mediaTransferBusy() {
  return mediaUploadActive;
}
