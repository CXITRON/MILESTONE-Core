// MILESTONE Core v1.8.0 safe OTA rollback layer.
//
// This layer intentionally does not alter Wi-Fi, DHCP, NTP, manifest checks,
// or the OTA transfer loop. It only records the currently running OTA slot
// before installation, validates the candidate after reboot, and can select
// the previous verified slot again if the candidate reboots before the
// stability hold completes.

namespace RollbackRecord {
constexpr uint8_t NONE = 0;
constexpr uint8_t PREPARED = 1;
constexpr uint8_t ARMED = 2;
constexpr uint8_t CONFIRMING = 3;
}

bool putRollbackByte(const char *key, uint8_t value) {
  if (!prefsReady) return false;
  return prefs.putUChar(key, value) == sizeof(uint8_t) && prefs.getUChar(key, 0xFF) == value;
}

String otaPartitionVersion(const esp_partition_t *partition) {
  if (!partition) return "";
  esp_app_desc_t description = {};
  if (esp_ota_get_partition_description(partition, &description) != ESP_OK) return "";
  return String(description.version);
}

const esp_partition_t *findRollbackPartitionByLabel(const String &label) {
  if (!label.length()) return nullptr;
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label.c_str());
  if (!partition || partition == esp_ota_get_running_partition()) return nullptr;
  esp_app_desc_t description = {};
  return esp_ota_get_partition_description(partition, &description) == ESP_OK ? partition : nullptr;
}

const esp_partition_t *discoverPreviousOtaPartition() {
  // With the two-slot OTA layout, the next writable slot from the currently
  // running candidate is the slot that held the previous firmware. The API
  // guarantees that this result is not the running partition.
  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition || partition == esp_ota_get_running_partition()) return nullptr;
  esp_app_desc_t description = {};
  return esp_ota_get_partition_description(partition, &description) == ESP_OK ? partition : nullptr;
}

bool clearRollbackActiveRecord() {
  if (!prefsReady) return false;
  // Disarm first. If cleanup is interrupted after this verified byte write,
  // stale descriptive fields can never trigger an unintended rollback.
  if (!putRollbackByte("rb_state", RollbackRecord::NONE)) return false;
  bool success = true;
  if (!removePreferenceVerified("rb_prev")) success = false;
  if (!removePreferenceVerified("rb_prevver")) success = false;
  if (!removePreferenceVerified("rb_target")) success = false;
  if (!removePreferenceVerified("rb_boots")) success = false;
  otaRollbackArmed = false;
  otaRollbackBootAttempts = 0;
  otaRollbackPreviousLabel = "";
  otaRollbackPreviousVersion = "";
  otaRollbackTargetVersion = "";
  return success;
}

void loadRollbackDiagnostics() {
  if (!prefsReady) return;
  otaRollbackLastAction = prefs.getString("rb_last", "");
  otaRollbackLastReason = prefs.getString("rb_reason", "");
}

bool prepareRollbackRecord(const String &targetVersion) {
  if (!prefsReady) return false;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running || running->type != ESP_PARTITION_TYPE_APP) return false;
  const String previousVersion = otaPartitionVersion(running);
  if (!previousVersion.length()) return false;

  if (!putStringVerified("rb_prev", running->label) ||
      !putStringVerified("rb_prevver", previousVersion) ||
      !putStringVerified("rb_target", targetVersion) ||
      !putRollbackByte("rb_boots", 0) ||
      !putRollbackByte("rb_state", RollbackRecord::PREPARED)) {
    clearRollbackActiveRecord();
    return false;
  }
  otaRollbackPreviousLabel = running->label;
  otaRollbackPreviousVersion = previousVersion;
  otaRollbackTargetVersion = targetVersion;
  otaRollbackBootAttempts = 0;
  otaRollbackArmed = false;
  logLine(String("rollback source prepared: ") + previousVersion + " [" + running->label + "]");
  return true;
}

bool armRollbackRecord() {
  if (!prefsReady) return false;
  const String target = prefs.getString("rb_target", "");
  const String previous = prefs.getString("rb_prev", "");
  if (!target.length() || !previous.length()) return false;
  if (!putRollbackByte("rb_boots", 0)) return false;
  if (!putRollbackByte("rb_state", RollbackRecord::ARMED)) return false;
  otaRollbackArmed = true;
  otaRollbackBootAttempts = 0;
  recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_ARMED, 0, 0, true);
  logLine(String("rollback protection armed for ") + target + " -> " + previous);
  return true;
}

bool adoptLegacyRollbackRecord(const String &targetVersion) {
  // v1.7.0 already records ota_stage/ota_target, but it predates the explicit
  // rollback source record. On the first v1.8.0 OTA boot, recover the previous
  // slot from the two-slot OTA layout so the very first 1.8.0 upgrade can also
  // receive application-level rollback protection after this guard executes.
  const esp_partition_t *previous = discoverPreviousOtaPartition();
  if (!previous) return false;
  const String previousVersion = otaPartitionVersion(previous);
  if (!previousVersion.length()) return false;
  if (!putStringVerified("rb_prev", previous->label) ||
      !putStringVerified("rb_prevver", previousVersion) ||
      !putStringVerified("rb_target", targetVersion) ||
      !putRollbackByte("rb_boots", 0) ||
      !putRollbackByte("rb_state", RollbackRecord::ARMED)) {
    clearRollbackActiveRecord();
    return false;
  }
  recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_ARMED, 0, 0, true);
  logLine(String("rollback source adopted from previous OTA slot: ") +
          previousVersion + " [" + previous->label + "]");
  return true;
}

