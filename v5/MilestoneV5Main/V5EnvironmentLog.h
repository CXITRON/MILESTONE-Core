#pragma once
#include <MilestoneV5LogRecord.h>
#include <MilestoneV5Video.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

// A bounded write-ahead row survives reset between append and acknowledgement.
// Unexpected tail data is never truncated: logging stops for explicit
// inspection.
class V5EnvironmentLog {
public:
  uint32_t errors = 0;
  bool recover() {
    if (!SD.exists(pending))
      return true;
    uint8_t record[MilestoneV5::kLogRecordSize];
    File journal = SD.open(pending, FILE_READ);
    bool valid = journal && journal.size() == sizeof(record) &&
                 journal.read(record, sizeof(record)) == sizeof(record) &&
                 MilestoneV5::validLogRecord(record, sizeof(record));
    journal.close();
    if (!valid)
      return fail();
    char path[40];
    snprintf(path, sizeof(path), "/logs/env-%04u-%02u-%02u.csv",
             unsigned(record[4]) | unsigned(record[5]) << 8, record[6],
             record[7]);
    uint32_t offset = MilestoneV5::readVideoU32(record + 8);
    size_t length = record[12];
    File file = SD.open(path, FILE_READ);
    uint64_t present = file ? file.size() : 0;
    if (present < offset || present > uint64_t(offset) + length) {
      file.close();
      return fail();
    }
    size_t suffix = present - offset;
    uint8_t check[127];
    if (suffix && (!file.seek(offset) || file.read(check, suffix) != suffix ||
                   memcmp(check, record + 13, suffix))) {
      file.close();
      return fail();
    }
    file.close();
    if (suffix < length) {
      file = SD.open(path, FILE_APPEND);
      if (!file || file.size() != present ||
          file.write(record + 13 + suffix, length - suffix) !=
              length - suffix) {
        file.close();
        return fail();
      }
      file.flush();
      file.close();
    }
    file = SD.open(path, FILE_READ);
    valid = file && file.size() == uint64_t(offset) + length &&
            file.seek(offset) && file.read(check, length) == length &&
            !memcmp(check, record + 13, length);
    file.close();
    if (!valid || !SD.remove(pending))
      return fail();
    return true;
  }
  bool append(unsigned year, unsigned month, unsigned day, const char *row,
              size_t length) {
    if (!recover())
      return false;
    char path[40];
    snprintf(path, sizeof(path), "/logs/env-%04u-%02u-%02u.csv", year, month,
             day);
    File file = SD.open(path, FILE_READ);
    uint64_t offset = file ? file.size() : 0;
    file.close();
    if (offset > UINT32_MAX || SD.totalBytes() < SD.usedBytes() + 1024 * 1024)
      return fail();
    uint8_t record[MilestoneV5::kLogRecordSize],
        check[MilestoneV5::kLogRecordSize];
    if (!MilestoneV5::encodeLogRecord(year, month, day, offset, row, length,
                                      record, sizeof(record)))
      return fail();
    if (SD.exists(temporary) && !SD.remove(temporary))
      return fail();
    file = SD.open(temporary, FILE_WRITE);
    bool ok = file && file.write(record, sizeof(record)) == sizeof(record);
    file.flush();
    file.close();
    file = SD.open(temporary, FILE_READ);
    ok = ok && file && file.size() == sizeof(record) &&
         file.read(check, sizeof(check)) == sizeof(check) &&
         !memcmp(check, record, sizeof(record));
    file.close();
    if (!ok || !SD.rename(temporary, pending))
      return fail();
    return recover();
  }

private:
  const char *pending = "/logs/environment.pending",
             *temporary = "/logs/environment.tmp";
  bool fail() {
    ++errors;
    return false;
  }
};
