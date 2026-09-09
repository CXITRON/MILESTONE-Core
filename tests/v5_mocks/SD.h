#pragma once
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdint.h>
#include <string>
#include <type_traits>
using std::min;
constexpr const char *FILE_READ = "r", *FILE_WRITE = "w", *FILE_APPEND = "a";
class String {
public:
  String() = default;
  String(const char *s) : value(s ? s : "") {}
  String(const std::string &s) : value(s) {}
  template <class T,
            typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T n) : value(std::to_string(n)) {}
  const char *c_str() const { return value.c_str(); }
  size_t length() const { return value.length(); }
  bool isEmpty() const { return value.empty(); }
  char operator[](size_t i) const { return value[i]; }
  bool operator==(const String &other) const { return value == other.value; }
  bool operator!=(const String &other) const { return value != other.value; }
  String &operator+=(char c) {
    value += c;
    return *this;
  }
  bool startsWith(const String &s) const { return value.find(s.value) == 0; }
  int indexOf(char c) const {
    auto n = value.find(c);
    return n == std::string::npos ? -1 : int(n);
  }
  void toCharArray(char *out, size_t n) const {
    if (n) {
      size_t length = std::min(n - 1, value.size());
      value.copy(out, length);
      out[length] = 0;
    }
  }
  friend String operator+(const String &a, const String &b) {
    return a.value + b.value;
  }

private:
  std::string value;
};
namespace FakeSd {
static std::filesystem::path root;
static bool failIndexRename = false, failWrites = false;
inline std::filesystem::path path(const String &s) {
  return root / std::filesystem::path(s.c_str()).relative_path();
}
} // namespace FakeSd
class File {
  struct Handle {
    std::filesystem::path path;
    std::fstream stream;
    bool writable = false;
  };
  std::shared_ptr<Handle> h;

public:
  File() = default;
  File(const std::filesystem::path &p, const char *mode) {
    auto candidate = std::make_shared<Handle>();
    candidate->path = p;
    candidate->writable = mode[0] != 'r';
    auto flags = std::ios::binary;
    if (mode[0] == 'r')
      flags |= std::ios::in;
    else if (mode[0] == 'a')
      flags |= std::ios::out | std::ios::app;
    else
      flags |= std::ios::in | std::ios::out | std::ios::trunc;
    candidate->stream.open(p, flags);
    if (candidate->stream.is_open())
      h = candidate;
  }
  explicit operator bool() const { return h && h->stream.is_open(); }
  size_t size() const {
    std::error_code error;
    return h ? std::filesystem::file_size(h->path, error) : 0;
  }
  size_t read(uint8_t *p, size_t n) {
    if (!h)
      return 0;
    h->stream.read(reinterpret_cast<char *>(p), n);
    return h->stream.gcount();
  }
  size_t write(const uint8_t *p, size_t n) {
    if (!h || FakeSd::failWrites)
      return 0;
    h->stream.write(reinterpret_cast<const char *>(p), n);
    return h->stream ? n : 0;
  }
  void flush() {
    if (h)
      h->stream.flush();
  }
  void close() {
    if (h && h->stream.is_open())
      h->stream.close();
    h.reset();
  }
  bool seek(uint32_t offset) {
    if (!h)
      return false;
    h->stream.clear();
    if (h->writable)
      h->stream.seekp(offset);
    else
      h->stream.seekg(offset);
    return bool(h->stream);
  }
};
class FakeSdClass {
public:
  const char *mountpoint() { return FakeSd::root.c_str(); }
  bool mkdirFalseSuccess = false;
  File open(const String &s, const char *mode = FILE_READ) {
    return File(FakeSd::path(s), mode);
  }
  bool exists(const String &s) {
    return std::filesystem::exists(FakeSd::path(s));
  }
  bool mkdir(const String &s) {
    if (mkdirFalseSuccess)
      return true;
    std::error_code e;
    // Arduino VFS mkdir succeeds for an already-existing directory.
    if (std::filesystem::is_directory(FakeSd::path(s), e))
      return true;
    return std::filesystem::create_directory(FakeSd::path(s), e);
  }
  bool remove(const String &s) {
    std::error_code e;
    return std::filesystem::remove(FakeSd::path(s), e);
  }
  bool rmdir(const String &s) {
    std::error_code e;
    return std::filesystem::remove(FakeSd::path(s), e);
  }
  bool rename(const String &from, const String &to) {
    if (FakeSd::failIndexRename &&
        std::string(to.c_str()).find("/firmware/index-") == 0)
      return false;
    if (exists(to))
      return false;
    std::error_code e;
    std::filesystem::rename(FakeSd::path(from), FakeSd::path(to), e);
    return !e;
  }
  uint64_t totalBytes() const { return 8ULL * 1024 * 1024 * 1024; }
  uint64_t usedBytes() const { return 1024 * 1024; }
  uint64_t cardSize() const { return totalBytes(); }
};
static FakeSdClass SD;