const esp_partition_t *resolveRollbackPartition() {
  if (!prefsReady) return nullptr;
  const String label = prefs.getString("rb_prev", "");
  const esp_partition_t *partition = findRollbackPartitionByLabel(label);
  if (partition) return partition;
  return discoverPreviousOtaPartition();
}

bool rollbackToPreviousFirmware(const String &reason) {
  if (!prefsReady) return false;
  const esp_partition_t *previous = resolveRollbackPartition();
  if (!previous) {
    otaRollbackLastAction = "failed";
    otaRollbackLastReason = "previous OTA partition unavailable";
    putStringVerified("rb_last", otaRollbackLastAction);
    putStringVerified("rb_reason", otaRollbackLastReason);
    recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_TRIGGERED,
                     static_cast<int16_t>(MilestoneDiagnostics::RollbackAction::FAILED), 0, true);
    logLine("rollback failed: previous OTA partition unavailable");
    return false;
  }

  const String previousVersion = otaPartitionVersion(previous);
  const esp_err_t result = esp_ota_set_boot_partition(previous);
  if (result != ESP_OK) {
    otaRollbackLastAction = "failed";
    otaRollbackLastReason = String("boot partition error ") + String(static_cast<int>(result));
    putStringVerified("rb_last", otaRollbackLastAction);
    putStringVerified("rb_reason", otaRollbackLastReason);
    recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_TRIGGERED,
                     static_cast<int16_t>(MilestoneDiagnostics::RollbackAction::FAILED),
                     static_cast<int32_t>(result), true);
    logLine(String("rollback boot selection failed: ") + String(static_cast<int>(result)));
    return false;
  }

  // Persist diagnostics before disarming. The boot target has already been
  // switched, so a restart after this point returns to the previous image.
  putStringVerified("rb_last", "automatic");
  putStringVerified("rb_from", FIRMWARE_VERSION);
  putStringVerified("rb_to", previousVersion);
  putStringVerified("rb_reason", reason);
  putStringVerified("ota_result", "rolled-back");
  removePreferenceVerified("ota_stage");
  removePreferenceVerified("ota_target");
  clearRollbackActiveRecord();
  recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_TRIGGERED,
                   static_cast<int16_t>(MilestoneDiagnostics::RollbackAction::APPLICATION), 0, true);
  logLine(String("ROLLBACK: ") + FIRMWARE_VERSION + " -> " + previousVersion +
          " [" + previous->label + "] reason=" + reason);
  delay(250);
  ESP.restart();
  return true;
}

