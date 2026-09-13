#pragma once
namespace FakeHttp {
static bool caConfigured = false;
}
struct NetworkClientSecure {
  void setCACert(const char *value) {
    FakeHttp::caConfigured = value && value[0];
  }
  void setHandshakeTimeout(unsigned) {}
};
