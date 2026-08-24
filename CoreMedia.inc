// MILESTONE Core custom media storage/playback and isolated v1.10.5 STREAM_MODE transport.
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
constexpr uint32_t MEDIA_PLAYBACK_RECOVERY_MS = 5000UL;
constexpr char MEDIA_DIRECTORY[] = "/media";
constexpr char MEDIA_INDEX_A[] = "/media/index.a";
constexpr char MEDIA_INDEX_B[] = "/media/index.b";
constexpr char MEDIA_UPLOAD_TEMP[] = "/media/upload.tmp";

// Live streaming is an isolated runtime mode. The browser pre-converts the
// whole source; the ESP32 only receives paced 128x128 one-bit frames into a
// PSRAM ring and flushes them to the SH1107. No live frame is written to flash.
constexpr uint8_t MEDIA_STREAM_QUEUE_FRAMES = 240;
// Arduino WebServer closes every HTTP response, so larger bounded batches
// amortize TCP setup/teardown on iPhone captive-AP links. Twenty-four raw
// frames remain below 50 KiB and the request scratch buffer lives in PSRAM.
constexpr uint8_t MEDIA_STREAM_MAX_PUSH_FRAMES = 24;
constexpr uint8_t MEDIA_STREAM_PREBUFFER_FRAMES = 96;
constexpr uint8_t MEDIA_STREAM_REBUFFER_FRAMES = 48;
constexpr size_t MEDIA_STREAM_PACKET_MAX_BYTES =
    static_cast<size_t>(MEDIA_STREAM_MAX_PUSH_FRAMES) *
    (MilestoneMedia::FRAME_HEADER_BYTES + MilestoneMedia::MAX_ENCODED_FRAME_BYTES);
// Full-screen SH1107 I2C flushes consume most of a 20 fps frame period.
// Reserve an explicit service window after each OLED transaction so the HTTP
// server can drain incoming frame batches instead of being starved by display
// work. The media timeline is still preserved by dropping stale source frames.
constexpr uint16_t MEDIA_STREAM_NETWORK_SLICE_MS = 12;
constexpr uint16_t MEDIA_STREAM_MAX_RENDER_INTERVAL_MS = 250;
constexpr uint16_t MEDIA_STREAM_MIN_FPS = 5;
constexpr uint16_t MEDIA_STREAM_MAX_FPS = 24;
constexpr uint32_t MEDIA_STREAM_STALE_MS = 15000UL;
constexpr uint32_t MEDIA_STREAM_RESOURCE_CHECK_MS = 2000UL;
constexpr uint32_t MEDIA_STREAM_MIN_FREE_HEAP = 52U * 1024U;
constexpr uint32_t MEDIA_STREAM_MIN_LARGEST_HEAP_BLOCK = 20U * 1024U;
constexpr uint32_t MEDIA_STREAM_MIN_STACK_FREE = 3072U;
constexpr float MEDIA_STREAM_WARNING_C = 75.0f;
constexpr float MEDIA_STREAM_STOP_C = 80.0f;
constexpr float MEDIA_STREAM_EMERGENCY_C = 85.0f;

enum class MediaStreamPhase : uint8_t {
  INACTIVE = 0,
  BUFFERING = 1,
  PLAYING = 2
};

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
bool mediaUpdateIndicatorDrawn = false;
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
bool mediaPlaybackRotationPending = false;
bool mediaPlaybackLoopBoundary = false;
uint32_t mediaPlaybackRecoveryMs = 0;

File mediaUploadFile;
bool mediaUploadActive = false;
bool mediaUploadComplete = false;
bool mediaUploadFailed = false;
size_t mediaUploadExpectedSize = 0;
size_t mediaUploadWritten = 0;
uint8_t mediaUploadDisplaySeconds = 8;
String mediaUploadName;
String mediaUploadError;

uint8_t *mediaStreamQueue = nullptr;
uint8_t *mediaStreamRawPacket = nullptr;
uint8_t *mediaStreamDecodeFrame = nullptr;
bool mediaStreamActive = false;
bool mediaStreamResumeNtpAfterExit = false;
MediaStreamPhase mediaStreamPhase = MediaStreamPhase::INACTIVE;
bool mediaStreamRawActive = false;
bool mediaStreamRawComplete = false;
bool mediaStreamRawFailed = false;
bool mediaStreamRawDuplicate = false;
const char *mediaStreamRawError = "idle";
uint32_t mediaStreamSessionId = 0;
uint32_t mediaStreamLastPushMs = 0;
uint32_t mediaStreamNextFrameMs = 0;
uint32_t mediaStreamNextRenderMs = 0;
uint32_t mediaStreamLastPushSeq = 0;
uint8_t mediaStreamLastPushFrameCount = 0;
size_t mediaStreamLastPushEncodedBytes = 0;
uint32_t mediaStreamEncodedBytes = 0;
uint32_t mediaStreamReceivedFrames = 0;
uint32_t mediaStreamDisplayedFrames = 0;
uint32_t mediaStreamDroppedFrames = 0;
uint32_t mediaStreamUnderruns = 0;
uint16_t mediaStreamFps = 20;
uint16_t mediaStreamFrameIntervalMs = 50;
uint8_t mediaStreamQueueHead = 0;
uint8_t mediaStreamQueueTail = 0;
uint8_t mediaStreamQueueCount = 0;
uint8_t mediaStreamRawFrameCount = 0;
uint8_t mediaStreamRawBaseTail = 0;
size_t mediaStreamRawExpectedBytes = 0;
size_t mediaStreamRawWrittenBytes = 0;
uint32_t mediaStreamRawSeq = 0;
uint32_t mediaStreamLastResourceCheckMs = 0;
uint32_t mediaStreamMinFreeHeap = 0xFFFFFFFFUL;
uint32_t mediaStreamMinLargestHeapBlock = 0xFFFFFFFFUL;
uint32_t mediaStreamMinStackFree = 0xFFFFFFFFUL;
uint32_t mediaStreamLastFlushUs = 0;
uint32_t mediaStreamFlushEmaUs = 0;
uint16_t mediaStreamRenderIntervalMs = 0;
uint16_t mediaStreamLastDirtyTiles = 0;
uint32_t mediaStreamFpsWindowStartedMs = 0;
uint16_t mediaStreamFpsWindowFrames = 0;
uint16_t mediaStreamActualFpsX10 = 0;
bool mediaStreamThermalWarning = false;
bool mediaStreamSourceEnded = false;
const char *mediaStreamLastStopReason = "idle";

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

