#pragma once
#include <esp_ota_ops.h>
#include <cstring>
constexpr int JPEG_IMAGE_FORMAT_RGB565 = 1, JPEG_IMAGE_SCALE_0 = 0;
struct esp_jpeg_image_cfg_t {
  uint8_t *indata = nullptr, *outbuf = nullptr;
  size_t indata_size = 0, outbuf_size = 0;
  int out_format = 0, out_scale = 0;
};
struct esp_jpeg_image_output_t { unsigned width = 128, height = 128, output_len = 32768; };
inline int esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *, esp_jpeg_image_output_t *) { return ESP_OK; }
inline int esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *) {
  memset(cfg->outbuf, cfg->indata[0], cfg->outbuf_size);
  return ESP_OK;
}
