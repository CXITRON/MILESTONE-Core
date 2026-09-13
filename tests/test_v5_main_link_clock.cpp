// Run the production post-callback/expiry blocks with deterministic time.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <MilestoneV5BoardConfig.h>

static uint32_t clockMs, lastValidLinkMs, txLeaseId;
static unsigned negotiatedProtocolVersion, zeroCapabilities, staleEvents;
static bool awaitingAck, redraw, reply;
static int nowMetadata;
static uint32_t millis() { return clockMs; }
static void serviceCompanion(uint32_t at) {
  if (reply) lastValidLinkMs = at;
}
struct {
  void note(int) { ++staleEvents; }
  void service() { clockMs += 900; serviceCompanion(millis()); }
} portal;
struct { struct { void flush() { clockMs += 3; } } display; } hardware;
struct { void println(const char *) {} } Serial;

static void tail(uint32_t loopStart, uint32_t callbackTime, bool receives) {
  uint32_t now = loopStart;
  clockMs = callbackTime;
  reply = receives;
#include "main_link_tail.inc"
}
static void afterPortal() {
  uint32_t now = 20000;
  clockMs = now + 7;
  reply = true;
#include "main_link_portal.inc"
  assert(now - lastValidLinkMs <= MilestoneV5::kLinkStaleMs);
}
int main() {
  negotiatedProtocolVersion = 1; zeroCapabilities = 127; awaitingAck = true;
  lastValidLinkMs = 1000;
  // A valid ACK several ms after loop start must survive the same loop.
  tail(15000, 15009, true);
  assert(lastValidLinkMs == 15009 && staleEvents == 0);
  assert(negotiatedProtocolVersion == 1 && zeroCapabilities == 127);
  afterPortal();
  // Idle service stays online through repeated callback/TFT timing changes.
  for (unsigned i = 0; i < 1000; ++i)
    tail(30000 + i * 1000, 30005 + i * 1000, true);
  assert(staleEvents == 0);
  lastValidLinkMs = 50000;
  tail(54990, 54997, false); // exactly 5 seconds remains valid
  assert(lastValidLinkMs == 50000);
  tail(55000, 55001, false);
  assert(lastValidLinkMs == 0 && staleEvents == 1);
  assert(negotiatedProtocolVersion == 0 && zeroCapabilities == 0 && !awaitingAck);
  // millis() rollover retains a new ACK and expires a genuinely old one.
  tail(UINT32_MAX - 2, 2, true);
  assert(lastValidLinkMs == 2 && staleEvents == 1);
  lastValidLinkMs = UINT32_MAX - 2000;
  tail(1000, 1002, false);
  assert(lastValidLinkMs != 0);
  tail(4000, 4002, false);
  assert(lastValidLinkMs == 0 && staleEvents == 2);
  puts("v5 MAIN callback/expiry clock regression passed");
}
