#pragma once
#include <Arduino.h>
#include <MilestoneV5Bundle.h>
#include <MilestoneV5DownloadWorker.h>
#include <MilestoneV5Signature.h>
#include <MilestoneV5Video.h>
#include <MilestoneV5Version.h>
#include <MilestoneV5Stable.h>
#include <errno.h>
#include <sys/stat.h>

class V5BundleDownload {
public:
  bool active = false, ready = false, useZero = false, upToDate = false,
       available = false;
  String directory, error, checkedVersion;
  uint32_t received = 0, total = 0;
  bool begin(const String &version, bool onZero, bool onlyCheck = false) {
    if (active) {
      error = "업데이트 확인이 이미 진행 중입니다";
      return false;
    }
    ready = false;
    available = false;
    checkOnly = onlyCheck;
    upToDate = false;
    checkedVersion = "";
    error = "";
    if (V5DownloadWorker::state.load() == 1) {
      error = "HTTPS 작업기가 아직 사용 중입니다";
      return false;
    }
    if (!SD.cardSize()) {
      error = "SD 카드를 사용할 수 없습니다";
      return false;
    }
    // The release selector is a tag, never an arbitrary URL or filesystem path.
    bool valid = version == "latest" || version == "stable";
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
    if (!onlyCheck && SD.totalBytes() < SD.usedBytes() + 10485760ULL + 1073741824ULL) {
      error = "SD 여유 공간이 부족합니다";
      return false;
    }
    base = "https://github.com/CXITRON/MILESTONE-Core/releases/" +
           (version == "latest" ? String("latest/download/")
            : version == "stable" ? String("download/stable/")
                                : String("download/v") + version + "/");
    stableSelector = version == "stable";
    char name[64];
    // FAT 8.3-compatible staging name: some mounted cards fail to resolve a
    // newly-created long name even when mkdir reports success.
    snprintf(name, sizeof(name), "/firmware/dl%06lx",
             (unsigned long)(esp_random() & 0xFFFFFFUL));
    directory = name;
    // Mount can succeed even when initial directory creation failed. Retry the
    // parent explicitly, without formatting or deleting any existing content.
    if (!makeDirectory("/firmware"))
      return false;
    if (SD.exists(directory)) {
      error = "다운로드 폴더 이름 충돌: 다시 확인해 주세요";
      log(String("OTA directory collision: ") + directory);
      directory = ""; // Never clean up a directory this attempt did not create.
      return false;
    }
    if (!makeDirectory(directory)) {
      directory = "";
      return false;
    }
    if (!makeDirectory(directory + "/main") ||
        !makeDirectory(directory + "/zero")) {
      cleanupDirectory();
      return false;
    }
    useZero = onZero;
    active = true;
    ready = false;
    upToDate = false;
    error = "";
    checkedVersion = "";
    latestSelector = version == "latest";
    index = 0;
    received = total = 0;
    started = millis();
    stageStarted = started;
    begun = false;
    firstZeroRequestMs = 0;
    lastReply = 0;
    id = esp_random();
    if (!id)
      id = 1;
    log(String("OTA transport begin: ") + (useZero ? "ZERO" : "MAIN") +
        " selector=" + version);
    return true;
  }
  void stop(const char *why) {
    file.close();
    active = false;
    ready = false;
    available = false;
    upToDate = false;
    error = why;
    log(String("OTA transport failed: ") + why);
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
    available = false;
    upToDate = false;
    directory = "";
  }
  void reject(const char *why) {
    if (active)
      stop(why);
    else {
      ready = false;
      available = false;
      upToDate = false;
      error = why;
    }
  }
  void service(bool localReady, bool healthy, bool zeroOnline) {
    if (!active)
      return;
    if (!healthy || millis() - started > 900000) {
      stop("안전 조건 또는 시간 초과로 다운로드 취소");
      return;
    }
    if (useZero) {
      const uint32_t now = millis();
      if (!begun && now - stageStarted > kZeroStartReplyTimeoutMs) {
        stop("ZERO 다운로드 시작 응답 시간 초과");
        return;
      }
      if (begun && !zeroOnline && lastReply &&
          now - lastReply > kZeroLinkTimeoutMs)
        stop("ZERO 다운로드 연결 끊김");
      else if (begun && now - stageStarted > kStageProgressTimeoutMs)
        stop("ZERO 다운로드 단계 진행 시간 초과");
      return;
    }
    if (!localReady) {
      if (!begun && millis() - stageStarted > kLocalPreparationTimeoutMs)
        stop("MAIN Wi-Fi 또는 NTP 준비 시간 초과");
      return;
    }
    if (!begun) {
      if (!openFile())
        return;
      if (!V5DownloadWorker::start(base + asset(), limit())) {
        stop(V5DownloadWorker::failureText());
        return;
      }
      begun = true;
    }
    int state = V5DownloadWorker::state.load(std::memory_order_acquire);
    if (state == 3) {
      stop(V5DownloadWorker::failureText());
      return;
    }
    total = V5DownloadWorker::length.load(std::memory_order_acquire);
    uint8_t bytes[2048];
    size_t n = V5DownloadWorker::read(received, bytes, sizeof(bytes));
    if (n && !append(bytes, n))
      return;
    if (n)
      stageStarted = millis();
    if (state == 2 && total && received == total)
      finishFile();
  }
  bool request(uint8_t *p, size_t &n) {
    if (!active || !useZero)
      return false;
    if (!begun) {
      if (!file && !openFile())
        return false;
      if (!firstZeroRequestMs)
        firstZeroRequestMs = millis();
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
      stop(n >= 15 ? V5DownloadWorker::failureText(p[14])
                   : "ZERO HTTPS 요청 실패");
      return true;
    }
    if (p[0] == 16) {
      if (p[5] == 0) {
        begun = true;
        firstZeroRequestMs = 0;
      }
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
    if (p[5] == 0 && n > 14) {
      if (append(p + 14, n - 14))
        stageStarted = millis();
    }
    else if (p[5] == 3) {
      if (total && received == total)
        finishFile();
      else
        stop("다운로드가 완료되지 않음");
    }
    return true;
  }

private:
  bool checkOnly = false;
  bool stableSelector = false;
  static constexpr uint32_t kZeroStartReplyTimeoutMs = 15000;
  static constexpr uint32_t kZeroLinkTimeoutMs = 15000;
  static constexpr uint32_t kLocalPreparationTimeoutMs = 90000;
  static constexpr uint32_t kStageProgressTimeoutMs = 180000;
  String base;
  File file;
  uint8_t index = 0;
  uint32_t id = 0, started = 0, stageStarted = 0, lastReply = 0,
           firstZeroRequestMs = 0;
  bool begun = false, hashActive = false, latestSelector = false;
  MilestoneV5::SignedBundleManifest bundle{};
  MilestoneV5::SignedImageManifest main{}, zero{};
  mbedtls_sha256_context sha;
  static void log(const String &message) {
    Serial.println(message);
    Serial0.println(message);
  }
  bool makeDirectory(const String &path) {
    errno = 0;
    const bool reported = SD.mkdir(path);
    const int mkdirError = errno;
    const char *mount = SD.mountpoint();
    const String absolute = String(mount ? mount : "") + path;
    struct stat status{};
    if (mount && ::stat(absolute.c_str(), &status) == 0 &&
        S_ISDIR(status.st_mode))
      return true;
    // The Arduino wrapper can report success without a resolvable FAT
    // directory. Verify its postcondition before creating children, then use
    // the mounted VFS mkdir directly. All SD operations remain on loopTask.
    log(String("OTA directory verify: path=") + path +
        " reported=" + String(reported ? 1 : 0) +
        " mkdir_errno=" + String(mkdirError) + " stat_errno=" + String(errno));
    if (mount && ::mkdir(absolute.c_str(), 0775) == 0 &&
        ::stat(absolute.c_str(), &status) == 0 && S_ISDIR(status.st_mode)) {
      log(String("OTA directory recovered: ") + path);
      return true;
    }
    const int failure = errno;
    error = String("다운로드 폴더 생성 실패: ") + path +
            " (errno=" + String(failure) + ")";
    log(String("OTA directory failure: path=") + path +
        " errno=" + String(failure));
    return false;
  }
  void cleanupDirectory() {
    bool shortStaging = directory.startsWith("/firmware/dl") &&
                        directory.length() == 18;
    for (unsigned i = 12; shortStaging && i < 18; ++i)
      shortStaging = (directory[i] >= '0' && directory[i] <= '9') ||
                     (directory[i] >= 'a' && directory[i] <= 'f');
    if (!shortStaging && !directory.startsWith("/firmware/download-"))
      return;
    for (unsigned i = 0; i < 8; ++i) {
      String path = directory + leafFor(i);
      if (SD.exists(path))
        SD.remove(path);
    }
    SD.remove(directory + "/stable.txt");
    SD.remove(directory + "/stable.sig");
    SD.rmdir(directory + "/main");
    SD.rmdir(directory + "/zero");
    SD.rmdir(directory);
  }
  static void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (8 * i);
  }
  const char *asset() const {
    if (stableSelector && index < 2)
      return index ? "v5-stable.sig" : "v5-stable.txt";
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
  static bool newerThanRunning(const MilestoneV5::SignedBundleManifest &value) {
    unsigned major = 0, minor = 0, patch = 0;
    int used = 0;
    if (sscanf(MilestoneV5::FIRMWARE_VERSION, "%u.%u.%u%n", &major, &minor,
               &patch, &used) != 3 ||
        used != int(strlen(MilestoneV5::FIRMWARE_VERSION)))
      return true;
    if (value.major != major)
      return value.major > major;
    if (value.minor != minor)
      return value.minor > minor;
    return value.patch > patch;
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
    if (index == 1) {
      valid = pair(directory + "/bundle", text, n) &&
              (stableSelector ? MilestoneV5::decodeStableDesignation(text, n, bundle)
                              : MilestoneV5::decodeBundleManifest(text, n, bundle));
      if (valid) {
        checkedVersion = String(bundle.major) + "." + String(bundle.minor) +
                         "." + String(bundle.patch);
        // Bind every remaining asset to the authenticated version. A new
        // GitHub latest release during transfer must not mix bundle versions.
        base = "https://github.com/CXITRON/MILESTONE-Core/releases/download/v" +
               checkedVersion + "/";
        if (latestSelector && !newerThanRunning(bundle)) {
          active = false;
          ready = false;
          upToDate = true;
          error = "";
          cleanupDirectory();
          directory = "";
          log(String("OTA signed catalog current: ") + checkedVersion);
          return;
        }
        if (checkOnly) {
          active = false;
          available = true;
          log(String("OTA signed check available: ") + checkedVersion);
          return; // No manifests or firmware binaries until physical OK.
        }
      }
    }
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
      log(String("OTA signed bundle verified: ") + checkedVersion);
      return;
    }
    if (++id == 0)
      ++id;
    begun = false;
    stageStarted = millis();
    firstZeroRequestMs = 0;
    lastReply = 0;
    received = total = 0;
    log(String("OTA next asset: ") + asset());
  }
};
