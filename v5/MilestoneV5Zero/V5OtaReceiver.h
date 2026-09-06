#pragma once
#include <MilestoneV5OtaWire.h>
#include <MilestoneV5Signature.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

// SPI loop exclusively owns this receiver. Flash is untouched until a signed
// ZERO manifest has passed verification. The running partition is never erased.
class V5OtaReceiver {
public:
  MilestoneV5::RemoteOtaState state = MilestoneV5::RemoteOtaState::Idle;
  uint32_t id = 0, received = 0;
  bool active() const {
    return state == S::Manifest || state == S::Quiescing ||
           state == S::Writing || state == S::Ready || state == S::Rebooting;
  }
  bool needsQuiescence() const {
    return state == S::Quiescing || state == S::Writing || state == S::Ready ||
           state == S::Rebooting;
  }
  void service(bool radioQuiet, bool safe, uint32_t now) {
    if (active() && (!safe || now - lastRequest > 15000)) {
      fail();
      return;
    }
    if (state == S::Quiescing && radioQuiet) {
      if (esp_ota_begin(destination, OTA_WITH_SEQUENTIAL_WRITES, &handle) !=
          ESP_OK) {
        fail();
        return;
      }
      otaActive = true;
      mbedtls_sha256_init(&sha);
      shaActive = true;
      if (mbedtls_sha256_starts(&sha, 0)) {
        fail();
        return;
      }
      received = 0;
      state = S::Writing;
    }
    if (state == S::Rebooting && now - rebootAt >= 500)
      ESP.restart();
  }
  bool request(const uint8_t *p, size_t n, uint8_t *out, size_t &length) {
    if (!MilestoneV5::validOtaRequest(p, n))
      return false;
    uint32_t requested = MilestoneV5::otaU32(p + 1);
    uint8_t op = p[0];
    if (op == 6) {
      replyReceipt(requested, out, length);
      return true;
    }
    if (op == 1) {
      if (active() && id != requested) {
        reply(op, requested, S::Failed, 0, out, length);
        return true;
      }
      if (!active() || id != requested) {
        cleanup();
        id = requested;
        received = 0;
        manifestLength = unsigned(p[5]) | unsigned(p[6]) << 8;
        signatureLength = unsigned(p[7]) | unsigned(p[8]) << 8;
        state = S::Manifest;
      }
    }
    if (requested != id) {
      reply(op, requested, S::Failed, 0, out, length);
      return true;
    }
    lastRequest = millis();
    if (op == 7) {
      fail();
    } else if (op == 2 && state == S::Manifest) {
      uint32_t offset = MilestoneV5::otaU32(p + 5);
      size_t take = n - 9, total = manifestLength + signatureLength;
      if (uint64_t(offset) + take > total || offset > received) {
        fail();
      } else if (offset < received) {
        if (uint64_t(offset) + take > received ||
            memcmp(metadata + offset, p + 9, take))
          fail();
      } else {
        memcpy(metadata + received, p + 9, take);
        received += take;
        if (received == total) {
          if (!MilestoneV5::decodeImageManifest(metadata, manifestLength,
                                                manifest) ||
              manifest.target != MilestoneV5::ManifestTarget::Zero ||
              MilestoneV5::kProtocolVersion < manifest.minimumPeerProtocol ||
              MilestoneV5::kProtocolVersion > manifest.maximumPeerProtocol ||
              !MilestoneV5::verifyImageSignature(metadata, manifestLength,
                                                 metadata + manifestLength,
                                                 signatureLength)) {
            fail();
          } else {
            destination = esp_ota_get_next_update_partition(nullptr);
            if (!destination || manifest.bytes > destination->size)
              fail();
            else
              state = S::Quiescing;
          }
        }
      }
    } else if (op == 3 && state == S::Writing) {
      uint32_t offset = MilestoneV5::otaU32(p + 5);
      size_t take = n - 9;
      if (offset > received || uint64_t(offset) + take > manifest.bytes)
        fail();
      else if (offset < received) {
        uint8_t check[MilestoneV5::kRemoteOtaChunk];
        if (uint64_t(offset) + take > received ||
            esp_partition_read(destination, offset, check, take) != ESP_OK ||
            memcmp(check, p + 9, take))
          fail();
      } else if (mbedtls_sha256_update(&sha, p + 9, take) ||
                 esp_ota_write(handle, p + 9, take) != ESP_OK)
        fail();
      else
        received += take;
    } else if (op == 4 && state == S::Writing) {
      uint8_t hash[32];
      if (received != manifest.bytes || mbedtls_sha256_finish(&sha, hash) ||
          memcmp(hash, manifest.sha256, 32))
        fail();
      else {
        esp_err_t result = esp_ota_end(handle);
        otaActive = false;
        if (result != ESP_OK)
          fail();
        else {
          cleanup();
          state = S::Ready;
        }
      }
    } else if (op == 5 && state == S::Ready) {
      // Receipt identifies the destination address, not merely the requested
      // hash: a rolled-back old app must not claim the new image was accepted.
      uint8_t record[84]{};
      memcpy(record, "VO02", 4);
      MilestoneV5::otaPut32(record + 4, id);
      MilestoneV5::otaPut32(record + 8, destination->address);
      MilestoneV5::otaPut32(record + 12, manifest.bytes);
      esp_app_desc_t description{};
      if (esp_ota_get_partition_description(destination, &description) !=
          ESP_OK) {
        fail();
        reply(op, id, state, received, out, length);
        return true;
      }
      memcpy(record + 16, manifest.sha256, 32);
      memcpy(record + 48, description.app_elf_sha256, 32);
      MilestoneV5::otaPut32(record + 80, MilestoneV5::crc32(record, 80));
      Preferences prefs;
      uint8_t check[84];
      bool saved =
          prefs.begin("v5_ota", false) &&
          prefs.putBytes("receipt", record, sizeof(record)) == sizeof(record) &&
          prefs.getBytes("receipt", check, sizeof(check)) == sizeof(check) &&
          !memcmp(record, check, sizeof(record));
      prefs.end();
      if (!saved || esp_ota_set_boot_partition(destination) != ESP_OK)
        fail();
      else {
        state = S::Rebooting;
        rebootAt = millis();
      }
    }
    reply(op, id, state, received, out, length);
    return true;
  }

private:
  using S = MilestoneV5::RemoteOtaState;
  uint8_t metadata[767]{};
  uint16_t manifestLength = 0, signatureLength = 0;
  MilestoneV5::SignedImageManifest manifest{};
  uint32_t lastRequest = 0, rebootAt = 0;
  const esp_partition_t *destination = nullptr;
  esp_ota_handle_t handle = 0;
  bool otaActive = false, shaActive = false;
  mbedtls_sha256_context sha;
  void cleanup() {
    if (otaActive) {
      esp_ota_abort(handle);
      otaActive = false;
    }
    if (shaActive) {
      mbedtls_sha256_free(&sha);
      shaActive = false;
    }
  }
  void fail() {
    cleanup();
    state = S::Failed;
  }
  static void reply(uint8_t op, uint32_t id, S state, uint32_t received,
                    uint8_t *out, size_t &n) {
    out[0] = op;
    MilestoneV5::otaPut32(out + 1, id);
    out[5] = uint8_t(state);
    MilestoneV5::otaPut32(out + 6, received);
    n = 10;
  }
  void replyReceipt(uint32_t requested, uint8_t *out, size_t &n) {
    S result = S::Failed;
    uint8_t record[84];
    Preferences p;
    bool valid =
        p.begin("v5_ota", true) &&
        p.getBytesLength("receipt") == sizeof(record) &&
        p.getBytes("receipt", record, sizeof(record)) == sizeof(record);
    p.end();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t bootState;
    esp_app_desc_t description{};
    if (valid && !memcmp(record, "VO02", 4) &&
        MilestoneV5::otaU32(record + 80) == MilestoneV5::crc32(record, 80) &&
        MilestoneV5::otaU32(record + 4) == requested && running &&
        running->address == MilestoneV5::otaU32(record + 8) &&
        esp_ota_get_partition_description(running, &description) == ESP_OK &&
        !memcmp(description.app_elf_sha256, record + 48, 32) &&
        esp_ota_get_state_partition(running, &bootState) == ESP_OK) {
      if (bootState == ESP_OTA_IMG_VALID)
        result = S::Complete;
      else if (bootState == ESP_OTA_IMG_PENDING_VERIFY)
        result = S::BootTesting;
    }
    reply(6, requested, result, 0, out, n);
  }
};
