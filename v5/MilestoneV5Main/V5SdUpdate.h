#pragma once
#include <MilestoneV5Signature.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

// Release key must be embedded in the firmware, never loaded from the SD card.
// Empty key fails closed. Provisioning a release key is a release prerequisite.
#ifndef MILESTONE_V5_RELEASE_PUBLIC_KEY
#define MILESTONE_V5_RELEASE_PUBLIC_KEY ""
#endif

class V5SdUpdate {
public:
  enum class State { Idle, Hashing, Writing, Ready, Failed };
  State state = State::Idle;
  String error;
  uint32_t size = 0, done = 0;

  bool begin(const char *directory, const uint8_t *requiredSha = nullptr) {
    if (state == State::Hashing || state == State::Writing)
      return false;
    cleanup();
    error = "";
    state = State::Idle;
    done = 0;
    if (!MILESTONE_V5_RELEASE_PUBLIC_KEY[0])
      return fail("릴리스 키 미설정");
    String base(directory);
    File manifest = SD.open(base + "/manifest.txt", FILE_READ),
         signature = SD.open(base + "/manifest.sig", FILE_READ);
    if (!manifest || !signature || manifest.size() == 0 ||
        manifest.size() > 255 || signature.size() == 0 ||
        signature.size() > 512)
      return fail("서명된 매니페스트 없음");
    uint8_t message[256]{}, sig[512];
    size_t n = manifest.size(), sn = signature.size();
    if (manifest.read(message, n) != n || signature.read(sig, sn) != sn)
      return fail("매니페스트 읽기 실패");
    manifest.close();
    signature.close();
    MilestoneV5::SignedImageManifest metadata{};
    if (!MilestoneV5::decodeImageManifest(message, n, metadata) ||
        metadata.target != MilestoneV5::ManifestTarget::Main ||
        MilestoneV5::kProtocolVersion < metadata.minimumPeerProtocol ||
        MilestoneV5::kProtocolVersion > metadata.maximumPeerProtocol)
      return fail("매니페스트 호환성 오류");
    if (requiredSha && memcmp(requiredSha, metadata.sha256, 32))
      return fail("묶음 MAIN 해시 불일치");
    if (!MilestoneV5::verifyImageSignature(message, n, sig, sn))
      return fail("서명 검증 실패");
    uint32_t bytes = metadata.bytes;
    memcpy(expected, metadata.sha256, 32);
    destination = esp_ota_get_next_update_partition(nullptr);
    if (!destination || bytes > destination->size)
      return fail("이미지가 OTA 슬롯 초과");
    file = SD.open(base + "/firmware.bin", FILE_READ);
    if (!file || file.size() != bytes)
      return fail("펌웨어 크기 불일치");
    size = bytes;
    mbedtls_sha256_init(&sha);
    shaActive = true;
    if (mbedtls_sha256_starts(&sha, 0) != 0)
      return fail("해시 초기화 실패");
    state = State::Hashing;
    return true;
  }

  void service() {
    if (state != State::Hashing && state != State::Writing)
      return;
    uint8_t buffer[2048];
    size_t take = min(size - done, uint32_t(sizeof(buffer)));
    if (take) {
      if (file.read(buffer, take) != take)
        return void(fail("펌웨어 읽기 실패"));
      if (mbedtls_sha256_update(&sha, buffer, take) != 0)
        return void(fail("해시 계산 실패"));
      if (state == State::Writing &&
          esp_ota_write(handle, buffer, take) != ESP_OK)
        return void(fail("OTA 기록 실패"));
      done += take;
      return;
    }
    uint8_t digest[32];
    if (mbedtls_sha256_finish(&sha, digest) != 0 ||
        memcmp(digest, expected, 32))
      return void(fail("펌웨어 SHA-256 불일치"));
    if (state == State::Hashing) {
      if (!file.seek(0))
        return void(fail("펌웨어 탐색 실패"));
      // Validation precedes Flash erase. Hash again while writing to catch
      // changed SD data.
      if (esp_ota_begin(destination, size, &handle) != ESP_OK)
        return void(fail("OTA 시작 실패"));
      otaActive = true;
      done = 0;
      if (mbedtls_sha256_starts(&sha, 0) != 0)
        return void(fail("해시 재시작 실패"));
      state = State::Writing;
    } else {
      const esp_err_t result = esp_ota_end(handle);
      otaActive = false;
      if (result != ESP_OK)
        return void(fail("이미지 검증 실패"));
      if (esp_ota_set_boot_partition(destination) != ESP_OK)
        return void(fail("부팅 슬롯 선택 실패"));
      cleanup();
      state = State::Ready;
    }
  }
  void cancel() {
    if (state == State::Hashing || state == State::Writing)
      fail("업데이트 취소됨");
  }

private:
  File file;
  const esp_partition_t *destination = nullptr;
  esp_ota_handle_t handle = 0;
  bool otaActive = false, shaActive = false;
  uint8_t expected[32]{};
  mbedtls_sha256_context sha;
  void cleanup() {
    if (otaActive) {
      esp_ota_abort(handle);
      otaActive = false;
    }
    file.close();
    if (shaActive) {
      mbedtls_sha256_free(&sha);
      shaActive = false;
    }
  }
  bool fail(const char *message) {
    cleanup();
    error = message;
    state = State::Failed;
    return false;
  }
};
