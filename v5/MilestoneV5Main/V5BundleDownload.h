#pragma once
#include <Arduino.h>
#include <MilestoneV5Bundle.h>
#include <MilestoneV5DownloadWorker.h>
#include <MilestoneV5Signature.h>
#include <MilestoneV5Video.h>

class V5BundleDownload {
public:
  bool active = false, ready = false, useZero = false;
  String directory, error;
  uint32_t received = 0, total = 0;
  bool begin(const String &version, bool onZero) {
    if (active || V5DownloadWorker::state.load() == 1 || !SD.cardSize())
      return false;
    // The release selector is a tag, never an arbitrary URL or filesystem path.
    bool valid = version == "latest";
    unsigned parts[3];
    int used = 0;
    if (!valid &&
        sscanf(version.c_str(), "%u.%u.%u%n", &parts[0], &parts[1], &parts[2],
               &used) == 3 &&
        used == int(version.length())) {
      char canonical[24];
      snprintf(canonical, sizeof(canonical), "%u.%u.%u", parts[0], parts[1],
               parts[2]);
      valid = version == canonical && parts[0] <= 65535 && parts[1] <= 65535 &&
              parts[2] <= 65535;
    }
    if (!valid) {
      error = "릴리스 버전이 올바르지 않습니다";
      return false;
    }
    if (!MILESTONE_V5_RELEASE_PUBLIC_KEY[0]) {
      error = "릴리스 공개 키가 설정되지 않았습니다";
      return false;
    }
    if (SD.totalBytes() < SD.usedBytes() + 10485760ULL + 1073741824ULL) {
      error = "SD 여유 공간이 부족합니다";
      return false;
    }
    base = "https://github.com/CXITRON/MILESTONE-Core/releases/" +
           (version == "latest" ? String("latest/download/")
                                : String("download/v") + version + "/");
    char name[64];
    snprintf(name, sizeof(name), "/firmware/download-%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    directory = name;
    if (SD.exists(directory) || !SD.mkdir(directory) ||
        !SD.mkdir(directory + "/main") || !SD.mkdir(directory + "/zero")) {
      error = "다운로드 폴더를 사용할 수 없습니다";
      cleanupDirectory();
      return false;
    }
    useZero = onZero;
    active = true;
    ready = false;
    error = "";
    index = 0;
    received = total = 0;
    started = millis();
    begun = false;
    id = esp_random();
    if (!id)
      id = 1;
    return true;
  }
  void stop(const char *why) {
    file.close();
    active = false;
    ready = false;
    error = why;
    V5DownloadWorker::cancel.store(true);
    if (hashActive) {
      mbedtls_sha256_free(&sha);
      hashActive = false;
    }
    cleanupDirectory();
  }
  void discard() {
    if (active)
      return;
    cleanupDirectory();
    ready = false;
    directory = "";
  }
  void service(bool localReady, bool healthy, bool zeroOnline) {
    if (!active)
      return;
    if (!healthy || millis() - started > 900000) {
      stop("안전 조건 또는 시간 초과로 다운로드 취소");
      return;
    }
    if (useZero) {
      if (begun && !zeroOnline && millis() - lastReply > 60000)
        stop("ZERO 다운로드 연결 끊김");
      return;
    }
    if (!localReady)
      return;
    if (!begun) {
      if (!openFile())
        return;
      if (!V5DownloadWorker::start(base + asset(), limit())) {
        stop("HTTPS 준비 조건을 충족하지 못함");
        return;
      }
      begun = true;
    }
    int state = V5DownloadWorker::state.load(std::memory_order_acquire);
    if (state == 3) {
      stop("HTTPS 다운로드 실패");
      return;
    }
    total = V5DownloadWorker::length.load(std::memory_order_acquire);
    uint8_t bytes[2048];
    size_t n = V5DownloadWorker::read(received, bytes, sizeof(bytes));
    if (n && !append(bytes, n))
      return;
    if (state == 2 && total && received == total)
      finishFile();
  }
  bool request(uint8_t *p, size_t &n) {
    if (!active || !useZero)
      return false;
    if (!begun) {
      if (!file && !openFile())
        return false;
      String url = base + asset();
      p[0] = 16;
      put(p + 1, id);
      put(p + 5, limit());
      memcpy(p + 9, url.c_str(), url.length());
      n = 9 + url.length();
    } else {
      p[0] = 17;
      put(p + 1, id);
      put(p + 5, received);
      n = 9;
    }
    return true;
  }
  bool response(const uint8_t *p, size_t n) {
    if (!active || !useZero || n < 14 ||
        MilestoneV5::readVideoU32(p + 1) != id || (p[0] != 16 && p[0] != 17))
      return false;
    lastReply = millis();
    if (p[5] == 2) {
      stop("ZERO HTTPS 요청 실패");
      return true;
    }
    if (p[0] == 16) {
      if (p[5] == 0)
        begun = true;
      return true;
    }
    if (MilestoneV5::readVideoU32(p + 6) != received) {
      stop("다운로드 위치 불일치");
      return true;
    }
    total = MilestoneV5::readVideoU32(p + 10);
    if (total > limit()) {
      stop("다운로드 크기가 매니페스트를 초과함");
      return true;
    }
    if (p[5] == 0 && n > 14)
      append(p + 14, n - 14);
    else if (p[5] == 3) {
      if (total && received == total)
        finishFile();
      else
        stop("다운로드가 완료되지 않음");
    }
    return true;
  }

private:
  String base;
  File file;
  uint8_t index = 0;
  uint32_t id = 0, started = 0, lastReply = 0;
  bool begun = false, hashActive = false;
  MilestoneV5::SignedBundleManifest bundle{};
  MilestoneV5::SignedImageManifest main{}, zero{};
  mbedtls_sha256_context sha;
  void cleanupDirectory() {
    if (!directory.startsWith("/firmware/download-"))
      return;
    for (unsigned i = 0; i < 8; ++i) {
      String path = directory + leafFor(i);
      if (SD.exists(path))
        SD.remove(path);
    }
    SD.rmdir(directory + "/main");
    SD.rmdir(directory + "/zero");
    SD.rmdir(directory);
  }
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (8 * i);
  }
  const char *asset() const {
    const char *v[] = {"v5-bundle.txt",        "v5-bundle.sig",
                       "v5-main-manifest.txt", "v5-main-manifest.sig",
                       "v5-zero-manifest.txt", "v5-zero-manifest.sig",
                       "v5-main.bin",          "v5-zero.bin"};
    return v[index];
  }
  const char *leaf() const { return leafFor(index); }
  static const char *leafFor(unsigned value) {
    const char *v[] = {"/bundle.txt",        "/bundle.sig",
                       "/main/manifest.txt", "/main/manifest.sig",
                       "/zero/manifest.txt", "/zero/manifest.sig",
                       "/main/firmware.bin", "/zero/firmware.bin"};
    return v[value];
  }
  uint32_t limit() const {
    return index == 6    ? main.bytes
           : index == 7  ? zero.bytes
           : (index & 1) ? 512
                         : 255;
  }
  bool openFile() {
    if (SD.exists(directory + leaf())) {
      stop("다운로드 대상이 이미 존재함");
      return false;
    }
    file = SD.open(directory + leaf(), FILE_WRITE);
    if (!file) {
      stop("SD에 다운로드를 기록할 수 없음");
      return false;
    }
    received = total = 0;
    mbedtls_sha256_init(&sha);
    hashActive = true;
    if (mbedtls_sha256_starts(&sha, 0)) {
      stop("SHA-256 초기화 실패");
      return false;
    }
    return true;
  }
  bool append(const uint8_t *p, size_t n) {
    if (n > limit() - received || file.write(p, n) != n ||
        mbedtls_sha256_update(&sha, p, n)) {
      stop("SD 다운로드 기록 실패");
      return false;
    }
    received += n;
    return true;
  }
  bool pair(const String &prefix, uint8_t *text, size_t &n) {
    File a = SD.open(prefix + ".txt", FILE_READ),
         b = SD.open(prefix + ".sig", FILE_READ);
    uint8_t sig[512];
    if (!a || !b || !a.size() || a.size() > 255 || !b.size() || b.size() > 512)
      return false;
    n = a.size();
    size_t sn = b.size();
    return a.read(text, n) == n && b.read(sig, sn) == sn &&
           MilestoneV5::verifyImageSignature(text, n, sig, sn);
  }
  void finishFile() {
    uint8_t digest[32];
    if (mbedtls_sha256_finish(&sha, digest)) {
      stop("다운로드 SHA-256 계산 실패");
      return;
    }
    mbedtls_sha256_free(&sha);
    hashActive = false;
    file.flush();
    file.close();
    uint8_t text[255];
    size_t n = 0;
    bool valid = true;
    if (index == 1)
      valid = pair(directory + "/bundle", text, n) &&
              MilestoneV5::decodeBundleManifest(text, n, bundle);
    if (index == 3 || index == 5) {
      auto &image = index == 3 ? main : zero;
      bool isZero = index == 5;
      valid = pair(directory + (isZero ? "/zero/manifest" : "/main/manifest"),
                   text, n) &&
              MilestoneV5::decodeImageManifest(text, n, image) &&
              image.target == (isZero ? MilestoneV5::ManifestTarget::Zero
                                      : MilestoneV5::ManifestTarget::Main) &&
              !memcmp(image.sha256,
                      isZero ? bundle.zeroSha256 : bundle.mainSha256, 32) &&
              image.minimumPeerProtocol <= 1 &&
              image.maximumPeerProtocol >= 1 &&
              image.bytes <= (isZero ? 1966080UL : 0x600000UL) &&
              image.major == bundle.major && image.minor == bundle.minor &&
              image.patch == bundle.patch;
    }
    if (index >= 6) {
      auto &image = index == 6 ? main : zero;
      valid = received == image.bytes && !memcmp(digest, image.sha256, 32);
    }
    if (!valid) {
      stop("서명·대상·해시 검증 실패");
      return;
    }
    ++index;
    if (index == 4 && !bundle.hasZero)
      index = 6;
    if (index == 8 || (index == 7 && !bundle.hasZero)) {
      active = false;
      ready = true;
      return;
    }
    if (++id == 0)
      ++id;
    begun = false;
    received = total = 0;
  }
};
