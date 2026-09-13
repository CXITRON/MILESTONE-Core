#pragma once
#include <sys/time.h>
namespace FakeNtp {
static unsigned starts = 0, stops = 0;
}
inline void esp_sntp_stop() { ++FakeNtp::stops; }
inline void esp_sntp_set_time_sync_notification_cb(void (*)(struct timeval *)) {
}
inline void configTime(int, int, const char *, const char *, const char *) {
  ++FakeNtp::starts;
}
