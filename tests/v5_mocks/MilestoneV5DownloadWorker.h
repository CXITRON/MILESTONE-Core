#pragma once
#include <SD.h>
#include <atomic>
#include <cstring>
#include <map>
#include <vector>
namespace V5DownloadWorker {
static std::atomic<int> state{0};
static std::atomic<bool> cancel{false};
static std::atomic<uint32_t> length{0};
static std::map<std::string, std::vector<uint8_t>> fixtures;
static std::vector<std::string> requests;
static std::vector<uint8_t> data;
static uint32_t consumed = 0;
inline bool start(const String &url, uint32_t maximum) {
  std::string path = url.c_str();
  std::string name = path.substr(path.find_last_of('/') + 1);
  requests.push_back(name);
  auto found = fixtures.find(name);
  consumed = 0;
  if (found == fixtures.end() || found->second.size() > maximum) {
    state = 3;
    return true;
  }
  data = found->second;
  length = data.size();
  state = 2;
  return true;
}
inline size_t read(uint32_t offset, uint8_t *out, size_t maximum) {
  if (offset != consumed || offset > data.size())
    return 0;
  size_t n = std::min(maximum, data.size() - offset);
  memcpy(out, data.data() + offset, n);
  consumed += n;
  return n;
}
} // namespace V5DownloadWorker
