#pragma once
#ifndef MILESTONE_V5_HARDWARE_DECLARED
#include "V5Hardware.h"
#endif
#include "V5SdUpdate.h"
#include "V5ZeroUpdate.h"
#include <MilestoneV5Bundle.h>
#include <MilestoneV5OtaWire.h>
#include <MilestoneV5Signature.h>
#include <MilestoneV5Stable.h>
#include <Preferences.h>

// Immutable SD sets + a CRC-protected NVS restart journal. Stable/Backup are
// published through an A/B SD index only for an authenticated administrator
// designation, after every copied asset has passed readback verification.
// Recovery directories are never written by this coordinator.
class V5BundleUpdate {
public:
  enum class Phase {
    Idle,
    Copying,
    InstallingMain,
    CheckingRunning,
    AwaitMainApproval,
    NeedZero,
    UpdatingZero,
    StabilityHold,
    Complete,
    Failed
  };
  Phase phase = Phase::Idle;
  String error;
  uint32_t progress = 0, total = 0;
  bool candidateReady = true, candidateRejected = false;
  bool critical() const {
    return phase == Phase::Copying || phase == Phase::InstallingMain ||
           phase == Phase::CheckingRunning || phase == Phase::UpdatingZero;
  }
  bool active() const {
    return phase != Phase::Idle && phase != Phase::Complete &&
           phase != Phase::Failed;
  }
  bool installing() const {
    return !archiveOnly && active() && phase != Phase::StabilityHold;
  }
  void beginBoot(V5Hardware &h, V5SdUpdate &main, V5ZeroUpdate &zero) {
    hardware = &h;
    mainUpdate = &main;
    zeroUpdate = &zero;
    loadIndex();
    Preferences prefs;
    uint8_t bytes[MilestoneV5::kBundleJournalSize];
    bool saved =
        prefs.begin("v5_bundle", true) &&
        prefs.getBytesLength("journal") == sizeof(bytes) &&
        prefs.getBytes("journal", bytes, sizeof(bytes)) == sizeof(bytes);
    prefs.end();
    if (!saved ||
        !MilestoneV5::decodeBundleJournal(bytes, sizeof(bytes), journal))
      return;
    if (journal.stage == J::Complete) {
      phase = Phase::Complete;
      return;
    }
    if (journal.stage == J::Failed) {
      phase = Phase::Failed;
      error = "이전 업데이트 묶음이 완료되지 않음";
      return;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->address != journal.mainAddress ||
        journal.mainBytes > running->size) {
      fail("MAIN 롤백됨; ZERO는 유지됨");
      return;
    }
    runningPartition = running;
    phase = Phase::CheckingRunning;
    candidateReady = false;
    progress = 0;
    total = journal.mainBytes;
    startHash();
  }
  bool start(const String &source = "/firmware/incoming", bool stableArchive = false) {
    if (active() || !hardware || !hardware->sdMounted)
      return false;
    cleanup();
    archiveOnly = stableArchive;
    sourceRoot = source;
    journal = {};
    error = "";
    candidateReady = true;
    candidateRejected = false;
    progress = total = 0;
    uint8_t text[255], signature[512];
    size_t n, sn;
    if (!readPair(sourceRoot + "/bundle.txt", sourceRoot + "/bundle.sig", text,
                  n, signature, sn) ||
        !MilestoneV5::decodeBundleManifest(text, n, bundle) ||
        !MilestoneV5::verifyImageSignature(text, n, signature, sn))
      return fail("업데이트 묶음 서명 검증 실패");
    if (archiveOnly) {
      uint8_t designation[255], sig[512];
      size_t dn, sn;
      MilestoneV5::SignedBundleManifest expected{};
      if (!readPair(sourceRoot + "/stable.txt", sourceRoot + "/stable.sig",
                    designation, dn, sig, sn) ||
          !MilestoneV5::verifyImageSignature(designation, dn, sig, sn) ||
          !MilestoneV5::decodeStableDesignation(designation, dn, expected) ||
          !MilestoneV5::sameBundle(expected, bundle))
        return fail("관리자 안정 버전 지정 검증 실패");
    }
    sizes[0] = n;
    sizes[1] = sn;
    mbedtls_sha256(text, n, hashes[0], 0);
    mbedtls_sha256(signature, sn, hashes[1], 0);
    if (!preflightAsset(false, bundle.mainSha256))
      return false;
    if (bundle.hasZero && !preflightAsset(true, bundle.zeroSha256))
      return false;
    count = bundle.hasZero ? 8 : 5;
    for (unsigned i = 0; i < count; ++i)
      total += sizes[i];
    if (SD.totalBytes() <
        SD.usedBytes() + uint64_t(total) + 1024ULL * 1024 * 1024)
      return fail("SD 여유 공간 부족");
    if (!SD.exists("/firmware/sets") && !SD.mkdir("/firmware/sets"))
      return fail("펌웨어 묶음 저장소 사용 불가");
    journal = {};
    journal.stage = J::MainPending;
    journal.hasZero = bundle.hasZero;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
      snprintf(journal.setId, sizeof(journal.setId), "%08lx%08lx",
               (unsigned long)esp_random(), (unsigned long)esp_random());
      if (!SD.exists(setRoot()) && SD.mkdir(setRoot()))
        break;
      if (attempt == 3)
        return fail("고유 펌웨어 묶음을 만들 수 없음");
    }
    if (!SD.mkdir(setRoot() + "/main") ||
        (bundle.hasZero && !SD.mkdir(setRoot() + "/zero")))
      return fail("펌웨어 묶음 폴더 생성 실패");
    const esp_partition_t *destination =
        esp_ota_get_next_update_partition(nullptr);
    if (!destination || sizes[4] > destination->size)
      return fail("MAIN이 비활성 슬롯 크기를 초과함");
    journal.mainAddress = destination->address;
    journal.mainBytes = sizes[4];
    memcpy(journal.mainSha256, bundle.mainSha256, 32);
    memcpy(journal.zeroSha256, bundle.zeroSha256, 32);
    // Persist intent before the first SD copy. A reset at any staging point is
    // then diagnosed as an incomplete MAIN transaction and can never start
    // ZERO or promote Stable silently.
    if (!archiveOnly && !saveJournal())
      return fail("묶음 준비 기록 저장 실패");
    fileIndex = 0;
    fileOffset = 0;
    readback = false;
    phase = Phase::Copying;
    return true;
  }
  void service(uint32_t now, bool healthy, bool mainAccepted,
               bool companionHealthy) {
    if (!active())
      return;
    if (!healthy) {
      if (phase == Phase::StabilityHold) {
        holdStarted = now;
        return;
      }
      if (!archiveOnly) {
        mainUpdate->cancel();
        zeroUpdate->cancel();
      }
      fail("기기 안전 조건으로 묶음 작업 중단");
      return;
    }
    if (phase == Phase::Copying) {
      copyStep();
      return;
    }
    if (phase == Phase::InstallingMain) {
      if (mainUpdate->state == V5SdUpdate::State::Failed)
        fail(mainUpdate->error.c_str());
      return;
    }
    if (phase == Phase::CheckingRunning) {
      uint8_t buffer[2048];
      size_t take = min(uint32_t(sizeof(buffer)), total - progress);
      if (take) {
        if (esp_partition_read(runningPartition, progress, buffer, take) !=
                ESP_OK ||
            mbedtls_sha256_update(&sha, buffer, take)) {
          rejectCandidate("Running MAIN read failed");
          return;
        }
        progress += take;
        return;
      }
      uint8_t hash[32];
      if (mbedtls_sha256_finish(&sha, hash) ||
          memcmp(hash, journal.mainSha256, 32)) {
        rejectCandidate("Running MAIN hash mismatch");
        return;
      }
      cleanup();
      candidateReady = true;
      phase = Phase::AwaitMainApproval;
      return;
    }
    if (phase == Phase::AwaitMainApproval) {
      if (!mainAccepted)
        return;
      if (journal.stage == J::MainPending) {
        journal.stage = J::MainVerified;
        if (!saveJournal()) {
          fail("MAIN 승인 기록 저장 실패");
          return;
        }
      }
      if (journal.stage == J::ZeroPending ||
          journal.stage == J::PromotePending) {
        if (journal.hasZero) {
          zeroUpdate->resumeReceipt(journal.zeroTransfer);
          phase = Phase::UpdatingZero;
        } else
          beginHold(now);
      } else
        phase = Phase::NeedZero;
      return;
    }
    if (phase == Phase::NeedZero) {
      if (!hardware->sdMounted) {
        error = "ZERO 업데이트 완료를 위해 SD를 삽입하세요";
        return;
      }
      if (!journal.hasZero) {
        beginHold(now);
        return;
      }
      String path = setRoot() + "/zero";
      if (!zeroUpdate->begin(path.c_str(), journal.zeroSha256)) {
        fail(zeroUpdate->error);
        return;
      }
      journal.stage = J::ZeroPending;
      journal.zeroTransfer = zeroUpdate->id;
      if (!saveJournal()) {
        zeroUpdate->cancel();
        fail("ZERO 전송 기록 저장 실패");
        return;
      }
      phase = Phase::UpdatingZero;
      return;
    }
    if (phase == Phase::UpdatingZero) {
      if (zeroUpdate->state == V5ZeroUpdate::State::Failed) {
        fail("ZERO 확인 실패; MAIN은 유지됨");
        return;
      }
      if (zeroUpdate->state == V5ZeroUpdate::State::Done)
        beginHold(now);
      return;
    }
    if (phase == Phase::StabilityHold && journal.hasZero && !companionHealthy) {
      holdStarted = now;
      return;
    }
    if (phase == Phase::StabilityHold && now - holdStarted >= 600000 &&
        now - lastPromotionTry >= 60000) {
      lastPromotionTry = now;
      // Successful installation is not an administrator's stable designation.
      // Keep the curated SD index intact; only archiveOnly can publish it.
      journal.stage = J::Complete;
      if (!saveJournal()) {
        error = "설치 완료; 기록 마무리 대기 중";
        return;
      }
      phase = Phase::Complete;
      error = "";
    }
  }
  String stablePath(bool zero) const {
    const char *id = zero ? index.zeroStable : index.mainStable;
    if (indexKnown && id[0])
      return String("/firmware/sets/") + id + (zero ? "/zero" : "/main");
    return zero ? "/firmware/zero/stable" : "/firmware/main/stable";
  }