void initializeRollbackProtection(const String &previousOtaStage, const String &previousOtaTarget) {
  loadRollbackDiagnostics();
  if (!prefsReady) return;

  uint8_t recordState = prefs.getUChar("rb_state", RollbackRecord::NONE);
  const bool freshCandidate = previousOtaStage == "rebooting" && previousOtaTarget == FIRMWARE_VERSION;

  if (freshCandidate && recordState == RollbackRecord::NONE) {
    if (adoptLegacyRollbackRecord(previousOtaTarget)) {
      recordState = RollbackRecord::ARMED;
    } else {
      // Preserve the pre-1.8.0 10-second confirmation behavior even when no
      // previous slot can be validated. This boot is simply not protected by
      // the application-level fallback.
      logLine("rollback protection unavailable: previous OTA slot could not be adopted");
      otaBootConfirmationPending = true;
      otaBootConfirmationStartedMs = millis();
      recordDiagnostic(MilestoneDiagnostics::Event::BOOT_CANDIDATE, 0, 0, true);
      lastOtaResult = "validating";
      putStringVerified("ota_result", lastOtaResult);
      return;
    }
  }

  const String recordedTarget = prefs.getString("rb_target", "");
  if (recordState == RollbackRecord::CONFIRMING && recordedTarget == FIRMWARE_VERSION) {
    // Power was lost after the candidate passed the stability hold but before
    // cleanup finished. Never roll back a candidate that was already accepted;
    // finish the bootloader/NVS confirmation on the next loop instead.
    otaRollbackPreviousLabel = prefs.getString("rb_prev", "");
    otaRollbackPreviousVersion = prefs.getString("rb_prevver", "");
    otaRollbackTargetVersion = recordedTarget;
    otaRollbackArmed = false;
    otaBootConfirmationPending = true;
    otaBootConfirmationStartedMs = millis() - OTA_BOOT_CONFIRM_MS;
    recordDiagnostic(MilestoneDiagnostics::Event::BOOT_CANDIDATE, 0, 0, true);
    lastOtaResult = "validating";
    putStringVerified("ota_result", lastOtaResult);
    logLine("resuming interrupted OTA confirmation");
    return;
  }
  if ((recordState == RollbackRecord::ARMED || recordState == RollbackRecord::PREPARED) &&
      recordedTarget == FIRMWARE_VERSION) {
    if (recordState == RollbackRecord::PREPARED) {
      // A candidate should normally only boot after the record was armed. If a
      // reset occurred in the tiny window after Update.end(), promote the
      // prepared record rather than leaving the candidate unprotected.
      if (!putRollbackByte("rb_state", RollbackRecord::ARMED)) {
        rollbackToPreviousFirmware("rollback record could not be armed");
        return;
      }
    }

    otaRollbackPreviousLabel = prefs.getString("rb_prev", "");
    otaRollbackPreviousVersion = prefs.getString("rb_prevver", "");
    otaRollbackTargetVersion = recordedTarget;
    otaRollbackBootAttempts = prefs.getUChar("rb_boots", 0);
    otaRollbackArmed = true;

    if (otaRollbackBootAttempts >= 1) {
      const String reason = String("candidate rebooted before confirmation; reset=") +
                            resetReasonName(esp_reset_reason());
      rollbackToPreviousFirmware(reason);
      return;
    }

    if (!putRollbackByte("rb_boots", 1)) {
      rollbackToPreviousFirmware("candidate boot counter could not be persisted");
      return;
    }
    otaRollbackBootAttempts = 1;
    otaBootConfirmationPending = true;
    otaBootConfirmationStartedMs = millis();
    recordDiagnostic(MilestoneDiagnostics::Event::BOOT_CANDIDATE, 0, 0, true);
    lastOtaResult = "validating";
    putStringVerified("ota_result", lastOtaResult);
    logLine(String("OTA candidate protected: ") + FIRMWARE_VERSION +
            "; fallback=" + otaRollbackPreviousVersion +
            " [" + otaRollbackPreviousLabel + "]");
    return;
  }

  if (recordState != RollbackRecord::NONE && recordedTarget != FIRMWARE_VERSION) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const String previousLabel = prefs.getString("rb_prev", "");
    if ((recordState == RollbackRecord::ARMED || recordState == RollbackRecord::PREPARED) &&
        running && previousLabel.length() && previousLabel == running->label) {
      // A rollback-enabled bootloader may have returned here before the failed
      // candidate could run our second-boot guard. Preserve that event in the
      // same diagnostics used by application-level rollback.
      otaRollbackLastAction = "bootloader";
      otaRollbackLastReason = "candidate reset before bootloader validation";
      putStringVerified("rb_last", otaRollbackLastAction);
      putStringVerified("rb_from", recordedTarget);
      putStringVerified("rb_to", FIRMWARE_VERSION);
      putStringVerified("rb_reason", otaRollbackLastReason);
      putStringVerified("ota_result", "rolled-back");
      lastOtaResult = "rolled-back";
      recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_TRIGGERED,
                       static_cast<int16_t>(MilestoneDiagnostics::RollbackAction::BOOTLOADER), 0, true);
      logLine(String("bootloader rollback detected: ") + recordedTarget + " -> " + FIRMWARE_VERSION);
    } else {
      logLine(String("clearing stale rollback record for target ") + recordedTarget);
    }
    clearRollbackActiveRecord();
  }
}

void processRollbackConfirmation() {
  if (!otaBootConfirmationPending ||
      !elapsed(millis(), otaBootConfirmationStartedMs, OTA_BOOT_CONFIRM_MS)) return;

  // Enter a non-rollback state first. If power is lost during the remaining
  // cleanup, the next boot will finish confirmation rather than falsely
  // returning to an older application.
  if (!putRollbackByte("rb_state", RollbackRecord::CONFIRMING)) {
    logLine("OTA candidate confirmation deferred: rollback state save failed");
    return;
  }

  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) logLine("OTA application marked valid by bootloader");
  else logLine(String("bootloader rollback validation not active: ") + String(static_cast<int>(result)));

  putStringVerified("rb_last", "confirmed");
  putStringVerified("rb_from", otaRollbackPreviousVersion);
  putStringVerified("rb_to", FIRMWARE_VERSION);
  removePreferenceVerified("rb_reason");

  if (!clearRollbackActiveRecord()) {
    logLine("OTA rollback metadata cleanup incomplete; protection is already disarmed");
  }
  otaBootConfirmationPending = false;
  lastOtaResult = "installed";
  if (!putStringVerified("ota_result", lastOtaResult)) logLine("OTA confirmation result save failed");
  if (!removePreferenceVerified("ota_stage")) logLine("OTA confirmation stage clear failed");
  if (!removePreferenceVerified("ota_target")) logLine("OTA confirmation target clear failed");
  otaRollbackLastAction = "confirmed";
  otaRollbackLastReason = "";
  recordDiagnostic(MilestoneDiagnostics::Event::ROLLBACK_CONFIRMED, 0, 0, true);
  logLine(String("OTA boot confirmed after stability hold: ") + FIRMWARE_VERSION);
}
