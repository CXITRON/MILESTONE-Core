#include "../CoreDiagnostics.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; ++failures; } } while (0)
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_EQ(actual, expected) do { const auto a_ = (actual); const auto e_ = (expected); if (!(a_ == e_)) { std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #actual " == " #expected " (" << a_ << " != " << e_ << ")\n"; ++failures; } } while (0)

void testResetAndValidation() {
  MilestoneDiagnostics::History history;
  MilestoneDiagnostics::reset(history);
  EXPECT_TRUE(MilestoneDiagnostics::valid(history));
  EXPECT_EQ(history.count, 0);
  EXPECT_EQ(history.next, 0);

  history.records[0].value = 37;
  EXPECT_FALSE(MilestoneDiagnostics::valid(history));
}

void testPreNtpTimestamp() {
  MilestoneDiagnostics::History history;
  MilestoneDiagnostics::reset(history);
  MilestoneDiagnostics::Record record;
  record.epoch = 0;
  record.uptimeSec = 7;
  record.event = MilestoneDiagnostics::Event::BOOT;
  MilestoneDiagnostics::append(history, record);

  MilestoneDiagnostics::Record newestRecord;
  EXPECT_TRUE(MilestoneDiagnostics::newest(history, 0, newestRecord));
  EXPECT_EQ(newestRecord.epoch, 0U);
  EXPECT_EQ(newestRecord.uptimeSec, 7U);
  EXPECT_TRUE(newestRecord.event == MilestoneDiagnostics::Event::BOOT);
}

void testRingBufferOrdering() {
  MilestoneDiagnostics::History history;
  MilestoneDiagnostics::reset(history);

  for (uint32_t i = 0; i < 20; ++i) {
    MilestoneDiagnostics::Record record;
    record.uptimeSec = i;
    record.event = MilestoneDiagnostics::Event::BOOT;
    record.value = static_cast<int32_t>(i);
    MilestoneDiagnostics::append(history, record);
  }

  EXPECT_TRUE(MilestoneDiagnostics::valid(history));
  EXPECT_EQ(history.count, MilestoneDiagnostics::HISTORY_CAPACITY);
  EXPECT_EQ(history.sequence, 20U);

  MilestoneDiagnostics::Record record;
  EXPECT_TRUE(MilestoneDiagnostics::newest(history, 0, record));
  EXPECT_EQ(record.value, 19);
  EXPECT_TRUE(MilestoneDiagnostics::newest(history, 15, record));
  EXPECT_EQ(record.value, 4);
  EXPECT_FALSE(MilestoneDiagnostics::newest(history, 16, record));
}

void testCorruptionFallback() {
  MilestoneDiagnostics::History history;
  MilestoneDiagnostics::reset(history);
  MilestoneDiagnostics::Record record;
  record.event = MilestoneDiagnostics::Event::NTP_SUCCESS;
  MilestoneDiagnostics::append(history, record);
  EXPECT_TRUE(MilestoneDiagnostics::valid(history));

  history.checksum ^= 0x12345678UL;
  EXPECT_FALSE(MilestoneDiagnostics::valid(history));

  MilestoneDiagnostics::append(history, record);
  EXPECT_TRUE(MilestoneDiagnostics::valid(history));
  EXPECT_EQ(history.count, 1);
}

void testDuplicateSuppression() {
  using MilestoneDiagnostics::Event;
  MilestoneDiagnostics::SuppressionEntry entries[3];
  MilestoneDiagnostics::rememberDiagnosticWrite(Event::NTP_TIMEOUT, 0, 10000, entries, 3);
  EXPECT_TRUE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_TIMEOUT, 0, 15000, entries, 3, 60000));
  EXPECT_FALSE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_SUCCESS, 0, 15000, entries, 3, 60000));
  EXPECT_FALSE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_TIMEOUT, 1, 15000, entries, 3, 60000));
  EXPECT_FALSE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_TIMEOUT, 0, 70000, entries, 3, 60000));

  // Interleaved events must not defeat suppression of the original pair.
  MilestoneDiagnostics::rememberDiagnosticWrite(Event::NTP_SUCCESS, 0, 12000, entries, 3);
  EXPECT_TRUE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_TIMEOUT, 0, 15000, entries, 3, 60000));

  MilestoneDiagnostics::SuppressionEntry wrapped[1];
  MilestoneDiagnostics::rememberDiagnosticWrite(Event::NTP_TIMEOUT, 0, UINT32_MAX - 20, wrapped, 1);
  EXPECT_TRUE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_TIMEOUT, 0, 10, wrapped, 1, 60000));

  // A full table evicts the least recently written pair.
  MilestoneDiagnostics::rememberDiagnosticWrite(Event::WIFI_DISCONNECTED, 1, 13000, entries, 3);
  MilestoneDiagnostics::rememberDiagnosticWrite(Event::OTA_FAILED, 2, 80000, entries, 3);
  EXPECT_FALSE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::NTP_TIMEOUT, 0, 80001, entries, 3, 60000));
  EXPECT_TRUE(MilestoneDiagnostics::shouldSuppressDuplicate(
      Event::OTA_FAILED, 2, 80001, entries, 3, 60000));
}

void testEventSpecificSuppressionWindows() {
  using MilestoneDiagnostics::Event;
  EXPECT_EQ(MilestoneDiagnostics::duplicateSuppressionWindowMs(Event::BOOT), 60000U);
  EXPECT_EQ(MilestoneDiagnostics::duplicateSuppressionWindowMs(Event::WIFI_DISCONNECTED), 60000U);
  EXPECT_EQ(MilestoneDiagnostics::duplicateSuppressionWindowMs(Event::WIFI_CONNECT_TIMEOUT), 3600000U);
  EXPECT_EQ(MilestoneDiagnostics::duplicateSuppressionWindowMs(Event::NTP_TIMEOUT), 3600000U);
  EXPECT_EQ(MilestoneDiagnostics::duplicateSuppressionWindowMs(Event::UPDATE_CHECK_FAILED), 3600000U);
}

void testEventClassification() {
  EXPECT_TRUE(MilestoneDiagnostics::isError(MilestoneDiagnostics::Event::OTA_FAILED));
  EXPECT_TRUE(MilestoneDiagnostics::isError(MilestoneDiagnostics::Event::NTP_TIMEOUT));
  EXPECT_TRUE(MilestoneDiagnostics::isError(MilestoneDiagnostics::Event::MEDIA_FAILED));
  EXPECT_FALSE(MilestoneDiagnostics::isError(MilestoneDiagnostics::Event::NTP_SUCCESS));
  EXPECT_FALSE(MilestoneDiagnostics::isError(MilestoneDiagnostics::Event::BOOT_VALIDATED));
}

}  // namespace

int main() {
  testResetAndValidation();
  testPreNtpTimestamp();
  testRingBufferOrdering();
  testCorruptionFallback();
  testDuplicateSuppression();
  testEventSpecificSuppressionWindows();
  testEventClassification();

  if (failures != 0) {
    std::cerr << failures << " diagnostics test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All MILESTONE diagnostics tests passed\n";
  return EXIT_SUCCESS;
}