private:
  bool archiveOnly = false;
  using J = MilestoneV5::BundleStage;
  V5Hardware *hardware = nullptr;
  V5SdUpdate *mainUpdate = nullptr;
  V5ZeroUpdate *zeroUpdate = nullptr;
  MilestoneV5::SignedBundleManifest bundle{};
  MilestoneV5::BundleJournal journal{};
  MilestoneV5::StableIndex index{};
  bool indexKnown = false, indexB = false, readback = false, shaActive = false;
  uint8_t fileIndex = 0, count = 0;
  uint32_t fileOffset = 0, holdStarted = 0, lastPromotionTry = 0;
  uint32_t sizes[8]{};
  uint8_t hashes[8][32]{};
  File input, output;
  mbedtls_sha256_context sha;
  String sourceRoot = "/firmware/incoming";
  const esp_partition_t *runningPartition = nullptr;
  String setRoot() const { return String("/firmware/sets/") + journal.setId; }
  static const char *leaf(unsigned index) {
    const char *paths[] = {"/bundle.txt",        "/bundle.sig",
                           "/main/manifest.txt", "/main/manifest.sig",
                           "/main/firmware.bin", "/zero/manifest.txt",
                           "/zero/manifest.sig", "/zero/firmware.bin"};
    return paths[index];
  }
  bool readPair(const String &textPath, const String &signaturePath,
                uint8_t *text, size_t &n, uint8_t *signature, size_t &sn) {
    File a = SD.open(textPath, FILE_READ),
         b = SD.open(signaturePath, FILE_READ);
    if (!a || !b || !a.size() || a.size() > 255 || !b.size() || b.size() > 512)
      return false;
    n = a.size();
    sn = b.size();
    return a.read(text, n) == n && b.read(signature, sn) == sn;
  }
  bool preflightAsset(bool zero, const uint8_t *expected) {
    uint8_t text[255], sig[512];
    size_t n, sn;
    String base = sourceRoot + (zero ? "/zero" : "/main");
    MilestoneV5::SignedImageManifest m{};
    if (!readPair(base + "/manifest.txt", base + "/manifest.sig", text, n, sig,
                  sn) ||
        !MilestoneV5::decodeImageManifest(text, n, m) ||
        m.target != (zero ? MilestoneV5::ManifestTarget::Zero
                          : MilestoneV5::ManifestTarget::Main) ||
        memcmp(m.sha256, expected, 32) ||
        MilestoneV5::kProtocolVersion < m.minimumPeerProtocol ||
        MilestoneV5::kProtocolVersion > m.maximumPeerProtocol ||
        !MilestoneV5::verifyImageSignature(text, n, sig, sn))
      return fail("묶음 자산 매니페스트 검증 실패");
    if (m.major != bundle.major || m.minor != bundle.minor ||
        m.patch != bundle.patch)
      return fail(zero ? "Bundle ZERO version mismatch"
                       : "Bundle MAIN version mismatch");
    if (!zero) {
      const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
      if (!slot || m.bytes > slot->size)
        return fail("MAIN이 비활성 슬롯 크기를 초과함");
    }
    if (zero && m.bytes > 1966080)
      return fail("ZERO가 고정 OTA 슬롯 크기를 초과함");
    File bin = SD.open(base + "/firmware.bin", FILE_READ);
    if (!bin || bin.size() != m.bytes)
      return fail("묶음 자산 크기 불일치");
    unsigned first = zero ? 5 : 2;
    sizes[first] = n;
    sizes[first + 1] = sn;
    sizes[first + 2] = m.bytes;
    mbedtls_sha256(text, n, hashes[first], 0);
    mbedtls_sha256(sig, sn, hashes[first + 1], 0);
    memcpy(hashes[first + 2], expected, 32);
    return true;
  }
  void startHash() {
    if (shaActive)
      mbedtls_sha256_free(&sha);
    mbedtls_sha256_init(&sha);
    shaActive = true;
    mbedtls_sha256_starts(&sha, 0);
  }
  void cleanup() {
    input.close();
    output.close();
    if (shaActive) {
      mbedtls_sha256_free(&sha);
      shaActive = false;
    }
  }
  bool saveJournal() {
    uint8_t bytes[MilestoneV5::kBundleJournalSize],
        check[MilestoneV5::kBundleJournalSize];
    if (!MilestoneV5::encodeBundleJournal(journal, bytes, sizeof(bytes)))
      return false;
    Preferences p;
    bool ok = p.begin("v5_bundle", false) &&
              p.putBytes("journal", bytes, sizeof(bytes)) == sizeof(bytes) &&
              p.getBytes("journal", check, sizeof(check)) == sizeof(check) &&
              !memcmp(check, bytes, sizeof(bytes));
    p.end();
    return ok;
  }
  bool fail(const char *message) {
    error = message;
    cleanup();
    phase = Phase::Failed;
    if (!archiveOnly && MilestoneV5::validFirmwareSetId(journal.setId)) {
      journal.stage = J::Failed;
      saveJournal();
    }
    return false;
  }
  void rejectCandidate(const char *message) {
    candidateRejected = true;
    candidateReady = false;
    fail(message);
  }
  void copyStep() {
    if (fileIndex >= count) {
      if (archiveOnly) {
        if (!promoteIndex()) {
          fail("안정 버전 인덱스 저장 실패; 이전 안정본 유지");
          return;
        }
        phase = Phase::Complete;
        error = "";
        return; // SD-only: never select or write an application partition.
      }
      if (!saveJournal()) {
        fail("묶음 재시작 기록 저장 실패");
        return;
      }
      String path = setRoot() + "/main";
      if (!mainUpdate->begin(path.c_str(), journal.mainSha256)) {
        fail(mainUpdate->error.c_str());
        return;
      }
      phase = Phase::InstallingMain;
      return;
    }
    if (!input) {
      input = SD.open(sourceRoot + leaf(fileIndex), FILE_READ);
      output = SD.open(setRoot() + leaf(fileIndex), FILE_WRITE);
      if (!input || !output || input.size() != sizes[fileIndex]) {
        fail("묶음 복사 파일 열기 실패");
        return;
      }
      fileOffset = 0;
      readback = false;
      startHash();
    }
    uint8_t buffer[2048];
    size_t take = min(uint32_t(sizeof(buffer)), sizes[fileIndex] - fileOffset);
    if (take) {
      if (input.read(buffer, take) != take ||
          mbedtls_sha256_update(&sha, buffer, take) ||
          (!readback && output.write(buffer, take) != take)) {
        fail("묶음 SD 복사 실패");
        return;
      }
      fileOffset += take;
      if (!readback)
        progress += take;
      return;
    }
    uint8_t hash[32];
    if (mbedtls_sha256_finish(&sha, hash) ||
        memcmp(hash, hashes[fileIndex], 32)) {
      fail("묶음 SD SHA-256 불일치");
      return;
    }
    if (!readback) {
      output.flush();
      output.close();
      input.close();
      input = SD.open(setRoot() + leaf(fileIndex), FILE_READ);
      if (!input || input.size() != sizes[fileIndex]) {
        fail("묶음 재확인 읽기 실패");
        return;
      }
      fileOffset = 0;
      readback = true;
      startHash();
    } else {
      cleanup();
      ++fileIndex;
    }
  }
  void beginHold(uint32_t now) {
    journal.stage = J::PromotePending;
    if (!saveJournal()) {
      fail("안정본 대기 기록 저장 실패");
      return;
    }
    holdStarted = now;
    phase = Phase::StabilityHold;
  }
  bool readIndex(const char *path, MilestoneV5::StableIndex &value) {
    File f = SD.open(path, FILE_READ);
    uint8_t bytes[MilestoneV5::kStableIndexSize];
    return f && f.size() == sizeof(bytes) &&
           f.read(bytes, sizeof(bytes)) == sizeof(bytes) &&
           MilestoneV5::decodeStableIndex(bytes, sizeof(bytes), value);
  }
  void loadIndex() {
    MilestoneV5::StableIndex a{}, b{};
    bool av = readIndex("/firmware/index-a", a),
         bv = readIndex("/firmware/index-b", b);
    indexKnown = av || bv;
    if (!indexKnown)
      return;
    indexB = !av || (bv && int32_t(b.generation - a.generation) > 0);
    index = indexB ? b : a;
  }
  bool promoteIndex() {
    loadIndex();
    if (indexKnown && !strcmp(index.mainStable, journal.setId) &&
        (!journal.hasZero || !strcmp(index.zeroStable, journal.setId)))
      return true;
    MilestoneV5::StableIndex next =
        indexKnown ? index : MilestoneV5::StableIndex{};
    ++next.generation;
    memcpy(next.mainBackup, next.mainStable, 17);
    memcpy(next.mainStable, journal.setId, 17);
    if (journal.hasZero) {
      memcpy(next.zeroBackup, next.zeroStable, 17);
      memcpy(next.zeroStable, journal.setId, 17);
    }
    uint8_t bytes[MilestoneV5::kStableIndexSize],
        check[MilestoneV5::kStableIndexSize];
    if (!MilestoneV5::encodeStableIndex(next, bytes, sizeof(bytes)))
      return false;
    const char *temp = "/firmware/index.tmp",
               *target = indexB ? "/firmware/index-a" : "/firmware/index-b";
    if (SD.exists(temp) && !SD.remove(temp))
      return false;
    File f = SD.open(temp, FILE_WRITE);
    bool ok = f && f.write(bytes, sizeof(bytes)) == sizeof(bytes);
    f.flush();
    f.close();
    f = SD.open(temp, FILE_READ);
    ok = ok && f && f.size() == sizeof(check) &&
         f.read(check, sizeof(check)) == sizeof(check) &&
         !memcmp(bytes, check, sizeof(bytes));
    f.close();
    if (!ok || (SD.exists(target) && !SD.remove(target)) ||
        !SD.rename(temp, target))
      return false;
    index = next;
    indexB = !indexB;
    indexKnown = true;
    return true;
  }
};