size_t mediaEnabledItemCount() {
  if (!mediaStorageReady) return 0;
  size_t count = 0;
  for (uint8_t i = 0; i < mediaCatalog.count; ++i) {
    if ((mediaCatalog.entries[i].flags & MEDIA_ENTRY_ENABLED) != 0) ++count;
  }
  return count;
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
  mediaPlaybackRotationPending = false;
  mediaPlaybackLoopBoundary = false;
  mediaPlaybackRecoveryMs = 0;
}

bool readMediaFrameRecord(bool firstFrame, uint16_t &delayMs,
                          MilestoneMedia::Error *decodeError = nullptr) {
  if (decodeError) *decodeError = MilestoneMedia::Error::NONE;
  if (!mediaPlaybackFile || !mediaFrameBuffer || !mediaFrameRecordBuffer) {
    if (decodeError) *decodeError = MilestoneMedia::Error::NULL_ARGUMENT;
    return false;
  }
  if (mediaPlaybackFile.available() < static_cast<int>(MilestoneMedia::FRAME_HEADER_BYTES)) {
    if (decodeError) *decodeError = MilestoneMedia::Error::FRAME_HEADER;
    return false;
  }
  if (mediaPlaybackFile.read(mediaFrameRecordBuffer, MilestoneMedia::FRAME_HEADER_BYTES) !=
      MilestoneMedia::FRAME_HEADER_BYTES) {
    if (decodeError) *decodeError = MilestoneMedia::Error::FRAME_HEADER;
    return false;
  }
  const uint16_t dataSize = MilestoneMedia::readLe16(mediaFrameRecordBuffer + 3);
  if (dataSize > MilestoneMedia::FRAME_BYTES + 32U ||
      mediaPlaybackFile.available() < static_cast<int>(dataSize)) {
    if (decodeError) *decodeError = MilestoneMedia::Error::FRAME_SIZE;
    return false;
  }
  if (mediaPlaybackFile.read(mediaFrameRecordBuffer + MilestoneMedia::FRAME_HEADER_BYTES, dataSize) != dataSize) {
    if (decodeError) *decodeError = MilestoneMedia::Error::FRAME_SIZE;
    return false;
  }
  size_t consumed = 0;
  MilestoneMedia::Error error = MilestoneMedia::Error::NONE;
  const bool decoded = MilestoneMedia::decodeFrame(mediaFrameRecordBuffer,
      MilestoneMedia::FRAME_HEADER_BYTES + dataSize, firstFrame, mediaFrameBuffer,
      delayMs, consumed, &error) && consumed == MilestoneMedia::FRAME_HEADER_BYTES + dataSize;
  if (!decoded && decodeError) {
    *decodeError = error == MilestoneMedia::Error::NONE
                     ? MilestoneMedia::Error::PAYLOAD_SIZE : error;
  }
  return decoded;
}

void markMediaPlaybackFailure(MilestoneMedia::Error error) {
  if (mediaPlaybackFailed) return;
  mediaPlaybackFailed = true;
  mediaPlaybackRecoveryMs = millis() + MEDIA_PLAYBACK_RECOVERY_MS;
  const uint32_t id = mediaPlaybackCatalogIndex < mediaCatalog.count
                        ? mediaCatalog.entries[mediaPlaybackCatalogIndex].id : 0;
  logLine(String("stored media playback failed: id=") + String(id) +
          " frame=" + String(mediaPlaybackFrameIndex) +
          " error=" + MilestoneMedia::errorName(error));
  recordDiagnostic(MilestoneDiagnostics::Event::MEDIA_FAILED,
                   static_cast<int16_t>(MilestoneDiagnostics::MediaFailure::PLAYBACK),
                   static_cast<int32_t>(id & 0x7FFFFFFFUL), false);
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
  mediaPlaybackRecoveryMs = millis() + MEDIA_PLAYBACK_RECOVERY_MS;
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
    if (mediaPlaybackRotationPending && mediaEnabledItemCount() > 1U) {
      mediaPlaybackLoopBoundary = true;
      return false;
    }
    if (!mediaPlaybackFile.seek(MilestoneMedia::HEADER_BYTES)) {
      markMediaPlaybackFailure(MilestoneMedia::Error::FRAME_HEADER);
      return false;
    }
    mediaPlaybackFrameIndex = 0;
    memset(mediaFrameBuffer, 0, MilestoneMedia::FRAME_BYTES);
  }
  uint16_t delayMs = 0;
  MilestoneMedia::Error decodeError = MilestoneMedia::Error::NONE;
  if (!readMediaFrameRecord(mediaPlaybackFrameIndex == 0, delayMs, &decodeError)) {
    markMediaPlaybackFailure(decodeError);
    return false;
  }
  ++mediaPlaybackFrameIndex;
  mediaPlaybackFrameReady = true;
  const uint16_t minimumDelay = thermalThrottled ? 250U : 100U;
  mediaPlaybackNextFrameMs = now + (delayMs < minimumDelay ? minimumDelay : delayMs);
  return true;
}

