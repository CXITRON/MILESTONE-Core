// Exercise the actual ZERO scheduler; only radio/clock calls are test doubles.
#include "../v5/MilestoneV5Zero/V5Network.h"
#include <cassert>
int main() {
  using namespace V5Network;
  begin();
  service(100000, true);
  assert(WiFi.attempts == 0 && FakeNtp::starts == 0);
  service(100001, false);
  assert(connecting && WiFi.attempts == 1);
  service(100002, true);
  assert(!connecting && WiFi.disconnects == 1);
  WiFi.connected = true;
  service(200000, true);
  assert(FakeNtp::starts == 0);
  service(200001, false);
  assert(syncing && FakeNtp::starts == 1);
  service(200002, true);
  assert(!syncing && FakeNtp::stops == 1);
  assert(WiFi.connected); // Keep an idle association; stop new Internet work.
  assert(requestTime(42, 200003, true) == 2);
  service(200004, false);
  assert(syncing && manualStarted && FakeNtp::starts == 2);
  timeReceived.store(true);
  service(200005, false);
  assert(!syncing && requestTime(42, 200006, true) == 0);
  assert(FakeNtp::starts == 2); // Retrying the same request is idempotent.
  assert(requestTime(43, 210000, true) == 2);
  service(210001, false);
  service(231002, false);
  assert(manualResult == 1 && !syncing); // Bounded NTP timeout.
  assert(requestTime(44, 300000, true) == 2);
  service(345000, true);
  assert(manualResult == 1); // Busy BLE cannot leave a request pending forever.
  uint8_t candidateBytes[] = {7};
  FakeOta::now = 400000;
  assert(provision(candidateBytes, sizeof(candidateBytes)) == 2);
  const unsigned previousAttempts = WiFi.attempts;
  service(415000, true);
  assert(!testing && testFinished && testResult == 1);
  assert(WiFi.attempts == previousAttempts);
  puts("v5 ZERO BLE/network ownership and manual NTP runtime tests passed");
}
