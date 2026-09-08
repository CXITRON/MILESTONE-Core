#pragma once

#include "V5Tft.h"
#include <FS.h>
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Video.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>

// Staged browser-audio playback. The browser uploads a complete MVJ1 file to
// microSD first; MAIN builds a sidecar frame index and later follows the
// browser's audio timeline. No video frame crosses the network while playing.
class V5SyncMedia {
public:
  enum class State : uint8_t {
    Idle,
    Uploading,
    Indexing,
    Ready,
    Playing,
    Paused,
    Error
  };

  static constexpr uint32_t kMaximumBytes = 0x7FFFFFFFUL;
  static constexpr uint32_t kControlStaleMs = 2500;

  void begin(bool mounted) {
    available = mounted;
    stopFiles();
    cleanup();
    state = State::Idle;
    error = "";
    requestedFrame = displayedFrame = UINT32_MAX;
    controlCount = renderedFrames = lastRenderedMs = 0;
  }

  bool beginUpload(uint32_t expected, bool openEnded = false) {
    remove();
    if (!available || (!openEnded && expected < 16) ||
        expected > kMaximumBytes) {
      return fail("동기화 영상 크기가 올바르지 않습니다");
    }
    // Do not walk the FAT allocation table before an upload. On a slow or
    // marginal card a full FAT free-space walk can hold the MAIN loop long
    // enough for the browser and companion link to time out. Bounded writes
    // below remain the source of truth and fail safely if the card fills.
    upload = SD.open(kUploadPath, FILE_WRITE);
    if (!upload)
      return fail("동기화 임시 파일을 만들 수 없습니다");
    expectedBytes = expected;
    writtenBytes = 0;
    uploadOpenEnded = openEnded;
    state = State::Uploading;
    error = "";
    return true;
  }

  bool writeUpload(const uint8_t *data, size_t size) {
    const uint32_t limit = uploadOpenEnded ? kMaximumBytes : expectedBytes;
    if (state != State::Uploading || !upload || !data ||
        writtenBytes > limit || size > limit - writtenBytes ||
        upload.write(data, size) != size) {
      abortUpload();
      return fail("동기화 영상 기록에 실패했습니다");
    }
    writtenBytes += size;
    return true;
  }

