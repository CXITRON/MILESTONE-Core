#pragma once
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Video.h>
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>

class V5Video {
public:
  bool playing = false, paused = false, repeat = true;
  bool monochrome = false;
  const char *error = "";
  bool open(const String &path) {
    stop();
    error = "";
    file = SD.open(path, FILE_READ);
    uint8_t header[16];
    if (!file || file.read(header, 16) != 16 ||
        !MilestoneV5::decodeVideoHeader(header, 16, info))
      return fail("MVJ1 헤더 오류");
    encoded = static_cast<uint8_t *>(heap_caps_malloc(
        MilestoneV5::kVideoMaxJpeg, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    pixels = static_cast<uint8_t *>(
        heap_caps_malloc(128 * 128 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!encoded || !pixels)
      return fail("PSRAM 사용 불가");
    frame = 0;
    next = millis();
    playing = true;
    prefetched = false;
    return true;
  }
  void stop() {
    file.close();
    free(encoded);
    free(pixels);
    encoded = nullptr;
    pixels = nullptr;
    playing = false;
    paused = false;
    prefetched = false;
  }
  void toggle() {
    if (playing) {
      paused = !paused;
      next = millis();
    }
  }
  bool service(SimpleSt7735 &display, uint32_t now) {
    if (!playing || paused)
      return false;
    // Read and CRC-check the next encoded frame in a separate loop turn. Its
    // PSRAM buffer stays ready while the UI and link service their deadlines.
    if (!prefetched) {
      prefetch();
      return false;
    }
    if (int32_t(now - next) < 0)
      return false;
    esp_jpeg_image_cfg_t cfg{};
    cfg.indata = encoded;
    cfg.indata_size = prefetchedLength;
    cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale = JPEG_IMAGE_SCALE_0;
    esp_jpeg_image_output_t output{};
    if (esp_jpeg_get_image_info(&cfg, &output) != ESP_OK ||
        output.width != 128 || output.height != 128 ||
        output.output_len != 32768)
      return fail("JPEG 크기 오류");
    cfg.outbuf = pixels;
    cfg.outbuf_size = 32768;
    if (esp_jpeg_decode(&cfg, &output) != ESP_OK)
      return fail("JPEG 해독 실패");
    uint16_t *rgb = reinterpret_cast<uint16_t *>(pixels);
    if (monochrome)
      for (unsigned i = 0; i < 128 * 128; ++i) {
        unsigned c = rgb[i];
        unsigned y =
            (((c >> 11) & 31) * 255 / 31 * 77 +
             ((c >> 5) & 63) * 255 / 63 * 150 + (c & 31) * 255 / 31 * 29) >>
            8;
        rgb[i] = ((y & 248) << 8) | ((y & 252) << 3) | (y >> 3);
      }
    display.rgb565(rgb, 16, 128);
    display.flushRegion(16, 128);
    ++frame;
    next = now + 1000 / info.fps;
    prefetched = false;
    return true;
  }

private:
  bool prefetch() {
    if (frame == info.frames) {
      if (file.position() != file.size())
        return fail("영상 끝 데이터 오류");
      if (!repeat) {
        stop();
        return false;
      }
      if (!file.seek(16))
        return fail("영상 탐색 실패");
      frame = 0;
    }
    uint8_t record[8];
    if (file.read(record, 8) != 8)
      return fail("프레임 헤더 손상");
    const uint32_t length = MilestoneV5::readVideoU32(record);
    if (length < 4 || length > MilestoneV5::kVideoMaxJpeg ||
        length > file.size() - file.position())
      return fail("프레임 크기 오류");
    if (file.read(encoded, length) != length ||
        MilestoneV5::crc32(encoded, length) !=
            MilestoneV5::readVideoU32(record + 4))
      return fail("프레임 CRC 불일치");
    prefetchedLength = length;
    prefetched = true;
    return true;
  }
  File file;
  uint8_t *encoded = nullptr, *pixels = nullptr;
  MilestoneV5::VideoInfo info{};
  uint32_t frame = 0, next = 0;
  uint32_t prefetchedLength = 0;
  bool prefetched = false;
  bool fail(const char *message) {
    stop();
    error = message;
    return false;
  }
};
