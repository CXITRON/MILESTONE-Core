#pragma once

#include "V5Tft.h"
#include <MilestoneV5LegacyMedia.h>
#include <SD.h>
#include <esp_heap_caps.h>

class V5LegacyMedia {
public:
  static constexpr uint8_t kMaxItems = 64;
  static constexpr size_t kNameBytes = 49;
  static constexpr uint8_t kEnabled = 1, kAnimated = 2, kLoop = 4;
  struct Entry {
    uint32_t id = 0, size = 0, duration = 0;
    uint16_t frames = 0;
    uint8_t displaySeconds = 8, flags = kEnabled;
    char name[kNameBytes]{};
  };
  struct Catalog {
    uint32_t generation = 0;
    uint8_t count = 0;
    Entry entries[kMaxItems];
  };

  bool ready = false, frameReady = false, paused = false, failed = false,
       displayEnabled = true;
  Catalog catalog;
  uint8_t selected = 0xFF;
  String error;

  void begin(bool sdMounted) {
    ready = false;
    if (!sdMounted)
      return;
    if (!SD.exists("/media") && !SD.mkdir("/media")) {
      error = "미디어 폴더 생성 실패";
      return;
    }
    Catalog a, b;
    const bool av = readIndex("/media/index.a", a),
               bv = readIndex("/media/index.b", b);
    if (av || bv) {
      if (!av || (bv && int32_t(b.generation - a.generation) > 0)) {
        catalog = b;
        activeSlot = 'b';
      } else {
        catalog = a;
        activeSlot = 'a';
      }
    }
    ready = allocate();
    if (!ready)
      error = "미디어 버퍼 할당 실패";
  }

  size_t usedBytes() const {
    size_t total = 0;
    for (unsigned i = 0; i < catalog.count; ++i)
      total += catalog.entries[i].size;
    return total;
  }
  bool hasEnabled() const {
    for (unsigned i = 0; i < catalog.count; ++i)
      if (catalog.entries[i].flags & kEnabled)
        return true;
    return false;
  }
  String path(uint32_t id) const {
    char value[32];
    snprintf(value, sizeof(value), "/media/%08lx.msm",
             static_cast<unsigned long>(id));
    return value;
  }

  bool beginUpload(const String &name, size_t expected, uint8_t display) {
    abortUpload();
    if (!ready || name.isEmpty() || name.length() >= kNameBytes ||
        expected < MilestoneMedia::HEADER_BYTES ||
        expected > MilestoneMedia::MAX_FILE_BYTES || display < 3 ||
        display > 60 || catalog.count >= kMaxItems) {
      error = "업로드 입력값 오류";
      return false;
    }
    SD.remove("/media/upload.tmp");
    upload = SD.open("/media/upload.tmp", FILE_WRITE);
    if (!upload) {
      error = "임시 파일 생성 실패";
      return false;
    }
    uploadName = name;
    uploadExpected = expected;
    uploadDisplay = display;
    uploadWritten = 0;
    uploadActive = true;
    return true;
  }
  bool writeUpload(const uint8_t *data, size_t size) {
    if (!uploadActive || !upload || size > uploadExpected - uploadWritten ||
        upload.write(data, size) != size) {
      error = "업로드 기록 실패";
      abortUpload();
      return false;
    }
    uploadWritten += size;
    return true;
  }
  bool finishUpload(Entry &created) {
    if (!uploadActive || uploadWritten != uploadExpected) {
      error = "업로드 크기 불일치";
      abortUpload();
      return false;
    }
    upload.flush();
    upload.close();
    uploadActive = false;
    MilestoneMedia::Header header;
    MilestoneMedia::Error mediaError;
    if (!validateFile("/media/upload.tmp", header, mediaError)) {
      error = String("MSM1 검증 실패: ") + MilestoneMedia::errorName(mediaError);
      SD.remove("/media/upload.tmp");
      return false;
    }
    uint32_t id = esp_random();
    while (!id || SD.exists(path(id)))
      id = esp_random();
    const String target = path(id);
    if (!SD.rename("/media/upload.tmp", target)) {
      error = "미디어 파일 확정 실패";
      SD.remove("/media/upload.tmp");
      return false;
    }
    Catalog next = catalog;
    Entry &entry = next.entries[next.count++];
    entry.id = id;
    entry.size = uploadExpected;
    entry.duration = header.durationMs;
    entry.frames = header.frameCount;
    entry.displaySeconds = uploadDisplay;
    entry.flags = kEnabled | (header.frameCount > 1 ? kAnimated : 0) |
                  (header.flags & MilestoneMedia::FLAG_LOOP ? kLoop : 0);
    uploadName.toCharArray(entry.name, sizeof(entry.name));
    if (!commit(next)) {
      SD.remove(target);
      error = "미디어 목록 저장 실패";
      return false;
    }
    created = catalog.entries[catalog.count - 1];
    return true;
  }
  void abortUpload() {
    if (upload)
      upload.close();
    if (uploadActive)
      SD.remove("/media/upload.tmp");
    uploadActive = false;
    uploadWritten = uploadExpected = 0;
  }

