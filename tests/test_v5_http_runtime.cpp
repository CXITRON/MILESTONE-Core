#include "../v5/libraries/MilestoneV5Core/src/MilestoneV5DownloadWorker.h"
#include <Arduino.h>
#include <cassert>
static void run(std::initializer_list<int> codes) {
  FakeHttp::requests.clear();
  FakeHttp::codes = codes;
  WiFi.connected = true;
  assert(V5DownloadWorker::start("https://github.com/CXITRON/MILESTONE-Core/"
                                 "releases/latest/download/v5-bundle.txt",
                                 8192));
  assert(FakeHttp::caConfigured);
}
int main() {
  using namespace V5DownloadWorker;
  run({-1, 503, 200});
  assert(state == 2 && FakeHttp::requests.size() == 3);
  assert(produced == 4096 && consumed == 0);
  uint8_t result[4096];
  assert(read(0, result, sizeof(result)) == sizeof(result));
  assert(!memcmp(result, FakeHttp::body.data(), sizeof(result)));
  run({404, 200});
  assert(state == 3 && FakeHttp::requests.size() == 1 && produced == 0);
  run({503, 503, 503, 200});
  assert(state == 3 && FakeHttp::requests.size() == 3);
  FakeHttp::location = "https://untrusted.invalid/image";
  run({302, 200});
  assert(state == 3 && FakeHttp::requests.size() == 1);
  FakeHttp::stopAfter = 1024;
  run({200, 200});
  assert(state == 3 && failure == kFailureStalled && produced == 1024);
  assert(FakeHttp::requests.size() == 1); // Never restart after a partial body.
  FakeHttp::stopAfter = 0;
  FakeHttp::onDelay = [] { cancel.store(true); };
  run({503, 200});
  assert(state == 3 && failure == kFailureCancelled &&
         FakeHttp::requests.size() == 1);
  heap_caps_free(ring);
  ring = nullptr;
  puts("v5 HTTPS pre-body retry, cancellation, CA/redirect and partial-body "
       "tests passed");
}
