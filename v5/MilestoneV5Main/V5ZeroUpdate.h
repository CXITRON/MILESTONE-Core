#pragma once
#include <MilestoneV5OtaWire.h>
#include <MilestoneV5Signature.h>

// Explicit local SD recovery of ZERO. Full automatic bundle orchestration is
// separate; this path never installs or rolls back MAIN.
class V5ZeroUpdate {
public:
  enum class State {
    Idle,
    Hashing,
    Begin,
    Metadata,
    AwaitWriting,
    Image,
    Finish,
    Commit,
    RebootWait,
    Done,
    Failed
  };
  State state = State::Idle;
  const char *error = "";
  uint32_t received = 0, size = 0, id = 0;
  bool busy() const {
    return state != State::Idle && state != State::Done &&
           state != State::Failed;
  }
  bool begin(const char *directory, const uint8_t *requiredSha = nullptr) {
    if (busy())
      return false;
    cleanup();
    error = "";
    received = 0;
    state = State::Idle;
    String base(directory);
    File text = SD.open(base + "/manifest.txt", FILE_READ),
         signature = SD.open(base + "/manifest.sig", FILE_READ);
    if (!text || !signature || text.size() < 90 || text.size() > 255 ||
        !signature.size() || signature.size() > 512)
      return fail("ZERO 매니페스트 없음");
    manifestLength = text.size();
    signatureLength = signature.size();
    if (text.read(metadata, manifestLength) != manifestLength ||
        signature.read(metadata + manifestLength, signatureLength) !=
            signatureLength)
      return fail("ZERO 매니페스트 읽기 실패");
    text.close();
    signature.close();
    MilestoneV5::SignedImageManifest manifest{};
    if (!MilestoneV5::decodeImageManifest(metadata, manifestLength, manifest) ||
        manifest.target != MilestoneV5::ManifestTarget::Zero ||
        MilestoneV5::kProtocolVersion < manifest.minimumPeerProtocol ||
        MilestoneV5::kProtocolVersion > manifest.maximumPeerProtocol ||
        !MilestoneV5::verifyImageSignature(metadata, manifestLength,
                                           metadata + manifestLength,
                                           signatureLength))
      return fail("ZERO 서명 검증 실패");
    if (requiredSha && memcmp(requiredSha, manifest.sha256, 32))
      return fail("묶음 ZERO 해시 불일치");
    memcpy(expected, manifest.sha256, 32);
    size = manifest.bytes;
    file = SD.open(base + "/firmware.bin", FILE_READ);
    if (!file || file.size() != size)
      return fail("ZERO 파일 크기 불일치");
    mbedtls_sha256_init(&sha);
    shaActive = true;
    if (mbedtls_sha256_starts(&sha, 0))
      return fail("ZERO 해시 초기화 실패");
    id = esp_random();
    if (!id)
      id = 1;
    started = lastActivity = millis();
    state = State::Hashing;
    return true;
  }
  void cancel() {
    if (busy())
      fail("ZERO 복구 취소됨");
  }
  void resumeReceipt(uint32_t transfer) {
    cleanup();
    id = transfer;
    size = received = 0;
    started = lastActivity = lastPoll = rebootWaitStarted = millis();
    error = "";
    state = transfer ? State::RebootWait : State::Failed;
  }
  void service(uint32_t now, bool safe) {
    if (!busy())
      return;
    if (!safe)
      return void(fail("ZERO 복구 취소됨"));
    if (state == State::RebootWait && now - rebootWaitStarted > 90000)
      return void(fail("ZERO 부팅 확인 실패"));
    if (now - started > 600000 || now - lastActivity > 90000)
      return void(fail("ZERO 복구 시간 초과"));
    if (state != State::Hashing)
      return;
    uint8_t buffer[2048];
    size_t take = min(uint32_t(sizeof(buffer)), size - received);
    if (take) {
      if (file.read(buffer, take) != take ||
          mbedtls_sha256_update(&sha, buffer, take))
        return void(fail("ZERO SD 읽기 실패"));
      received += take;
      return;
    }
    uint8_t hash[32];
    if (mbedtls_sha256_finish(&sha, hash) || memcmp(hash, expected, 32))
      return void(fail("ZERO SHA-256 불일치"));
    mbedtls_sha256_free(&sha);
    shaActive = false;
    received = 0;
    state = State::Begin;
  }
  bool request(uint8_t *p, size_t &n) {
    if (!busy() || state == State::Hashing)
      return false;
    uint8_t op = 0;
    n = 5;
    switch (state) {
    case State::Begin:
    case State::AwaitWriting:
      op = 1;
      n = 9;
      p[5] = manifestLength;
      p[6] = manifestLength >> 8;
      p[7] = signatureLength;
      p[8] = signatureLength >> 8;
      break;
    case State::Metadata: {
      op = 2;
      size_t take =
          min(size_t(460), size_t(manifestLength + signatureLength - received));
      MilestoneV5::otaPut32(p + 5, received);
      memcpy(p + 9, metadata + received, take);
      n = 9 + take;
      break;
    }
    case State::Image: {
      op = 3;
      size_t take = min(uint32_t(460), size - received);
      if (!take) {
        state = State::Finish;
        return request(p, n);
      }
      MilestoneV5::otaPut32(p + 5, received);
      if (!file.seek(received) || file.read(p + 9, take) != take) {
        fail("ZERO 이미지 읽기 실패");
        return false;
      }
      n = 9 + take;
      break;
    }
    case State::Finish:
      op = 4;
      break;
    case State::Commit:
      op = 5;
      break;
    case State::RebootWait:
      if (millis() - lastPoll < 1000)
        return false;
      lastPoll = millis();
      op = 6;
      break;
    default:
      return false;
    }
    p[0] = op;
    MilestoneV5::otaPut32(p + 1, id);
    sentOp = op;
    sentLength = n >= 9 ? uint32_t(n - 9) : 0;
    return true;
  }
  bool response(const uint8_t *p, size_t n) {
    if (!busy() || n != 10 || p[0] != sentOp ||
        MilestoneV5::otaU32(p + 1) != id ||
        p[5] > uint8_t(MilestoneV5::RemoteOtaState::BootTesting))
      return false;
    using R = MilestoneV5::RemoteOtaState;
    R remote = static_cast<R>(p[5]);
    uint32_t offset = MilestoneV5::otaU32(p + 6);
    lastActivity = millis();
    if (state == State::RebootWait) {
      if (remote == R::Complete) {
        cleanup();
        state = State::Done;
      }
      return true;
    }
    if (remote == R::Failed) {
      fail("ZERO가 복구를 거부함");
      return true;
    }
    if (state == State::Begin && remote == R::Manifest) {
      if (offset > manifestLength + signatureLength)
        return false;
      received = offset;
      state = State::Metadata;
    } else if (state == State::Metadata) {
      if (remote == R::Manifest) {
        if (offset != received + sentLength)
          return false;
        received = offset;
      } else if (remote == R::Quiescing) {
        state = State::AwaitWriting;
        received = 0;
      } else if (remote == R::Writing) {
        state = State::Image;
        received = 0;
      } else
        return false;
    } else if (state == State::AwaitWriting) {
      if (remote == R::Writing) {
        state = State::Image;
        received = 0;
      } else if (remote != R::Quiescing)
        return false;
    } else if (state == State::Image && remote == R::Writing) {
      if (offset != received + sentLength || offset > size)
        return false;
      received = offset;
      if (received == size)
        state = State::Finish;
    } else if (state == State::Finish && remote == R::Ready)
      state = State::Commit;
    else if (state == State::Commit && remote == R::Rebooting) {
      state = State::RebootWait;
      lastPoll = rebootWaitStarted = millis();
    } else
      return false;
    return true;
  }

private:
  File file;
  uint8_t metadata[767]{}, expected[32]{};
  uint16_t manifestLength = 0, signatureLength = 0;
  uint8_t sentOp = 0;
  uint32_t sentLength = 0, started = 0, lastActivity = 0, lastPoll = 0,
           rebootWaitStarted = 0;
  bool shaActive = false;
  mbedtls_sha256_context sha;
  void cleanup() {
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