  bool update(uint32_t id, const String &name, uint8_t display, bool enabled) {
    if (name.isEmpty() || name.length() >= kNameBytes || display < 3 ||
        display > 60)
      return false;
    Catalog next = catalog;
    for (unsigned i = 0; i < next.count; ++i) {
      if (next.entries[i].id != id)
        continue;
      name.toCharArray(next.entries[i].name, sizeof(next.entries[i].name));
      next.entries[i].displaySeconds = display;
      if (enabled)
        next.entries[i].flags |= kEnabled;
      else
        next.entries[i].flags &= ~kEnabled;
      closePlayback();
      return commit(next);
    }
    return false;
  }
  bool move(uint32_t id, bool up) {
    Catalog next = catalog;
    for (unsigned i = 0; i < next.count; ++i) {
      if (next.entries[i].id != id)
        continue;
      if ((up && i == 0) || (!up && i + 1 >= next.count))
        return true;
      const unsigned other = up ? i - 1 : i + 1;
      Entry temporary = next.entries[i];
      next.entries[i] = next.entries[other];
      next.entries[other] = temporary;
      closePlayback();
      return commit(next);
    }
    return false;
  }
  bool remove(uint32_t id) {
    Catalog next = catalog;
    for (unsigned i = 0; i < next.count; ++i) {
      if (next.entries[i].id != id)
        continue;
      const String target = path(id), backup = target + ".delete";
      SD.remove(backup);
      if (!SD.rename(target, backup))
        return false;
      for (unsigned j = i + 1; j < next.count; ++j)
        next.entries[j - 1] = next.entries[j];
      --next.count;
      closePlayback();
      if (!commit(next)) {
        SD.rename(backup, target);
        return false;
      }
      SD.remove(backup);
      return true;
    }
    return false;
  }
  bool clear() {
    closePlayback();
    Catalog empty;
    empty.generation = catalog.generation;
    if (!commit(empty))
      return false;
    File directory = SD.open("/media");
    if (directory) {
      for (File file = directory.openNextFile(); file;
           file = directory.openNextFile()) {
        String name = file.name();
        file.close();
        if (name.endsWith(".msm") || name.endsWith(".delete") ||
            name.endsWith("upload.tmp"))
          SD.remove(name.startsWith("/") ? name : String("/media/") + name);
      }
      directory.close();
    }
    return true;
  }
  bool repair() {
    closePlayback();
    SD.remove("/media/index.a");
    SD.remove("/media/index.b");
    SD.remove("/media/upload.tmp");
    catalog = Catalog();
    activeSlot = 0;
    return commit(catalog);
  }

