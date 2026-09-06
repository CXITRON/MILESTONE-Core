#pragma once
#ifdef ARDUINO_ARCH_ESP32
#include <Arduino.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>

namespace MilestoneV5 {
inline bool bootSafetyApplication() {
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "safety");
  esp_app_desc_t description;
  if (!p || esp_ota_get_partition_description(p, &description) != ESP_OK ||
      description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
      esp_ota_set_boot_partition(p) != ESP_OK)
    return false;
  ESP.restart();
  return true;
}
// Caller owns the grace period and only reports essential loop/self-test
// health. SD, radio, sensor and artwork availability are not candidate
// acceptance gates.
inline bool finishBootCandidate(bool essentialHealthy) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK)
    return false;
  if (state != ESP_OTA_IMG_PENDING_VERIFY)
    return true;
  return essentialHealthy
             ? esp_ota_mark_app_valid_cancel_rollback() == ESP_OK
             : esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
}

inline bool bootPreviousApplication() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
  if (!running || !other || other->address == running->address)
    return false;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(other, &state) != ESP_OK ||
      state != ESP_OTA_IMG_VALID)
    return false;
  esp_app_desc_t description;
  if (esp_ota_get_partition_description(other, &description) != ESP_OK ||
      description.magic_word != ESP_APP_DESC_MAGIC_WORD)
    return false;
  // IDF verifies the application image before changing otadata.
  if (esp_ota_set_boot_partition(other) != ESP_OK)
    return false;
  ESP.restart();
  return true;
}
} // namespace MilestoneV5
#endif
