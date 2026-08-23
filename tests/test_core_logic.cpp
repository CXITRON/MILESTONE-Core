#include "../CoreLogic.h"

#include <algorithm>
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

void testLateBluetoothEncryptionFailure() {
  constexpr int timeoutStatus = 13;
  EXPECT_TRUE(MilestoneCoreLogic::shouldIgnoreLateBluetoothEncryptionFailure(
      true, timeoutStatus, timeoutStatus));
  EXPECT_FALSE(MilestoneCoreLogic::shouldIgnoreLateBluetoothEncryptionFailure(
      false, timeoutStatus, timeoutStatus));
  EXPECT_FALSE(MilestoneCoreLogic::shouldIgnoreLateBluetoothEncryptionFailure(
      true, 0, timeoutStatus));
  EXPECT_FALSE(MilestoneCoreLogic::shouldIgnoreLateBluetoothEncryptionFailure(
      true, 25, timeoutStatus));
}

void testMusicBrainzReleaseGroupParser() {
  MilestoneCoreLogic::MusicBrainzReleaseGroupParser parser;
  MilestoneCoreLogic::resetMusicBrainzReleaseGroupParser(parser);
  char mbid[37] = {};
  const char *xml = "<release-group-list><release-group ext:score=\"100\" type=\"Single\" "
                    "id=\"70664047-2545-4e46-b75f-4556f2a7b83e\"><title>x</title>";
  EXPECT_FALSE(MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(
      parser, reinterpret_cast<const uint8_t *>(xml), 31, mbid));
  EXPECT_TRUE(MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(
      parser, reinterpret_cast<const uint8_t *>(xml + 31), std::strlen(xml) - 31, mbid));
  EXPECT_TRUE(std::strcmp(mbid, "70664047-2545-4e46-b75f-4556f2a7b83e") == 0);

  MilestoneCoreLogic::resetMusicBrainzReleaseGroupParser(parser);
  const char *nested = "<recording id=\"x\"><release-group type='Album' "
                       "id='7678ff0a-9446-4d5f-b46e-56c84fc68654'></release-group>";
  EXPECT_TRUE(MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(
      parser, reinterpret_cast<const uint8_t *>(nested), std::strlen(nested), mbid));
  EXPECT_TRUE(std::strcmp(mbid, "7678ff0a-9446-4d5f-b46e-56c84fc68654") == 0);

  MilestoneCoreLogic::resetMusicBrainzReleaseGroupParser(parser);
  const char *invalid = "<release-group-list id=\"70664047-2545-4e46-b75f-4556f2a7b83e\">";
  EXPECT_FALSE(MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(
      parser, reinterpret_cast<const uint8_t *>(invalid), std::strlen(invalid), mbid));

  MilestoneCoreLogic::resetMusicBrainzReleaseGroupParser(parser);
  const char *multiple =
      "<release-group ext:score='100' id='70664047-2545-4e46-b75f-4556f2a7b83e'>"
      "</release-group><release-group type='Album' "
      "id='7678ff0a-9446-4d5f-b46e-56c84fc68654'></release-group>";
  const char *expected[] = {"70664047-2545-4e46-b75f-4556f2a7b83e",
                            "7678ff0a-9446-4d5f-b46e-56c84fc68654"};
  size_t found = 0;
  for (size_t i = 0; i < std::strlen(multiple); ++i) {
    if (MilestoneCoreLogic::feedMusicBrainzReleaseGroupParser(
            parser, reinterpret_cast<const uint8_t *>(multiple + i), 1, mbid)) {
      EXPECT_TRUE(found < 2U);
      if (found < 2U) EXPECT_TRUE(std::strcmp(mbid, expected[found]) == 0);
      ++found;
    }
  }
  EXPECT_EQ(found, 2U);
}

void testAppleArtworkUrlParser() {
  MilestoneCoreLogic::AppleArtworkUrlParser parser;
  MilestoneCoreLogic::resetAppleArtworkUrlParser(parser);
  const char *json = "{\"resultCount\":1,\"results\":[{\"artistName\":\"요네즈 켄시\","
                     "\"artworkUrl100\" : \"https:\\/\\/is1-ssl.mzstatic.com\\/cover.jpg\"}]}";
  bool found = false;
  for (size_t offset = 0; offset < std::strlen(json); offset += 7U) {
    const size_t take = std::min<size_t>(7U, std::strlen(json) - offset);
    if (MilestoneCoreLogic::feedAppleArtworkUrlParser(
            parser, reinterpret_cast<const uint8_t *>(json + offset), take)) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
  EXPECT_TRUE(std::strcmp(parser.value, "https://is1-ssl.mzstatic.com/cover.jpg") == 0);

  MilestoneCoreLogic::resetAppleArtworkUrlParser(parser);
  const char *empty = "{\"resultCount\":0,\"results\":[]}";
  EXPECT_FALSE(MilestoneCoreLogic::feedAppleArtworkUrlParser(
      parser, reinterpret_cast<const uint8_t *>(empty), std::strlen(empty)));
}

}  // namespace

int main() {
  testSemanticVersions();
  testDates();
  testCycleOrder();
  testSha256();
  testJsonEscapes();
  testUtf8HangulDetection();
  testLateBluetoothEncryptionFailure();
  testMusicBrainzReleaseGroupParser();
  testAppleArtworkUrlParser();

  if (failures != 0) {
    std::cerr << failures << " core logic test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All MILESTONE Core logic tests passed\n";
  return EXIT_SUCCESS;
}