  bool selectRelative(int direction) {
    displayEnabled = true;
    if (!hasEnabled()) {
      closePlayback();
      return false;
    }
    int start = selected == 0xFF ? (direction < 0 ? catalog.count : -1)
                                 : selected;
    for (unsigned step = 1; step <= catalog.count; ++step) {
      int index = (start + direction * int(step)) % int(catalog.count);
      if (index < 0)
        index += catalog.count;
      if ((catalog.entries[index].flags & kEnabled) && open(index))
        return true;
    }
    return false;
  }
  bool ensureSelected() {
    return displayEnabled &&
           (selected != 0xFF && playback ? true : selectRelative(1));
  }
  void toggle() { paused = !paused; }
  void closePlayback() {
    if (playback)
      playback.close();
    selected = 0xFF;
    frameReady = false;
    paused = false;
    frameIndex = 0;
  }
  void hide() {
    closePlayback();
    displayEnabled = false;
  }
  bool service(SimpleSt7735 &display, uint32_t now) {
    if (!ready || paused || !ensureSelected())
      return false;
    bool changed = false;
    if (frameReady) {
      displayFrame(display);
      frameReady = false;
      changed = true;
    }
    if (int32_t(now - nextFrameMs) < 0)
      return changed;
    if (frameIndex >= header.frameCount) {
      if (!(catalog.entries[selected].flags & kLoop)) {
        paused = true;
        return changed;
      }
      playback.seek(MilestoneMedia::HEADER_BYTES);
      frameIndex = 0;
    }
    uint16_t delay = 0;
    if (!readFrame(frameIndex == 0, delay)) {
      failed = true;
      error = "미디어 프레임 읽기 실패";
      closePlayback();
      return changed;
    }
    ++frameIndex;
    nextFrameMs = now + (delay < 100 ? 100 : delay);
    displayFrame(display);
    frameReady = false;
    return true;
  }

private:
  char activeSlot = 0;
  File upload, playback;
  bool uploadActive = false;
  size_t uploadExpected = 0, uploadWritten = 0;
  uint8_t uploadDisplay = 8;
  String uploadName;
  uint8_t *frame = nullptr, *record = nullptr;
  MilestoneMedia::Header header{};
  uint16_t frameIndex = 0;
  uint32_t nextFrameMs = 0;

