#pragma once
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <functional>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0, pdPASS = 1;
namespace FakeHttp {
static std::vector<int> codes;
static std::vector<std::string> requests;
static std::vector<uint8_t> body(4096, 42);
static String location = "https://release-assets.githubusercontent.com/image";
static size_t offset = 0, stopAfter = 0;
static std::function<void()> onDelay;
} // namespace FakeHttp
inline void vTaskDelay(unsigned ticks) {
  FakeOta::now += ticks;
  if (FakeHttp::onDelay)
    FakeHttp::onDelay();
}
inline void vTaskDelete(void *) {}
inline int xTaskCreatePinnedToCore(void (*run)(void *), const char *, unsigned,
                                   void *, unsigned, void *, int) {
  run(nullptr);
  return pdPASS;
}
struct FakeStream {
  int available() {
    if (FakeHttp::stopAfter && FakeHttp::offset >= FakeHttp::stopAfter)
      return 0;
    return FakeHttp::body.size() - FakeHttp::offset;
  }
  int read(uint8_t *out, size_t n) {
    if (FakeHttp::stopAfter)
      n = min(n, FakeHttp::stopAfter - FakeHttp::offset);
    memcpy(out, FakeHttp::body.data() + FakeHttp::offset, n);
    FakeHttp::offset += n;
    return n;
  }
};
class HTTPClient {
  FakeStream stream;

public:
  void setConnectTimeout(unsigned) {}
  void setTimeout(unsigned) {}
  void setReuse(bool) {}
  void setFollowRedirects(int) {}
  void collectHeaders(const char **, unsigned) {}
  bool begin(NetworkClientSecure &, const String &url) {
    FakeHttp::requests.emplace_back(url.c_str());
    FakeHttp::offset = 0;
    return true;
  }
  void addHeader(const char *, const char *) {}
  int GET() {
    if (FakeHttp::codes.empty())
      return -1;
    int result = FakeHttp::codes.front();
    FakeHttp::codes.erase(FakeHttp::codes.begin());
    return result;
  }
  int getSize() { return FakeHttp::body.size(); }
  String header(const char *) { return FakeHttp::location; }
  FakeStream *getStreamPtr() { return &stream; }
  bool connected() {
    return !(FakeHttp::stopAfter && FakeHttp::offset >= FakeHttp::stopAfter);
  }
  void end() {}
};
