// CORE profile compatibility boundary for shared config and factory reset.
// No media storage, playback, stream transport, or LittleFS implementation is
// linked into CORE. These constant stubs satisfy the few common call sites
// while preserving the schema values used when switching back to MEDIA.

constexpr uint8_t MEDIA_DISABLED_DISPLAY_SECONDS = 8;

constexpr bool mediaStreamActive = false;
constexpr bool mediaStreamRawActive = false;
constexpr bool mediaPlaybackFailed = false;

bool mediaTransferBusy() { return false; }
bool mediaHasEnabledItems() { return false; }
void closeMediaPlayback() {}
void stopMediaStream() {}
bool selectNextMediaItem() { return false; }
uint8_t currentMediaDisplaySeconds() { return MEDIA_DISABLED_DISPLAY_SECONDS; }
bool mediaDisplayOwnsFrame() { return false; }
bool renderMediaFrameIfNeeded(uint32_t) { return false; }
bool copyCurrentMediaFrameToDisplayBuffer() { return false; }
bool initializeMediaStorage() { return false; }

bool clearAllMediaFiles() {
  // CORE does not mount LittleFS, but an explicit factory reset must still
  // remove MEDIA data from the shared flash partition.
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!partition) return true;
  return esp_partition_erase_range(partition, 0, partition->size) == ESP_OK;
}
