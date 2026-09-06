#pragma once
#include <algorithm>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
using esp_err_t = int;
using esp_ota_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
constexpr uint32_t OTA_WITH_SEQUENTIAL_WRITES = 0xfffffffe;
enum esp_ota_img_states_t { ESP_OTA_IMG_VALID, ESP_OTA_IMG_PENDING_VERIFY };
struct esp_partition_t {
  uint32_t address, size;
};
struct esp_app_desc_t {
  uint8_t app_elf_sha256[32];
};
namespace FakeOta {
static esp_partition_t partitions[] = {{0x10000, 4096}, {0x20000, 4096}};
static unsigned running = 0, beginCalls = 0, writeCalls = 0, endCalls = 0,
                abortCalls = 0;
static uint32_t now = 100, selected = 0, written = 0;
static bool active = false, writeFailure = false, endFailure = false,
            restarted = false;
static esp_ota_img_states_t bootState = ESP_OTA_IMG_VALID;
static std::vector<uint8_t> flash(4096, 255);
static uint8_t identities[2][32]{};
inline void reset() {
  running = beginCalls = writeCalls = endCalls = abortCalls = 0;
  now = 100;
  selected = written = 0;
  active = writeFailure = endFailure = restarted = false;
  bootState = ESP_OTA_IMG_VALID;
  std::fill(flash.begin(), flash.end(), 255);
  memset(identities[0], 17, 32);
  memset(identities[1], 34, 32);
}
} // namespace FakeOta
inline uint32_t millis() { return FakeOta::now; }
inline uint32_t esp_random() {
  static uint32_t sequence = 10;
  return ++sequence;
}
struct FakeEsp {
  void restart() { FakeOta::restarted = true; }
};
static FakeEsp ESP __attribute__((unused));
inline const esp_partition_t *esp_ota_get_next_update_partition(const void *) {
  return &FakeOta::partitions[1 - FakeOta::running];
}
inline const esp_partition_t *esp_ota_get_running_partition() {
  return &FakeOta::partitions[FakeOta::running];
}
inline int esp_ota_get_state_partition(const esp_partition_t *,
                                       esp_ota_img_states_t *state) {
  *state = FakeOta::bootState;
  return ESP_OK;
}
inline int esp_ota_get_partition_description(const esp_partition_t *p,
                                             esp_app_desc_t *description) {
  memcpy(description->app_elf_sha256,
         FakeOta::identities[p->address == 0x10000 ? 0 : 1], 32);
  return ESP_OK;
}
inline int esp_ota_begin(const esp_partition_t *, uint32_t,
                         esp_ota_handle_t *handle) {
  ++FakeOta::beginCalls;
  FakeOta::active = true;
  FakeOta::written = 0;
  *handle = 1;
  return ESP_OK;
}
inline int esp_ota_write(esp_ota_handle_t, const void *data, size_t n) {
  ++FakeOta::writeCalls;
  if (!FakeOta::active || FakeOta::writeFailure ||
      n > FakeOta::flash.size() - FakeOta::written)
    return ESP_FAIL;
  memcpy(FakeOta::flash.data() + FakeOta::written, data, n);
  FakeOta::written += n;
  return ESP_OK;
}
inline int esp_ota_end(esp_ota_handle_t) {
  ++FakeOta::endCalls;
  FakeOta::active = false;
  return FakeOta::endFailure ? ESP_FAIL : ESP_OK;
}
inline int esp_ota_abort(esp_ota_handle_t) {
  ++FakeOta::abortCalls;
  FakeOta::active = false;
  return ESP_OK;
}
inline int esp_ota_set_boot_partition(const esp_partition_t *p) {
  FakeOta::selected = p->address;
  return ESP_OK;
}
inline int esp_partition_read(const esp_partition_t *, uint32_t offset,
                              void *out, size_t n) {
  if (uint64_t(offset) + n > FakeOta::flash.size())
    return ESP_FAIL;
  memcpy(out, FakeOta::flash.data() + offset, n);
  return ESP_OK;
}
