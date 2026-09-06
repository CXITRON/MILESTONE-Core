#include <Arduino.h>
#include <MilestoneV5SafetyRuntime.h>
#include <MilestoneV5Version.h>
extern "C" bool verifyRollbackLater() { return true; }
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial.println(MilestoneV5::FIRMWARE_VERSION);
  V5Safety::begin();
}
void loop() { V5Safety::service(); }
