#define ARDUINO_ARCH_ESP32 1
#include "../v5/MilestoneV5Main/V5ArtworkIndex.h"
#include <MilestoneV5Diagnostics.h>
#include <MilestoneV5SystemSettings.h>
#include <MilestoneV5WifiStore.h>
#include <SD.h>
#include <cassert>
int main(int argc, char **argv) {
  assert(argc == 2);
  FakeSd::root = std::filesystem::path(argv[1]) / "storage-sd";
  std::filesystem::create_directories(FakeSd::root / "now");
  MilestoneV5::WifiStore store;
  MilestoneV5::WifiCredentials a, b, out;
  strcpy(a.ssid, "first");
  strcpy(a.password, "test-only-password");
  b = a;
  strcpy(b.ssid, "second");
  assert(store.save(a));
  assert(store.save(b));
  assert(store.load(out) && !strcmp(out.ssid, "second"));
  assert(store.load(out, 1) && !strcmp(out.ssid, "first"));
  FakeNvs::writeFailure = true;
  strcpy(b.ssid, "failed");
  assert(!store.save(b));
  FakeNvs::writeFailure = false;
  assert(store.load(out) && !strcmp(out.ssid, "second"));
  // Corrupt the newest generation; recovery must retain the previous bank.
  FakeNvs::records["a"][20] ^= 1;
  assert(store.load(out) && !strcmp(out.ssid, "first"));
  a.security = 1;
  strcpy(a.username, "enterprise-user");
  strcpy(a.identity, "outer");
  strcpy(a.password, "short");
  assert(store.save(a));
  assert(store.load(out) && out.security == 1 &&
         !strcmp(out.username, "enterprise-user"));
  uint8_t wire[MilestoneV5::kWifiWireBytes];
  MilestoneV5::encodeWifi(a, wire);
  assert(MilestoneV5::decodeWifi(wire, sizeof(wire), out));
  wire[228] = 2;
  assert(!MilestoneV5::decodeWifi(wire, sizeof(wire), out));
  FakeNvs::records.clear();
  MilestoneV5::Diagnostics diagnostics;
  diagnostics.note(1, 0, 1, 0);
  diagnostics.note(1, 0, 2, 0);
  assert(diagnostics.count == 1);
  for (unsigned i = 0; i < 20; ++i)
    diagnostics.note(2, i, 100000 + i, 1800000000);
  assert(diagnostics.count == 16);
  MilestoneV5::Diagnostics restored;
  restored.begin();
  assert(restored.count == 16);
  assert(restored.events[(restored.head + 15) % 16].value == 19);
  FakeNvs::writeFailure = true;
  assert(!restored.clear());
  assert(restored.count == 16);
  FakeNvs::writeFailure = false;
  FakeNvs::records.clear();
  MilestoneV5::SystemSettings settings;
  settings.fixedAp = true;
  settings.apPassword = "test-password";
  settings.ntpSeconds = 0;
  assert(settings.save());
  MilestoneV5::SystemSettings next;
  next.begin();
  assert(next.loaded && next.fixedAp && next.ntpSeconds == 0);
  uint8_t record[96];
  next.encode(record);
  record[3] ^= 1;
  assert(!next.apply(record));
  V5ArtworkIndex index;
  index.service();
  assert(index.beginSnapshot());
  index.add(String(std::string(64, 'a')), 1, 22704, 100);
  index.finish();
  for (unsigned i = 0; i < 20; ++i)
    index.service();
  assert(index.known && index.images == 1 && index.bytes == 22704);
  assert(index.beginSnapshot());
  index.add(String(std::string(64, 'b')), 2, 22704, 200);
  index.add(String(std::string(64, 'c')), 0, 0, 200);
  index.finish();
  for (unsigned i = 0; i < 20; ++i)
    index.service();
  assert(index.images == 1);
  // The second index is B after the first generation A. Corrupt B's body.
  auto path = FakeSd::root / "now/art-index-b";
  std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
  assert(file);
  file.seekp(42);
  file.put('!');
  file.close();
  V5ArtworkIndex fallback;
  for (unsigned i = 0; i < 30; ++i)
    fallback.service();
  assert(fallback.known && fallback.images == 1 && fallback.bytes == 22704);
}
