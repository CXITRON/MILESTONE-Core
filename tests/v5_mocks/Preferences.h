#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <string.h>
namespace FakeNvs {static std::map<std::string,std::vector<uint8_t>> records;static bool writeFailure=false;}
class Preferences {
 public:
  bool begin(const char *,bool){return true;}
  void end(){}
  size_t getBytesLength(const char *key){return FakeNvs::records[key].size();}
  size_t putBytes(const char *key,const void *data,size_t n){if(FakeNvs::writeFailure)return 0;const uint8_t *p=static_cast<const uint8_t*>(data);FakeNvs::records[key]=std::vector<uint8_t>(p,p+n);return n;}
  size_t getBytes(const char *key,void *data,size_t n){auto &bytes=FakeNvs::records[key];if(bytes.size()>n)return 0;memcpy(data,bytes.data(),bytes.size());return bytes.size();}
};