bool renderMediaFrameIfNeeded(uint32_t now) {
  if (updateState == UpdateState::CHECKING && !updateCheckIndicatorRendered) {
    display.setDrawColor(0);
    display.drawBox(114, 0, 14, 11);
    display.setDrawColor(1);
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(118, 9, "U");
    display.sendBuffer();
    mediaUpdateIndicatorDrawn = true;
    updateCheckIndicatorRendered = true;
    return true;
  }
  if (mediaPlaybackCatalogIndex == 0xFF && !selectNextMediaItem()) return false;
  if (mediaUpdateIndicatorDrawn && updateState != UpdateState::CHECKING) {
    mediaUpdateIndicatorDrawn = false;
    if (mediaFrameBuffer && mediaPlaybackCatalogIndex != 0xFF && !mediaPlaybackFailed) {
      memcpy(display.getBufferPtr(), mediaFrameBuffer, MilestoneMedia::FRAME_BYTES);
      display.sendBuffer();
      return true;
    }
  }
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

bool deleteMediaEntry(uint32_t id, String &detail) {
  const int index = findMediaEntry(id);
  if (index < 0) {
    detail = "catalog:not-found";
    return false;
  }

  // Release playback before touching the file or its catalog entry.
  closeMediaPlayback();

  MediaCatalog candidate = mediaCatalog;
  for (uint8_t i = static_cast<uint8_t>(index); i + 1U < candidate.count; ++i) {
    candidate.entries[i] = candidate.entries[i + 1U];
  }
  --candidate.count;
  candidate.entries[candidate.count] = MediaEntry();

  const String path = mediaPath(id);
  const bool fileExisted = LittleFS.exists(path);

  // Normal path: persist the smaller catalog first. This preserves the old
  // catalog if the index write itself fails.
  if (commitMediaCatalog(candidate)) {
    if (!fileExisted) {
      detail = "catalog-committed:file-already-missing";
      return true;
    }
    if (LittleFS.remove(path)) {
      detail = "catalog-committed:file-removed";
      return true;
    }

    // The item is already deleted from the authoritative catalog. Do not roll
    // it back merely because best-effort orphan cleanup failed; that made the
    // UI appear undeletable. A later repair/reboot can reclaim the orphan.
    detail = "catalog-committed:file-cleanup-pending";
    logLine(String("custom media delete cleanup pending: id=") + String(id));
    return true;
  }

  // Recovery path for a tight/fragmented LittleFS: deleting the large media
  // file may be the only way to make room for the next small catalog index.
  if (!fileExisted || !LittleFS.remove(path)) {
    detail = fileExisted ? "catalog-write-failed:file-remove-failed"
                         : "catalog-write-failed:file-missing";
    return false;
  }

  if (commitMediaCatalog(candidate)) {
    detail = "file-freed:catalog-retry-committed";
    return true;
  }

  // The file is gone but the catalog could not be persisted even after space
  // was freed. Hide the stale entry for this boot; boot-time validation will
  // quarantine the missing file on the next start and retry catalog repair.
  mediaCatalog = candidate;
  mediaCatalogScratch = candidate;
  detail = "file-removed:catalog-persist-pending";
  logLine(String("custom media delete catalog persist pending: id=") + String(id));
  return true;
}

bool allocateMediaStreamBuffer() {
  if (mediaStreamQueue) return true;
  if (!psramFound()) return false;
  const size_t queueBytes =
      static_cast<size_t>(MEDIA_STREAM_QUEUE_FRAMES) * MilestoneMedia::FRAME_BYTES;
  const size_t allocationBytes =
      queueBytes + MEDIA_STREAM_PACKET_MAX_BYTES + MilestoneMedia::FRAME_BYTES;
  mediaStreamQueue = static_cast<uint8_t *>(heap_caps_malloc(
      allocationBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!mediaStreamQueue) return false;
  mediaStreamRawPacket = mediaStreamQueue + queueBytes;
  mediaStreamDecodeFrame = mediaStreamRawPacket + MEDIA_STREAM_PACKET_MAX_BYTES;
  memset(mediaStreamDecodeFrame, 0, MilestoneMedia::FRAME_BYTES);
  return true;
}

void releaseMediaPlaybackBuffersForStream() {
  closeMediaPlayback();
  if (mediaFrameBuffer) {
    free(mediaFrameBuffer);
    mediaFrameBuffer = nullptr;
  }
  if (mediaFrameRecordBuffer) {
    free(mediaFrameRecordBuffer);
    mediaFrameRecordBuffer = nullptr;
  }
}

void releaseMediaStreamBuffer() {
  if (mediaStreamQueue) free(mediaStreamQueue);
  mediaStreamQueue = nullptr;
  mediaStreamRawPacket = nullptr;
  mediaStreamDecodeFrame = nullptr;
}

void resetMediaStreamRawState() {
  mediaStreamRawActive = false;
  mediaStreamRawComplete = false;
  mediaStreamRawFailed = false;
  mediaStreamRawDuplicate = false;
  mediaStreamRawError = "idle";
  mediaStreamRawFrameCount = 0;
  mediaStreamRawBaseTail = 0;
  mediaStreamRawExpectedBytes = 0;
  mediaStreamRawWrittenBytes = 0;
  mediaStreamRawSeq = 0;
}

void stopMediaStream() {
  const bool wasActive = mediaStreamActive;
  if (wasActive) {
    Serial.printf("[MILESTONE] STREAM_MODE stop: reason=%s, queue=%u, received=%lu, displayed=%lu, dropped=%lu, underruns=%u\n",
                  mediaStreamLastStopReason ? mediaStreamLastStopReason : "unknown",
                  static_cast<unsigned>(mediaStreamQueueCount),
                  static_cast<unsigned long>(mediaStreamReceivedFrames),
                  static_cast<unsigned long>(mediaStreamDisplayedFrames),
                  static_cast<unsigned long>(mediaStreamDroppedFrames),
                  static_cast<unsigned>(mediaStreamUnderruns));
  }
  mediaStreamActive = false;
  mediaStreamPhase = MediaStreamPhase::INACTIVE;
  mediaStreamThermalWarning = false;
  mediaStreamSourceEnded = false;
  resetMediaStreamRawState();
  mediaStreamSessionId = 0;
  mediaStreamLastPushMs = 0;
  mediaStreamNextFrameMs = 0;
  mediaStreamNextRenderMs = 0;
  mediaStreamLastPushSeq = 0;
  mediaStreamLastPushFrameCount = 0;
  mediaStreamLastPushEncodedBytes = 0;
  mediaStreamEncodedBytes = 0;
  mediaStreamQueueHead = 0;
  mediaStreamQueueTail = 0;
  mediaStreamQueueCount = 0;
  releaseMediaStreamBuffer();
  if (wasActive) {
    if (prefsReady) prefs.putBool("stream_open", false);
    leaveMediaStreamPerformanceMode();
  }
}

bool startMediaStream(uint16_t fps) {
  if (fps < MEDIA_STREAM_MIN_FPS || fps > MEDIA_STREAM_MAX_FPS) return false;
  if (mediaStreamActive) stopMediaStream();
  releaseMediaPlaybackBuffersForStream();
  if (!allocateMediaStreamBuffer()) {
    allocateMediaBuffers();
    return false;
  }
  mediaStreamFps = fps;
  mediaStreamFrameIntervalMs = static_cast<uint16_t>((1000U + fps / 2U) / fps);
  mediaStreamSessionId = esp_random();
  if (mediaStreamSessionId == 0) mediaStreamSessionId = 1;
  mediaStreamLastPushMs = millis();
  mediaStreamNextFrameMs = 0;
  mediaStreamNextRenderMs = 0;
  mediaStreamLastPushSeq = 0;
  mediaStreamLastPushFrameCount = 0;
  mediaStreamLastPushEncodedBytes = 0;
  mediaStreamEncodedBytes = 0;
  mediaStreamReceivedFrames = 0;
  mediaStreamDisplayedFrames = 0;
  mediaStreamDroppedFrames = 0;
  mediaStreamUnderruns = 0;
  mediaStreamQueueHead = 0;
  mediaStreamQueueTail = 0;
  mediaStreamQueueCount = 0;
  mediaStreamLastResourceCheckMs = mediaStreamLastPushMs;
  mediaStreamMinFreeHeap = ESP.getFreeHeap();
  mediaStreamMinLargestHeapBlock = ESP.getMaxAllocHeap();
  mediaStreamMinStackFree = uxTaskGetStackHighWaterMark(nullptr);
  mediaStreamLastFlushUs = 0;
  mediaStreamFlushEmaUs = 0;
  mediaStreamRenderIntervalMs = mediaStreamFrameIntervalMs;
  mediaStreamLastDirtyTiles = 0;
  mediaStreamFpsWindowStartedMs = mediaStreamLastPushMs;
  mediaStreamFpsWindowFrames = 0;
  mediaStreamActualFpsX10 = 0;
  mediaStreamThermalWarning = false;
  mediaStreamSourceEnded = false;
  mediaStreamLastStopReason = "active";
  resetMediaStreamRawState();
  mediaStreamPhase = MediaStreamPhase::BUFFERING;
  mediaStreamActive = true;
  enterMediaStreamPerformanceMode();
  if (prefsReady) prefs.putBool("stream_open", true);
  logLine(String("STREAM_MODE start: fps=") + String(mediaStreamFps) +
          ", heap=" + String(ESP.getFreeHeap()) +
          ", largest=" + String(ESP.getMaxAllocHeap()) +
          ", psram=" + String(ESP.getFreePsram()));
  return true;
}

uint8_t mediaStreamQueueFree() {
  return static_cast<uint8_t>(MEDIA_STREAM_QUEUE_FRAMES - mediaStreamQueueCount);
}

bool beginMediaStreamRawWrite(uint32_t session, uint32_t seq, uint8_t frameCount,
                              size_t encodedBytes) {
  resetMediaStreamRawState();
  if (!mediaStreamActive || !mediaStreamQueue || !mediaStreamRawPacket ||
      !mediaStreamDecodeFrame || session != mediaStreamSessionId) {
    mediaStreamRawError = "invalid-session";
    mediaStreamRawFailed = true;
    return false;
  }
  if (mediaStreamSourceEnded) {
    mediaStreamRawError = "source-ended";
    mediaStreamRawFailed = true;
    return false;
  }
  const size_t minimumBytes =
      static_cast<size_t>(frameCount) * MilestoneMedia::FRAME_HEADER_BYTES;
  if (seq == 0 || seq < mediaStreamLastPushSeq || frameCount == 0 ||
      frameCount > MEDIA_STREAM_MAX_PUSH_FRAMES || encodedBytes < minimumBytes ||
      encodedBytes > MEDIA_STREAM_PACKET_MAX_BYTES) {
    mediaStreamRawError = "invalid-sequence";
    mediaStreamRawFailed = true;
    return false;
  }
  // A lost HTTP response can make the browser retry a batch that the device
  // already committed. Consume and validate that same body length, but never
  // decode or enqueue it a second time. Sequence reuse with different metadata
  // is rejected so a stale retry cannot silently advance the decoder state.
  if (seq == mediaStreamLastPushSeq && mediaStreamLastPushSeq != 0) {
    if (frameCount != mediaStreamLastPushFrameCount ||
        encodedBytes != mediaStreamLastPushEncodedBytes) {
      mediaStreamRawError = "duplicate-mismatch";
      mediaStreamRawFailed = true;
      return false;
    }
    mediaStreamRawFrameCount = frameCount;
    mediaStreamRawExpectedBytes = encodedBytes;
    mediaStreamRawWrittenBytes = 0;
    mediaStreamRawSeq = seq;
    mediaStreamRawDuplicate = true;
    mediaStreamRawActive = true;
    mediaStreamRawError = "duplicate";
    return true;
  }
  if (frameCount > mediaStreamQueueFree()) {
    mediaStreamRawError = "buffer-full";
    mediaStreamRawFailed = true;
    return false;
  }
  mediaStreamRawFrameCount = frameCount;
  mediaStreamRawExpectedBytes = encodedBytes;
  mediaStreamRawWrittenBytes = 0;
  mediaStreamRawSeq = seq;
  mediaStreamRawBaseTail = mediaStreamQueueTail;
  mediaStreamRawActive = true;
  mediaStreamRawError = "active";
  return true;
}

bool writeMediaStreamRawChunk(const uint8_t *data, size_t length) {
  if (!mediaStreamRawActive || mediaStreamRawFailed || !data) return false;
  if (mediaStreamRawWrittenBytes + length > mediaStreamRawExpectedBytes) {
    mediaStreamRawError = "body-too-large";
    mediaStreamRawFailed = true;
    return false;
  }
  if (!mediaStreamRawDuplicate) {
    memcpy(mediaStreamRawPacket + mediaStreamRawWrittenBytes, data, length);
  }
  mediaStreamRawWrittenBytes += length;
  return true;
}

bool finishMediaStreamRawWrite(size_t totalSize) {
  if (!mediaStreamRawActive || mediaStreamRawFailed ||
      mediaStreamRawWrittenBytes != mediaStreamRawExpectedBytes ||
      totalSize != mediaStreamRawExpectedBytes) {
    if (!mediaStreamRawFailed) mediaStreamRawError = "body-size-mismatch";
    mediaStreamRawFailed = true;
    mediaStreamRawActive = false;
    return false;
  }
  mediaStreamRawComplete = true;
  mediaStreamRawActive = false;
  return true;
}

bool commitMediaStreamRawWrite() {
  if (!mediaStreamRawComplete || mediaStreamRawFailed || mediaStreamRawFrameCount == 0) return false;
  if (mediaStreamRawDuplicate) {
    mediaStreamLastPushMs = millis();
    resetMediaStreamRawState();
    return true;
  }

  size_t offset = 0;
  uint8_t lastDecodedSlot = mediaStreamRawBaseTail;
  for (uint8_t i = 0; i < mediaStreamRawFrameCount; ++i) {
    const uint8_t slot = static_cast<uint8_t>(
        (mediaStreamRawBaseTail + i) % MEDIA_STREAM_QUEUE_FRAMES);
    uint8_t *destination =
        mediaStreamQueue + static_cast<size_t>(slot) * MilestoneMedia::FRAME_BYTES;
    const bool firstFrame = mediaStreamReceivedFrames == 0 && i == 0;
    if (!firstFrame) {
      if (i == 0) {
        memcpy(destination, mediaStreamDecodeFrame, MilestoneMedia::FRAME_BYTES);
      } else {
        const uint8_t previousSlot = static_cast<uint8_t>(
            (mediaStreamRawBaseTail + i - 1U) % MEDIA_STREAM_QUEUE_FRAMES);
        memcpy(destination,
               mediaStreamQueue + static_cast<size_t>(previousSlot) * MilestoneMedia::FRAME_BYTES,
               MilestoneMedia::FRAME_BYTES);
      }
    }
    uint16_t delayMs = 0;
    size_t consumed = 0;
    MilestoneMedia::Error decodeError = MilestoneMedia::Error::NONE;
    if (!MilestoneMedia::decodeFrame(
            mediaStreamRawPacket + offset,
            mediaStreamRawExpectedBytes - offset,
            firstFrame, destination, delayMs, consumed, &decodeError) ||
        delayMs != 0 || consumed == 0) {
      mediaStreamRawError = "encoded-frame-invalid";
      mediaStreamRawFailed = true;
      mediaStreamRawComplete = false;
      return false;
    }
    offset += consumed;
    lastDecodedSlot = slot;
  }
  if (offset != mediaStreamRawExpectedBytes) {
    mediaStreamRawError = "encoded-body-trailing-data";
    mediaStreamRawFailed = true;
    mediaStreamRawComplete = false;
    return false;
  }

  memcpy(mediaStreamDecodeFrame,
         mediaStreamQueue + static_cast<size_t>(lastDecodedSlot) * MilestoneMedia::FRAME_BYTES,
         MilestoneMedia::FRAME_BYTES);
  mediaStreamQueueTail = static_cast<uint8_t>(
      (mediaStreamQueueTail + mediaStreamRawFrameCount) % MEDIA_STREAM_QUEUE_FRAMES);
  mediaStreamQueueCount = static_cast<uint8_t>(mediaStreamQueueCount + mediaStreamRawFrameCount);
  mediaStreamReceivedFrames += mediaStreamRawFrameCount;
  mediaStreamLastPushSeq = mediaStreamRawSeq;
  mediaStreamLastPushFrameCount = mediaStreamRawFrameCount;
  mediaStreamLastPushEncodedBytes = mediaStreamRawExpectedBytes;
  mediaStreamEncodedBytes += static_cast<uint32_t>(mediaStreamRawExpectedBytes);
  mediaStreamLastPushMs = millis();
  const uint8_t resumeThreshold = mediaStreamDisplayedFrames == 0
      ? MEDIA_STREAM_PREBUFFER_FRAMES : MEDIA_STREAM_REBUFFER_FRAMES;
  if (mediaStreamPhase == MediaStreamPhase::BUFFERING &&
      mediaStreamQueueCount >= resumeThreshold) {
    mediaStreamPhase = MediaStreamPhase::PLAYING;
    mediaStreamNextFrameMs = mediaStreamLastPushMs + mediaStreamFrameIntervalMs;
    mediaStreamNextRenderMs = mediaStreamNextFrameMs;
    mediaStreamFpsWindowStartedMs = mediaStreamLastPushMs;
    mediaStreamFpsWindowFrames = 0;
  }
  resetMediaStreamRawState();
  return true;
}

bool finishMediaStreamSource(uint32_t session) {
  if (!mediaStreamActive || session == 0 || session != mediaStreamSessionId) return false;
  mediaStreamSourceEnded = true;
  mediaStreamLastPushMs = millis();
  // A short source, or the tail after an underrun, may contain fewer frames
  // than the normal prebuffer/rebuffer threshold. Once the browser declares
  // EOF there is nothing more to wait for, so drain every queued frame.
  if (mediaStreamPhase == MediaStreamPhase::BUFFERING && mediaStreamQueueCount > 0) {
    mediaStreamPhase = MediaStreamPhase::PLAYING;
    mediaStreamNextFrameMs = mediaStreamLastPushMs + mediaStreamFrameIntervalMs;
    mediaStreamNextRenderMs = mediaStreamNextFrameMs;
    mediaStreamFpsWindowStartedMs = mediaStreamLastPushMs;
    mediaStreamFpsWindowFrames = 0;
  }
  return true;
}

void processMediaStreamState(uint32_t now) {
  if (!mediaStreamActive) return;
  if (!portalActive) {
    mediaStreamLastStopReason = "portal-closed";
    stopMediaStream();
    return;
  }
  if (temperatureSensorFault || thermalSafeMode || firmwareUpdateBusy()) {
    mediaStreamLastStopReason = "safety-state";
    stopMediaStream();
    return;
  }
  if (mediaStreamSourceEnded && !mediaStreamRawActive && mediaStreamQueueCount == 0) {
    mediaStreamLastStopReason = "source-complete";
    stopMediaStream();
    return;
  }
  // A deep jitter buffer can legitimately go several seconds without another
  // POST. Only treat the sender as stale when the device is actually waiting
  // for data or the remaining queue has fallen into the rebuffer range.
  if (!mediaStreamSourceEnded && !mediaStreamRawActive &&
      (mediaStreamPhase == MediaStreamPhase::BUFFERING ||
       mediaStreamQueueCount <= MEDIA_STREAM_REBUFFER_FRAMES) &&
      elapsed(now, mediaStreamLastPushMs, MEDIA_STREAM_STALE_MS)) {
    mediaStreamLastStopReason = "source-stale";
    stopMediaStream();
    return;
  }
  if (!elapsed(now, mediaStreamLastResourceCheckMs, MEDIA_STREAM_RESOURCE_CHECK_MS)) return;
  mediaStreamLastResourceCheckMs = now;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestHeapBlock = ESP.getMaxAllocHeap();
  const uint32_t stackFree = uxTaskGetStackHighWaterMark(nullptr);
  if (freeHeap < mediaStreamMinFreeHeap) mediaStreamMinFreeHeap = freeHeap;
  if (largestHeapBlock < mediaStreamMinLargestHeapBlock) mediaStreamMinLargestHeapBlock = largestHeapBlock;
  if (stackFree < mediaStreamMinStackFree) mediaStreamMinStackFree = stackFree;
  if (freeHeap < MEDIA_STREAM_MIN_FREE_HEAP ||
      largestHeapBlock < MEDIA_STREAM_MIN_LARGEST_HEAP_BLOCK ||
      stackFree < MEDIA_STREAM_MIN_STACK_FREE) {
    mediaStreamLastStopReason = "resource-low";
    // Avoid dynamic String allocation precisely when internal heap is under pressure.
    Serial.printf("[MILESTONE] STREAM_MODE resource stop: heap=%lu, largest=%lu, stack=%lu\n",
                  static_cast<unsigned long>(freeHeap),
                  static_cast<unsigned long>(largestHeapBlock),
                  static_cast<unsigned long>(stackFree));
    stopMediaStream();
  }
}

void updateMediaStreamMeasuredFps(uint32_t now) {
  if (!elapsed(now, mediaStreamFpsWindowStartedMs, 1000UL)) return;
  const uint32_t windowMs = now - mediaStreamFpsWindowStartedMs;
  uint32_t measuredX10 = windowMs > 0
      ? (static_cast<uint32_t>(mediaStreamFpsWindowFrames) * 10000U + windowMs / 2U) / windowMs
      : 0U;
  if (measuredX10 > 65535U) measuredX10 = 65535U;
  mediaStreamActualFpsX10 = static_cast<uint16_t>(measuredX10);
  mediaStreamFpsWindowFrames = 0;
  mediaStreamFpsWindowStartedMs = now;
}
uint16_t mediaStreamSafeRenderIntervalMs() {
  uint32_t intervalMs = mediaStreamFrameIntervalMs;
  const uint32_t flushBudgetUs = mediaStreamLastFlushUs > mediaStreamFlushEmaUs
      ? mediaStreamLastFlushUs : mediaStreamFlushEmaUs;
  if (flushBudgetUs > 0) {
    const uint32_t flushMs = (flushBudgetUs + 999U) / 1000U;
    const uint32_t safeMs = flushMs + MEDIA_STREAM_NETWORK_SLICE_MS;
    if (safeMs > intervalMs) intervalMs = safeMs;
  }
  if (intervalMs > MEDIA_STREAM_MAX_RENDER_INTERVAL_MS) {
    intervalMs = MEDIA_STREAM_MAX_RENDER_INTERVAL_MS;
  }
  return static_cast<uint16_t>(intervalMs);
}

bool flushMediaStreamFrame(const uint8_t *frame) {
  uint8_t *displayBuffer = display.getBufferPtr();
  if (!displayBuffer || !frame) return false;

  uint8_t rowFirstTile[16];
  uint8_t rowLastTile[16];
  memset(rowFirstTile, 0xFF, sizeof(rowFirstTile));
  memset(rowLastTile, 0, sizeof(rowLastTile));
  uint8_t firstTileX = 16;
  uint8_t lastTileX = 0;
  uint8_t firstTileY = 16;
  uint8_t lastTileY = 0;
  uint8_t changedRows = 0;
  uint16_t dirtyTiles = 0;
  uint16_t rowSpanTiles = 0;

  for (uint8_t row = 0; row < 16; ++row) {
    for (uint8_t tileX = 0; tileX < 16; ++tileX) {
      const size_t offset = static_cast<size_t>(row) * 128U +
                            static_cast<size_t>(tileX) * 8U;
      if (memcmp(displayBuffer + offset, frame + offset, 8U) == 0) continue;
      ++dirtyTiles;
      if (rowFirstTile[row] == 0xFF) rowFirstTile[row] = tileX;
      rowLastTile[row] = tileX;
      if (tileX < firstTileX) firstTileX = tileX;
      if (tileX > lastTileX) lastTileX = tileX;
      if (row < firstTileY) firstTileY = row;
      if (row > lastTileY) lastTileY = row;
    }
    if (rowFirstTile[row] != 0xFF) {
      ++changedRows;
      rowSpanTiles += static_cast<uint16_t>(rowLastTile[row] - rowFirstTile[row] + 1U);
    }
  }

  mediaStreamLastDirtyTiles = dirtyTiles;
  if (dirtyTiles == 0) {
    mediaStreamLastFlushUs = 0;
    return true;
  }

  memcpy(displayBuffer, frame, MilestoneMedia::FRAME_BYTES);
  const uint32_t startedUs = micros();
  const uint16_t boundingTiles = static_cast<uint16_t>(lastTileX - firstTileX + 1U) *
                                 static_cast<uint16_t>(lastTileY - firstTileY + 1U);

  // Small/local motion is cheapest as per-row X spans. Moderately localized
  // changes use one bounding rectangle. Highly dithered/full-motion frames
  // still take the controller's optimized full-buffer path.
  if (changedRows <= 8U && rowSpanTiles <= 112U) {
    for (uint16_t row = firstTileY; row <= lastTileY; ++row) {
      if (rowFirstTile[row] == 0xFF) continue;
      display.updateDisplayArea(rowFirstTile[row], static_cast<uint8_t>(row),
                                static_cast<uint8_t>(rowLastTile[row] - rowFirstTile[row] + 1U), 1);
    }
  } else if (boundingTiles <= 176U) {
    display.updateDisplayArea(firstTileX, firstTileY,
                              static_cast<uint8_t>(lastTileX - firstTileX + 1U),
                              static_cast<uint8_t>(lastTileY - firstTileY + 1U));
  } else {
    display.sendBuffer();
  }

  const uint32_t flushUs = micros() - startedUs;
  mediaStreamLastFlushUs = flushUs;
  mediaStreamFlushEmaUs = mediaStreamFlushEmaUs == 0 ? flushUs
      : static_cast<uint32_t>((mediaStreamFlushEmaUs * 7ULL + flushUs) / 8ULL);
  mediaStreamRenderIntervalMs = mediaStreamSafeRenderIntervalMs();
  return true;
}

bool renderMediaStreamFrameIfDue(uint32_t now) {
  if (!mediaStreamActive || mediaStreamPhase != MediaStreamPhase::PLAYING ||
      !oledReady || displaySleeping || !mediaStreamQueue || mediaStreamNextFrameMs == 0 ||
      mediaStreamNextRenderMs == 0 || !deadlineReached(now, mediaStreamNextRenderMs)) return false;

  if (mediaStreamQueueCount == 0) {
    ++mediaStreamUnderruns;
    mediaStreamPhase = MediaStreamPhase::BUFFERING;
    mediaStreamNextFrameMs = 0;
    mediaStreamNextRenderMs = 0;
    return false;
  }

  // mediaStreamNextFrameMs is the source timeline. mediaStreamNextRenderMs is
  // the OLED service clock. If OLED/network work cannot sustain the requested
  // source FPS, consume stale frames here instead of letting latency grow.
  const int32_t behindMs = static_cast<int32_t>(now - mediaStreamNextFrameMs);
  if (behindMs >= static_cast<int32_t>(mediaStreamFrameIntervalMs) && mediaStreamQueueCount > 1) {
    uint32_t stale = static_cast<uint32_t>(behindMs) / mediaStreamFrameIntervalMs;
    if (stale >= mediaStreamQueueCount) stale = mediaStreamQueueCount - 1U;
    mediaStreamQueueHead = static_cast<uint8_t>((mediaStreamQueueHead + stale) % MEDIA_STREAM_QUEUE_FRAMES);
    mediaStreamQueueCount = static_cast<uint8_t>(mediaStreamQueueCount - stale);
    mediaStreamDroppedFrames += stale;
    mediaStreamNextFrameMs += stale * mediaStreamFrameIntervalMs;
  }

  const uint32_t scheduledRenderMs = mediaStreamNextRenderMs;
  const uint8_t *frame = mediaStreamQueue +
      static_cast<size_t>(mediaStreamQueueHead) * MilestoneMedia::FRAME_BYTES;
  flushMediaStreamFrame(frame);
  mediaStreamQueueHead = static_cast<uint8_t>((mediaStreamQueueHead + 1U) % MEDIA_STREAM_QUEUE_FRAMES);
  --mediaStreamQueueCount;
  ++mediaStreamDisplayedFrames;
  ++mediaStreamFpsWindowFrames;
  const uint32_t afterFlush = millis();
  updateMediaStreamMeasuredFps(afterFlush);
  mediaStreamNextFrameMs += mediaStreamFrameIntervalMs;
  mediaStreamRenderIntervalMs = mediaStreamSafeRenderIntervalMs();
  // Advance from the prior deadline instead of the time this loop happened to
  // run. HTTP receive jitter must not accumulate into visible slow playback.
  mediaStreamNextRenderMs = scheduledRenderMs + mediaStreamRenderIntervalMs;
  if (deadlineReached(afterFlush,
                      mediaStreamNextRenderMs + mediaStreamRenderIntervalMs * 2U)) {
    mediaStreamNextRenderMs = afterFlush + mediaStreamRenderIntervalMs;
  }

  if (deadlineReached(afterFlush, mediaStreamNextFrameMs + mediaStreamFrameIntervalMs * 4U)) {
    // A very long HTTP/I2C stall should not create an ever-growing catch-up
    // storm. Re-anchor only after stale frames have already been discarded.
    mediaStreamNextFrameMs = afterFlush + mediaStreamFrameIntervalMs;
  }
  if (mediaStreamQueueCount == 0) {
    mediaStreamPhase = MediaStreamPhase::BUFFERING;
    mediaStreamNextFrameMs = 0;
    mediaStreamNextRenderMs = 0;
  }
  return true;
}

const char *mediaStreamPhaseName() {
  switch (mediaStreamPhase) {
    case MediaStreamPhase::BUFFERING: return "buffering";
    case MediaStreamPhase::PLAYING: return "playing";
    default: return "inactive";
  }
}

bool mediaTransferBusy() {
  return mediaUploadActive || mediaStreamActive || mediaStreamRawActive;
}