  bool finishUpload() {
    if (state != State::Uploading || !upload || writtenBytes < 16 ||
        (!uploadOpenEnded && writtenBytes != expectedBytes)) {
      abortUpload();
      return fail("동기화 영상 크기가 일치하지 않습니다");
    }
    upload.flush();
    upload.close();
    expectedBytes = writtenBytes;
    uploadOpenEnded = false;
    source = SD.open(kUploadPath, FILE_READ);
    uint8_t header[16];
    if (!source || source.read(header, sizeof(header)) != sizeof(header) ||
        !MilestoneV5::decodeVideoHeader(header, sizeof(header), info)) {
      return failAndClean("MVJ1 헤더가 올바르지 않습니다");
    }
    scratch = static_cast<uint8_t *>(heap_caps_malloc(
        MilestoneV5::kVideoMaxJpeg, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!scratch)
      return failAndClean("동기화 검증용 PSRAM이 부족합니다");
    index = SD.open(kIndexTempPath, FILE_WRITE);
    if (!index)
      return failAndClean("동기화 인덱스를 만들 수 없습니다");
    uint8_t indexHeader[16] = {'M', 'V', 'X', '1'};
    put32(indexHeader + 4, info.frames);
    put32(indexHeader + 8, info.fps);
    put32(indexHeader + 12, expectedBytes);
    if (index.write(indexHeader, sizeof(indexHeader)) != sizeof(indexHeader))
      return failAndClean("동기화 인덱스 헤더 기록에 실패했습니다");
    indexedFrames = 0;
    state = State::Indexing;
    return true;
  }

  void serviceIndex(uint32_t now) {
    if (state != State::Indexing)
      return;
    const uint32_t started = micros();
    do {
      if (indexedFrames == info.frames) {
        if (source.position() != source.size())
          return void(failAndClean("MVJ1 끝에 알 수 없는 데이터가 있습니다"));
        source.close();
        index.flush();
        index.close();
        freeScratch();
        SD.remove(kVideoPath);
        SD.remove(kIndexPath);
        if (!SD.rename(kUploadPath, kVideoPath) ||
            !SD.rename(kIndexTempPath, kIndexPath))
          return void(failAndClean("동기화 파일 확정에 실패했습니다"));
        state = State::Ready;
        indexedAtMs = now;
        return;
      }
      const uint32_t offset = source.position();
      uint8_t record[8], entry[4];
      if (source.read(record, sizeof(record)) != sizeof(record))
        return void(failAndClean("MVJ1 프레임 헤더가 손상되었습니다"));
      const uint32_t length = MilestoneV5::readVideoU32(record);
      if (length < 4 || length > MilestoneV5::kVideoMaxJpeg ||
          length > source.size() - source.position() ||
          source.read(scratch, length) != length ||
          MilestoneV5::crc32(scratch, length) !=
              MilestoneV5::readVideoU32(record + 4))
        return void(failAndClean("MVJ1 프레임 CRC 검증에 실패했습니다"));
      esp_jpeg_image_cfg_t jpeg{};
      jpeg.indata = scratch;
      jpeg.indata_size = length;
      jpeg.out_format = JPEG_IMAGE_FORMAT_RGB565;
      jpeg.out_scale = JPEG_IMAGE_SCALE_0;
      esp_jpeg_image_output_t dimensions{};
      if (esp_jpeg_get_image_info(&jpeg, &dimensions) != ESP_OK ||
          dimensions.width != 128 || dimensions.height != 128)
        return void(failAndClean("MVJ1 JPEG 크기 검증에 실패했습니다"));
      put32(entry, offset);
      if (index.write(entry, sizeof(entry)) != sizeof(entry))
        return void(failAndClean("동기화 인덱스 기록에 실패했습니다"));
      ++indexedFrames;
    } while (micros() - started < 5000U);
  }

  bool control(uint32_t positionMs, bool run, uint32_t now) {
    if (state != State::Ready && state != State::Playing &&
        state != State::Paused)
      return false;
    if (!openPlayback())
      return false;
    const uint32_t duration = durationMs();
    anchorPositionMs = positionMs < duration ? positionMs : duration;
    anchorLocalMs = now;
    lastControlMs = now;
    ++controlCount;
    state = run && anchorPositionMs < duration ? State::Playing : State::Paused;
    return true;
  }

  bool servicePlayback(SimpleSt7735 &display, uint32_t now, bool monochrome) {
    if (state != State::Playing && state != State::Paused)
      return false;
    if (MilestoneV5::synchronizedVideoControlStale(
            now, lastControlMs, kControlStaleMs, state == State::Playing)) {
      anchorPositionMs = positionMs(now);
      anchorLocalMs = now;
      state = State::Paused;
      return false;
    }
    const uint32_t target = MilestoneV5::videoFrameAtMs(info, positionMs(now));
    requestedFrame = target;
    if (target == displayedFrame)
      return false;
    const uint32_t frameStartedUs = micros();
    uint8_t entry[4], record[8];
    if (!playIndex.seek(16U + target * 4U) ||
        playIndex.read(entry, sizeof(entry)) != sizeof(entry) ||
        !playVideo.seek(MilestoneV5::readVideoU32(entry)) ||
        playVideo.read(record, sizeof(record)) != sizeof(record))
      return playbackFail("동기화 프레임 탐색에 실패했습니다");
    const uint32_t length = MilestoneV5::readVideoU32(record);
    if (length < 4 || length > MilestoneV5::kVideoMaxJpeg ||
        playVideo.read(encoded, length) != length ||
        MilestoneV5::crc32(encoded, length) !=
            MilestoneV5::readVideoU32(record + 4))
      return playbackFail("동기화 프레임 CRC가 일치하지 않습니다");
    readUs = micros() - frameStartedUs;
    const uint32_t decodeStartedUs = micros();
    esp_jpeg_image_cfg_t cfg{};
    cfg.indata = encoded;
    cfg.indata_size = length;
    cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale = JPEG_IMAGE_SCALE_0;
    esp_jpeg_image_output_t output{};
    if (esp_jpeg_get_image_info(&cfg, &output) != ESP_OK ||
        output.width != 128 || output.height != 128 || output.output_len != 32768)
      return playbackFail("동기화 JPEG 크기가 올바르지 않습니다");
    cfg.outbuf = pixels;
    cfg.outbuf_size = 32768;
    if (esp_jpeg_decode(&cfg, &output) != ESP_OK)
      return playbackFail("동기화 JPEG 해독에 실패했습니다");
    uint16_t *rgb = reinterpret_cast<uint16_t *>(pixels);
    if (monochrome)
      for (unsigned i = 0; i < 128U * 128U; ++i) {
        const unsigned c = rgb[i];
        const unsigned y = ((((c >> 11) & 31) * 255 / 31 * 77) +
                            (((c >> 5) & 63) * 255 / 63 * 150) +
                            ((c & 31) * 255 / 31 * 29)) >> 8;
        rgb[i] = ((y & 248) << 8) | ((y & 252) << 3) | (y >> 3);
      }
    decodeUs = micros() - decodeStartedUs;
    const uint32_t outputStartedUs = micros();
    display.rgb565(rgb, 16, 128);
    display.flushRegion(16, 128);
    outputUs = micros() - outputStartedUs;
    frameUs = micros() - frameStartedUs;
    if (frameUs > maxFrameUs)
      maxFrameUs = frameUs;
    if (displayedFrame != UINT32_MAX && target > displayedFrame + 1)
      skippedFrames += target - displayedFrame - 1;
    displayedFrame = target;
    ++renderedFrames;
    lastRenderedMs = now;
    return true;
  }

  void invalidateDisplayedFrame() { displayedFrame = UINT32_MAX; }

  void stopPlayback() {
    playVideo.close();
    playIndex.close();
    free(encoded);
    free(pixels);
    encoded = pixels = nullptr;
    displayedFrame = UINT32_MAX;
    if (state == State::Playing || state == State::Paused)
      state = State::Ready;
  }

  void remove() {
    stopFiles();
    cleanup();
    state = State::Idle;
    error = "";
    expectedBytes = writtenBytes = indexedFrames = 0;
    uploadOpenEnded = false;
    requestedFrame = displayedFrame = UINT32_MAX;
    controlCount = renderedFrames = lastRenderedMs = 0;
    info = {};
  }

  void requestBrowserToggle() { ++browserEventSequence; }

  void abortUpload() {
    upload.close();
    if (state == State::Uploading) {
      SD.remove(kUploadPath);
      fail("동기화 업로드가 중단되었습니다");
    }
  }

  bool ready() const {
    return state == State::Ready || state == State::Playing ||
           state == State::Paused;
  }
  bool activePlayback() const {
    return state == State::Playing || state == State::Paused;
  }
  bool occupied() const { return state != State::Idle && state != State::Error; }
  uint32_t durationMs() const {
    return info.fps ? uint32_t((uint64_t(info.frames) * 1000U) / info.fps) : 0;
  }
  uint32_t positionMs(uint32_t now) const {
    return MilestoneV5::synchronizedVideoPositionMs(
        anchorPositionMs, anchorLocalMs, now, durationMs(),
        state == State::Playing);
  }
  const char *stateName() const {
    switch (state) {
    case State::Idle: return "idle";
    case State::Uploading: return "uploading";
    case State::Indexing: return "indexing";
    case State::Ready: return "ready";
    case State::Playing: return "playing";
    case State::Paused: return "paused";
    case State::Error: return "error";
    }
    return "error";
  }

  State state = State::Idle;
  String error;
  MilestoneV5::VideoInfo info{};
  uint32_t expectedBytes = 0, writtenBytes = 0, indexedFrames = 0;
  uint32_t indexedAtMs = 0, displayedFrame = UINT32_MAX;
  uint32_t requestedFrame = UINT32_MAX, controlCount = 0, renderedFrames = 0,
           lastRenderedMs = 0;
  uint8_t browserEventSequence = 0;
  bool uploadOpenEnded = false;
  uint32_t readUs = 0, decodeUs = 0, outputUs = 0, frameUs = 0,
           maxFrameUs = 0, skippedFrames = 0;

private:
  static constexpr const char *kDirectory = "/media/sync";
  static constexpr const char *kUploadPath = "/media/sync/video.tmp";
  static constexpr const char *kIndexTempPath = "/media/sync/index.tmp";
  static constexpr const char *kVideoPath = "/media/sync/video.mvj";
  static constexpr const char *kIndexPath = "/media/sync/video.idx";
  bool available = false;
  File upload, source, index, playVideo, playIndex;
  uint8_t *scratch = nullptr, *encoded = nullptr, *pixels = nullptr;
  uint32_t anchorPositionMs = 0, anchorLocalMs = 0, lastControlMs = 0;

  static void put32(uint8_t *p, uint32_t value) {
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
  }
  void cleanup() {
    if (!available)
      return;
    if (!SD.exists(kDirectory))
      SD.mkdir(kDirectory);
    SD.remove(kUploadPath);
    SD.remove(kIndexTempPath);
    SD.remove(kVideoPath);
    SD.remove(kIndexPath);
  }
  void freeScratch() {
    free(scratch);
    scratch = nullptr;
  }
  void stopFiles() {
    upload.close();
    source.close();
    index.close();
    stopPlayback();
    freeScratch();
  }
  bool openPlayback() {
    if (playVideo && playIndex)
      return true;
    playVideo = SD.open(kVideoPath, FILE_READ);
    playIndex = SD.open(kIndexPath, FILE_READ);
    uint8_t header[16];
    if (!playVideo || !playIndex ||
        playIndex.read(header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "MVX1", 4) ||
        MilestoneV5::readVideoU32(header + 4) != info.frames ||
        MilestoneV5::readVideoU32(header + 8) != info.fps ||
        MilestoneV5::readVideoU32(header + 12) != playVideo.size())
      return playbackFail("동기화 인덱스를 열 수 없습니다");
    encoded = static_cast<uint8_t *>(heap_caps_malloc(
        MilestoneV5::kVideoMaxJpeg, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    pixels = static_cast<uint8_t *>(
        heap_caps_malloc(128U * 128U * 2U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!encoded || !pixels)
      return playbackFail("동기화 재생용 PSRAM이 부족합니다");
    displayedFrame = UINT32_MAX;
    return true;
  }
  bool playbackFail(const char *message) {
    stopPlayback();
    state = State::Error;
    error = message;
    return false;
  }
  bool fail(const char *message) {
    state = State::Error;
    error = message;
    return false;
  }
  bool failAndClean(const char *message) {
    stopFiles();
    cleanup();
    return fail(message);
  }
};