  bool allocate() {
    if (!frame)
      frame = static_cast<uint8_t *>(heap_caps_malloc(
          MilestoneMedia::COLOR_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!record)
      record = static_cast<uint8_t *>(heap_caps_malloc(
          MilestoneMedia::COLOR_FRAME_BYTES + 261,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return frame && record;
  }
  static void serialize(const Entry &entry, uint8_t *out) {
    memset(out, 0, 65);
    MilestoneMedia::writeLe32(out, entry.id);
    MilestoneMedia::writeLe32(out + 4, entry.size);
    MilestoneMedia::writeLe32(out + 8, entry.duration);
    MilestoneMedia::writeLe16(out + 12, entry.frames);
    out[14] = entry.displaySeconds;
    out[15] = entry.flags;
    memcpy(out + 16, entry.name, kNameBytes);
  }
  static bool deserialize(const uint8_t *in, Entry &entry) {
    entry = Entry();
    entry.id = MilestoneMedia::readLe32(in);
    entry.size = MilestoneMedia::readLe32(in + 4);
    entry.duration = MilestoneMedia::readLe32(in + 8);
    entry.frames = MilestoneMedia::readLe16(in + 12);
    entry.displaySeconds = in[14];
    entry.flags = in[15];
    memcpy(entry.name, in + 16, kNameBytes);
    entry.name[kNameBytes - 1] = 0;
    return entry.id && entry.size >= MilestoneMedia::HEADER_BYTES &&
           entry.size <= MilestoneMedia::MAX_FILE_BYTES && entry.frames &&
           entry.frames <= MilestoneMedia::MAX_FRAMES &&
           entry.displaySeconds >= 3 && entry.displaySeconds <= 60 &&
           !(entry.flags & ~(kEnabled | kAnimated | kLoop)) && entry.name[0];
  }
  bool readIndex(const char *name, Catalog &out) {
    File file = SD.open(name, FILE_READ);
    if (!file)
      return false;
    const size_t size = file.size();
    if (size < 20 || size > 20 + kMaxItems * 65) {
      file.close();
      return false;
    }
    uint8_t *data = static_cast<uint8_t *>(malloc(size));
    const bool read = data && file.read(data, size) == size;
    file.close();
    if (!read || memcmp(data, "MSI1", 4) || data[4] != 1 ||
        data[5] > kMaxItems || size != 20 + size_t(data[5]) * 65 ||
        MilestoneMedia::readLe32(data + 12) != size_t(data[5]) * 65 ||
        MilestoneMedia::readLe32(data + 16) !=
            MilestoneMedia::crc32(data + 20, size - 20)) {
      free(data);
      return false;
    }
    out = Catalog();
    out.generation = MilestoneMedia::readLe32(data + 8);
    out.count = data[5];
    for (unsigned i = 0; i < out.count; ++i)
      if (!deserialize(data + 20 + i * 65, out.entries[i])) {
        free(data);
        return false;
      }
    free(data);
    return true;
  }
  bool writeIndex(const char *name, const Catalog &value) {
    const size_t size = 20 + size_t(value.count) * 65;
    uint8_t *data = static_cast<uint8_t *>(calloc(size, 1));
    if (!data)
      return false;
    memcpy(data, "MSI1", 4);
    data[4] = 1;
    data[5] = value.count;
    MilestoneMedia::writeLe32(data + 8, value.generation);
    MilestoneMedia::writeLe32(data + 12, size - 20);
    for (unsigned i = 0; i < value.count; ++i)
      serialize(value.entries[i], data + 20 + i * 65);
    MilestoneMedia::writeLe32(data + 16,
                              MilestoneMedia::crc32(data + 20, size - 20));
    SD.remove(name);
    File file = SD.open(name, FILE_WRITE);
    bool ok = file && file.write(data, size) == size;
    if (file) {
      file.flush();
      file.close();
    }
    free(data);
    Catalog verified;
    return ok && readIndex(name, verified) &&
           verified.generation == value.generation &&
           verified.count == value.count;
  }
  bool commit(Catalog next) {
    next.generation = catalog.generation + 1;
    const char slot = activeSlot == 'a' ? 'b' : 'a';
    const char *name = slot == 'a' ? "/media/index.a" : "/media/index.b";
    if (!writeIndex(name, next))
      return false;
    catalog = next;
    activeSlot = slot;
    return true;
  }
  bool validateFile(const String &name, MilestoneMedia::Header &parsed,
                    MilestoneMedia::Error &mediaError) {
    File file = SD.open(name, FILE_READ);
    if (!file)
      return false;
    const size_t size = file.size();
    uint8_t *data = size <= MilestoneMedia::MAX_FILE_BYTES
                        ? static_cast<uint8_t *>(heap_caps_malloc(
                              size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                        : nullptr;
    const bool read = data && file.read(data, size) == size;
    file.close();
    const bool valid = read &&
                       MilestoneMedia::validateFile(data, size, &parsed,
                                                    &mediaError);
    free(data);
    return valid;
  }
  bool open(uint8_t index) {
    closePlayback();
    if (!ready || index >= catalog.count || !allocate())
      return false;
    playback = SD.open(path(catalog.entries[index].id), FILE_READ);
    uint8_t first[MilestoneMedia::HEADER_BYTES];
    if (!playback || playback.read(first, sizeof(first)) != sizeof(first) ||
        !MilestoneMedia::parseHeader(first, catalog.entries[index].size, header) ||
        header.frameCount != catalog.entries[index].frames) {
      closePlayback();
      return false;
    }
    selected = index;
    uint16_t delay = 0;
    if (!readFrame(true, delay)) {
      closePlayback();
      return false;
    }
    frameIndex = 1;
    nextFrameMs = millis() + (delay < 100 ? 100 : delay);
    frameReady = true;
    return true;
  }
  bool readFrame(bool first, uint16_t &delay) {
    if (!playback || playback.available() < 5 || playback.read(record, 5) != 5)
      return false;
    const uint16_t dataSize = MilestoneMedia::readLe16(record + 3);
    if (dataSize > MilestoneMedia::frameBytes(header) + 256 ||
        playback.read(record + 5, dataSize) != dataSize)
      return false;
    size_t consumed = 0;
    return MilestoneMedia::decodeFrameSized(
               record, 5 + dataSize, first, frame,
               MilestoneMedia::frameBytes(header), delay, consumed) &&
           consumed == 5 + dataSize;
  }
  void displayFrame(SimpleSt7735 &display) {
    if (header.color)
      display.blitRgb332(frame, 16);
    else
      display.blitMonoPacked(frame, 16, 0xFFFF, 0x0000);
  }
};
