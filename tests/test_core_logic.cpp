#include "../CoreLogic.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

using MilestoneCoreLogic::CivilDate;

namespace {

int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; ++failures; } } while (0)
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_EQ(actual, expected) do { const auto a_ = (actual); const auto e_ = (expected); if (!(a_ == e_)) { std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #actual " == " #expected " (" << a_ << " != " << e_ << ")\n"; ++failures; } } while (0)

void testSemanticVersions() {
  uint16_t parts[3] = {};
  EXPECT_TRUE(MilestoneCoreLogic::parseSemanticVersion("1.8.1", parts));
  EXPECT_EQ(parts[0], 1);
  EXPECT_EQ(parts[1], 8);
  EXPECT_EQ(parts[2], 1);
  EXPECT_TRUE(MilestoneCoreLogic::parseSemanticVersion("0001.0008.0001", parts));
  EXPECT_FALSE(MilestoneCoreLogic::parseSemanticVersion("1.8", parts));
  EXPECT_FALSE(MilestoneCoreLogic::parseSemanticVersion("1.8.1.0", parts));
  EXPECT_FALSE(MilestoneCoreLogic::parseSemanticVersion("1.8.x", parts));
  EXPECT_FALSE(MilestoneCoreLogic::parseSemanticVersion("65536.0.0", parts));
  EXPECT_EQ(MilestoneCoreLogic::compareSemanticVersions("1.8.0", "1.8.1"), -1);
  EXPECT_EQ(MilestoneCoreLogic::compareSemanticVersions("1.8.1", "1.8.1"), 0);
  EXPECT_EQ(MilestoneCoreLogic::compareSemanticVersions("2.0.0", "1.99.99"), 1);
}

void testDates() {
  CivilDate date;
  EXPECT_TRUE(MilestoneCoreLogic::parseIsoDate("2028-02-29", date));
  EXPECT_EQ(date.year, 2028);
  EXPECT_EQ(date.month, 2);
  EXPECT_EQ(date.day, 29);
  EXPECT_FALSE(MilestoneCoreLogic::parseIsoDate("2027-02-29", date));
  EXPECT_FALSE(MilestoneCoreLogic::parseIsoDate("2028-13-01", date));
  EXPECT_FALSE(MilestoneCoreLogic::parseIsoDate("2028-00-01", date));
  EXPECT_FALSE(MilestoneCoreLogic::parseIsoDate("2023-12-31", date));
  EXPECT_FALSE(MilestoneCoreLogic::parseIsoDate("2100-01-01", date));
  EXPECT_FALSE(MilestoneCoreLogic::parseIsoDate("2028/02/29", date));

  EXPECT_EQ(MilestoneCoreLogic::daysBetween({2028, 2, 28}, {2028, 2, 29}), 1);
  EXPECT_EQ(MilestoneCoreLogic::daysBetween({2028, 2, 29}, {2028, 3, 1}), 1);
  EXPECT_EQ(MilestoneCoreLogic::daysBetween({2027, 12, 31}, {2028, 1, 1}), 1);
  EXPECT_EQ(MilestoneCoreLogic::daysBetween({2028, 1, 1}, {2027, 12, 31}), -1);
  EXPECT_EQ(MilestoneCoreLogic::daysBetween({2028, 8, 10}, {2028, 8, 10}), 0);
}

void testCycleOrder() {
  uint8_t values[7] = {};
  EXPECT_TRUE(MilestoneCoreLogic::parseCycleOrder("0,1,2,3,4,5,6", values, 7));
  for (uint8_t i = 0; i < 7; ++i) EXPECT_EQ(values[i], i);
  EXPECT_TRUE(MilestoneCoreLogic::parseCycleOrder(" 3, 2 ,1,0 ", values, 4));
  EXPECT_EQ(values[0], 3);
  EXPECT_EQ(values[3], 0);
  EXPECT_FALSE(MilestoneCoreLogic::parseCycleOrder("0,1,2,3,4,5,5", values, 7));
  EXPECT_FALSE(MilestoneCoreLogic::parseCycleOrder("0,1,2,3,4,5", values, 7));
  EXPECT_FALSE(MilestoneCoreLogic::parseCycleOrder("0,1,2,3,4,5,6,", values, 7));
  EXPECT_FALSE(MilestoneCoreLogic::parseCycleOrder("0,1,2,3,4,5,7", values, 7));
  EXPECT_FALSE(MilestoneCoreLogic::parseCycleOrder("0,1,2,3,4,5, 6x", values, 7));
}

void testSha256() {
  EXPECT_TRUE(MilestoneCoreLogic::validSha256("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  EXPECT_TRUE(MilestoneCoreLogic::validSha256("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"));
  EXPECT_FALSE(MilestoneCoreLogic::validSha256("0123"));
  EXPECT_FALSE(MilestoneCoreLogic::validSha256("g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
}

void testJsonEscapes() {
  char decoded = 0;
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('b', decoded));
  EXPECT_EQ(static_cast<int>(decoded), static_cast<int>('\b'));
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('f', decoded));
  EXPECT_EQ(static_cast<int>(decoded), static_cast<int>('\f'));
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('n', decoded));
  EXPECT_EQ(static_cast<int>(decoded), static_cast<int>('\n'));
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('r', decoded));
  EXPECT_EQ(static_cast<int>(decoded), static_cast<int>('\r'));
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('t', decoded));
  EXPECT_EQ(static_cast<int>(decoded), static_cast<int>('\t'));
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('"', decoded));
  EXPECT_EQ(decoded, '"');
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('\\', decoded));
  EXPECT_EQ(decoded, '\\');
  EXPECT_TRUE(MilestoneCoreLogic::decodeJsonEscape('/', decoded));
  EXPECT_EQ(decoded, '/');
  EXPECT_FALSE(MilestoneCoreLogic::decodeJsonEscape('u', decoded));
}

void testUtf8HangulDetection() {
  EXPECT_TRUE(MilestoneCoreLogic::utf8ContainsHangul("지금 재생 중"));
  EXPECT_TRUE(MilestoneCoreLogic::utf8ContainsHangul("한日 mix"));
  EXPECT_FALSE(MilestoneCoreLogic::utf8ContainsHangul("今夜は最高"));
  EXPECT_FALSE(MilestoneCoreLogic::utf8ContainsHangul("カタカナ / ひらがな"));
  EXPECT_FALSE(MilestoneCoreLogic::utf8ContainsHangul("Now Playing"));
  EXPECT_FALSE(MilestoneCoreLogic::utf8ContainsHangul("\xE3\x81"));
}

}  // namespace

int main() {
  testSemanticVersions();
  testDates();
  testCycleOrder();
  testSha256();
  testJsonEscapes();
  testUtf8HangulDetection();

  if (failures != 0) {
    std::cerr << failures << " core logic test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All MILESTONE Core logic tests passed\n";
  return EXIT_SUCCESS;
}
