#include <cstdio>
#include <cstring>

#define PROGMEM
#include "PortalPage.h"

int main() {
  const size_t size = std::strlen(MILESTONE_PORTAL_HTML);
  return std::fwrite(MILESTONE_PORTAL_HTML, 1, size, stdout) == size ? 0 : 1;
}
