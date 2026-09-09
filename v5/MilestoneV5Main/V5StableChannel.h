#pragma once
#include "V5BundleDownload.h"
#include "V5BundleUpdate.h"

// Uses the existing bounded transfer and verified SD set writer, never Flash.
class V5StableChannel {
public:
  V5BundleDownload download;
  String status = "아직 확인하지 않음", version;
  bool busy() const { return phase != 0; }
  void yieldToUser() {
    if (!phase || phase == 3)
      return;
    if (download.active)
      download.stop("사용자 업데이트 요청으로 안정본 작업 보류");
    fail(download, "사용자 요청으로 안정본 작업 보류");
  }
  void service(uint32_t now, bool eligible, bool onZero,
               V5BundleUpdate &archive) {
    if (!phase) {
      if (!eligible || now < 45000 ||
          (attempted && uint32_t(now - lastAttempt) < 3600000UL))
        return;
      attempted = true;
      lastAttempt = now;
      if (!download.begin("stable", onZero, true)) {
        status = download.error;
        log();
        return;
      }
      status = "관리자 안정 버전 확인 중";
      phase = 1;
      log();
      return;
    }
    if (phase == 3) {
      if (archive.active())
        return;
      status = archive.phase == V5BundleUpdate::Phase::Complete
                   ? "SD 안정 버전 저장 완료" : archive.error;
      download.discard();
      download.error = "";
      download.checkedVersion = "";
      phase = 0;
      log();
      return;
    }
    if (download.active)
      return;
    if (phase == 1 && download.available) {
      File text = SD.open(download.directory + "/bundle.txt", FILE_READ);
      File sig = SD.open(download.directory + "/bundle.sig", FILE_READ);
      textSize = text ? text.size() : 0;
      sigSize = sig ? sig.size() : 0;
      if (!textSize || textSize > sizeof(designation) || !sigSize ||
          sigSize > sizeof(signature) || text.read(designation, textSize) != textSize ||
          sig.read(signature, sigSize) != sigSize) {
        fail(download, "안정 버전 지정 읽기 실패");
        return;
      }
      text.close();
      sig.close();
      version = download.checkedVersion;
      // Compare the signed descriptor, not the currently running app version.
      MilestoneV5::SignedBundleManifest selected{};
      bool current = MilestoneV5::decodeStableDesignation(designation, textSize, selected) &&
                     matches(archive.stablePath(false), selected.mainSha256) &&
                     (!selected.hasZero || matches(archive.stablePath(true), selected.zeroSha256));
      if (current) {
        status = "SD 안정 버전 최신";
        download.discard();
        download.error = "";
        download.checkedVersion = "";
        phase = 0;
        log();
        return;
      }
      download.discard();
      if (!download.begin(version, onZero)) {
        fail(download, download.error);
        return;
      }
      status = "지정된 안정 버전 다운로드 중";
      phase = 2;
      log();
      return;
    }
    if (phase == 2 && download.ready) {
      if (!write(download.directory + "/stable.txt", designation, textSize) ||
          !write(download.directory + "/stable.sig", signature, sigSize) ||
          !archive.start(download.directory, true)) {
        fail(download, archive.error.isEmpty() ? String("안정 버전 지정 저장 실패") : archive.error);
        return;
      }
      status = "SD 안정 버전 검증·저장 중";
      phase = 3;
      log();
      return;
    }
    fail(download, download.error.isEmpty() ? String("안정 버전 확인 실패") : download.error);
  }
private:
  uint8_t phase = 0;
  bool attempted = false;
  uint32_t lastAttempt = 0;
  uint8_t designation[255]{}, signature[512]{};
  size_t textSize = 0, sigSize = 0;
  static bool matches(const String &path, const uint8_t *hash) {
    File f = SD.open(path + "/manifest.txt", FILE_READ);
    File sig = SD.open(path + "/manifest.sig", FILE_READ);
    uint8_t text[255], signature[512];
    size_t n = f ? f.size() : 0, sn = sig ? sig.size() : 0;
    MilestoneV5::SignedImageManifest image{};
    if (!n || n > sizeof(text) || !sn || sn > sizeof(signature) ||
        f.read(text, n) != n || sig.read(signature, sn) != sn ||
        !MilestoneV5::verifyImageSignature(text, n, signature, sn) ||
        !MilestoneV5::decodeImageManifest(text, n, image) || memcmp(image.sha256, hash, 32))
      return false;
    f.close(); sig.close();
    File binary = SD.open(path + "/firmware.bin", FILE_READ);
    return binary && binary.size() == image.bytes;
  }
  static bool write(const String &path, const uint8_t *data, size_t size) {
    File f = SD.open(path, FILE_WRITE);
    if (!f || f.write(data, size) != size)
      return false;
    f.flush();
    f.close();
    return true;
  }
  void fail(V5BundleDownload &download, const String &why) {
    status = why;
    download.discard();
    download.error = "";
    download.checkedVersion = "";
    phase = 0;
    log();
  }
  void log() { Serial0.println(String("STABLE ") + version + " " + status); }
};
