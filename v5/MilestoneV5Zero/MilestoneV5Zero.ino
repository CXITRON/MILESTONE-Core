#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_slave.h>

#include "V5Ams.h"
#include "V5ArtworkWorker.h"
#include "V5Download.h"
#include "V5Network.h"
#include "V5OtaReceiver.h"
#include <Adafruit_NeoPixel.h>
#include <MilestoneV5BoardConfig.h>
#include <MilestoneV5Boot.h>
#include <MilestoneV5Now.h>
#include <MilestoneV5Protocol.h>
#include <MilestoneV5Thermal.h>
#include <MilestoneV5Transport.h>
#include <MilestoneV5Version.h>
#include <MilestoneV5Video.h>

extern "C" bool verifyRollbackLater() { return true; }

namespace {

alignas(4) uint8_t txSlot[MilestoneV5::kSpiSlotSize] = {};
alignas(4) uint8_t rxSlot[MilestoneV5::kSpiSlotSize] = {};
MilestoneV5::SequenceTracker mainSequences;
uint32_t txSequence = 0;
uint32_t validFrames = 0;
uint32_t invalidFrames = 0;
uint32_t zeroBootId = 0;
uint32_t mainBootId = 0;
bool mainSessionKnown = false;
spi_slave_transaction_t transaction = {};
bool transactionQueued = false;
bool sendMetadataNext = false;
uint32_t bootStartedMs = 0;
bool bootValidated = false;
bool provisionSeen = false;
uint8_t lastProvisionResult = 1;
uint8_t statusTurn = 0;
uint32_t artworkTransfer = 0;
bool requestRemembered = false;
uint32_t rememberedSequence = 0;
uint8_t rememberedResponse[MilestoneV5::kSpiSlotSize]{};
Adafruit_NeoPixel localLed(1, MilestoneV5::ZeroPins::kRgb,
                           NEO_GRB + NEO_KHZ800);
bool thermalStop = false;
V5OtaReceiver otaReceiver;
V5RemoteDownload remoteDownload;
MilestoneV5::ThermalPolicy thermal(true);
float localTemperature = NAN;
void prepareStatus(uint32_t ackSequence, bool requestValid);

void prepareArtwork(const MilestoneV5::DecodedFrame &d) {
  if (d.payloadLength < 5) {
    prepareStatus(d.fields.sequence, false);
    return;
  }
  const uint8_t op = d.payload[0];
  const uint32_t id = MilestoneV5::readVideoU32(d.payload + 1);
  uint8_t p[468]{};
  p[0] = op;
  memcpy(p + 1, d.payload + 1, 4);
  p[5] = 2;
  size_t n = 6;
  auto type = MilestoneV5::MessageType::kTaskResult;
  const int state = V5ArtworkWorker::state.load(std::memory_order_acquire);
  if (op == 3 && id && d.payloadLength >= 17 && !thermalStop &&
      !otaReceiver.active()) {
    if (state == 1)
      p[5] = 1;
    else if (V5ArtworkWorker::start(d.payload + 5, d.payloadLength - 5)) {
      artworkTransfer = id;
      p[5] = 0;
    }
  } else if (op == 4 && id == artworkTransfer && d.payloadLength == 9 &&
             !thermalStop) {
    const uint32_t offset = MilestoneV5::readVideoU32(d.payload + 5);
    if (state == 1)
      p[5] = 1;
    else if (state == 2 && offset < MilestoneV5::kArtworkBytes) {
      memcpy(p, d.payload + 1, 8);
      n = 8 + min(size_t(460), size_t(MilestoneV5::kArtworkBytes - offset));
      memcpy(p + 8, V5ArtworkWorker::packet + offset, n - 8);
      type = MilestoneV5::MessageType::kArtworkChunk;
    }
  }
  MilestoneV5::FrameFields f{type, MilestoneV5::kFlagResponse, 0, ++txSequence,
                             d.fields.sequence};
  MilestoneV5::encodeSpiSlot(f, p, n, txSlot, sizeof(txSlot));
}

void prepareTime(uint32_t ack) {
  const time_t epoch = time(nullptr);
  uint8_t payload[5] = {2};
  for (unsigned i = 0; i < 4; ++i)
    payload[i + 1] = uint32_t(epoch) >> (8 * i);
  MilestoneV5::FrameFields f{MilestoneV5::MessageType::kTaskResult,
                             MilestoneV5::kFlagResponse, 0, ++txSequence, ack};
  MilestoneV5::encodeSpiSlot(f, payload, sizeof(payload), txSlot,
                             sizeof(txSlot));
}

void prepareMetadata(uint32_t ack) {
  MilestoneV5::NowMetadata m{};
  MilestoneV5::copyNowText(m.title, sizeof(m.title),
                           V5Ams::bluetoothNowPlayingTitle());
  MilestoneV5::copyNowText(m.artist, sizeof(m.artist),
                           V5Ams::bluetoothNowPlayingArtist());
  MilestoneV5::copyNowText(m.album, sizeof(m.album),
                           V5Ams::bluetoothNowPlayingAlbum());
  m.connected = V5Ams::bluetoothNowPlayingConnected();
  m.ready = V5Ams::bluetoothNowPlayingAmsReady();
  m.playing = V5Ams::bluetoothNowPlayingIsPlaying();
  const float elapsed = V5Ams::bluetoothCurrentElapsedSeconds(),
              duration = V5Ams::bluetoothNowPlayingDurationSeconds();
  m.elapsedSeconds =
      isfinite(elapsed) ? uint32_t(constrain(elapsed, 0.0f, 604800.0f)) : 0;
  m.durationSeconds =
      isfinite(duration) ? uint32_t(constrain(duration, 0.0f, 604800.0f)) : 0;
  uint8_t bytes[444];
  size_t size;
  if (!MilestoneV5::encodeNow(m, bytes, sizeof(bytes), size))
    return;
  const MilestoneV5::FrameFields f{MilestoneV5::MessageType::kAmsMetadata,
                                   MilestoneV5::kFlagResponse, 0, ++txSequence,
                                   ack};
  MilestoneV5::encodeSpiSlot(f, bytes, size, txSlot, sizeof(txSlot));
}

// READY describes an armed DMA transaction, not intent to queue one.
void IRAM_ATTR slaveReady(spi_slave_transaction_t *) {
  gpio_set_level(static_cast<gpio_num_t>(MilestoneV5::ZeroPins::kLinkReady), 1);
}
void IRAM_ATTR slaveDone(spi_slave_transaction_t *) {
  gpio_set_level(static_cast<gpio_num_t>(MilestoneV5::ZeroPins::kLinkReady), 0);
}

void prepareStatus(uint32_t ackSequence, bool requestValid) {
  constexpr uint16_t kStatusRequestValid = 1U << 0;
  constexpr uint16_t kStatusPsramFound = 1U << 1;
  const int16_t reportedTemperature =
      isfinite(localTemperature) && localTemperature >= -40 &&
              localTemperature <= 125
          ? static_cast<int16_t>(localTemperature * 100.0f)
          : INT16_MIN;
  const MilestoneV5::StatusPayload status = {
      static_cast<uint16_t>(
          (requestValid ? kStatusRequestValid : 0) |
          (psramFound() ? kStatusPsramFound : 0) |
          (V5Ams::bluetoothNowPlayingHasLiveConnection() ? 4 : 0) |
          (WiFi.status() == WL_CONNECTED ? 8 : 0) |
          (V5ArtworkWorker::state.load() == 1 ? 16 : 0) |
          (thermalStop ? 32 : 0) | (otaReceiver.active() ? 64 : 0)),
      reportedTemperature,
      ESP.getFreeHeap(),
      ESP.getFreePsram(),
  };
  uint8_t payload[MilestoneV5::kStatusPayloadSize] = {};
  if (!MilestoneV5::encodeStatusPayload(status, payload, sizeof(payload)))
    return;
  MilestoneV5::FrameFields fields = {
      MilestoneV5::MessageType::kStatus,
      static_cast<uint16_t>(MilestoneV5::kFlagResponse),
      0,
      ++txSequence,
      ackSequence,
  };
  MilestoneV5::encodeSpiSlot(fields, payload, sizeof(payload), txSlot,
                             sizeof(txSlot));
}

void prepareHelloReply(uint32_t ackSequence) {
  const MilestoneV5::HelloPayload hello = {
      MilestoneV5::kProtocolVersion,
      MilestoneV5::kProtocolVersion,
      2,
      uint32_t(MilestoneV5::kCapabilityBleAms) |
          uint32_t(MilestoneV5::kCapabilityWifiSta) |
          uint32_t(MilestoneV5::kCapabilityInternetHttp) |
          uint32_t(MilestoneV5::kCapabilityCompanionOta) |
          (psramFound() ? uint32_t(MilestoneV5::kCapabilityPsram) : 0UL),
      zeroBootId,
  };
  uint8_t payload[MilestoneV5::kHelloPayloadSize] = {};
  if (!MilestoneV5::encodeHelloPayload(hello, payload, sizeof(payload)))
    return;
  const MilestoneV5::FrameFields fields = {
      MilestoneV5::MessageType::kHelloReply,
      MilestoneV5::kFlagResponse,
      0,
      ++txSequence,
      ackSequence,
  };
  MilestoneV5::encodeSpiSlot(fields, payload, sizeof(payload), txSlot,
                             sizeof(txSlot));
}

void reportLinkPeriodically() {
  if ((validFrames % 20) == 1 && Serial.availableForWrite() >= 80) {
    Serial.printf("MAIN link valid=%lu invalid=%lu latest=%lu\n",
                  static_cast<unsigned long>(validFrames),
                  static_cast<unsigned long>(invalidFrames),
                  static_cast<unsigned long>(mainSequences.latest()));
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  pinMode(MilestoneV5::ZeroPins::kLinkReady, OUTPUT);
  digitalWrite(MilestoneV5::ZeroPins::kLinkReady, LOW);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = MilestoneV5::ZeroPins::kLinkMosi;
  bus.miso_io_num = MilestoneV5::ZeroPins::kLinkMiso;
  bus.sclk_io_num = MilestoneV5::ZeroPins::kLinkSck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = MilestoneV5::kSpiSlotSize;

  spi_slave_interface_config_t slave = {};
  slave.spics_io_num = MilestoneV5::ZeroPins::kLinkCs;
  slave.queue_size = 1;
  slave.mode = 0;
  slave.post_setup_cb = slaveReady;
  slave.post_trans_cb = slaveDone;
  const esp_err_t result =
      spi_slave_initialize(SPI2_HOST, &bus, &slave, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) {
    Serial.printf("ZERO SPI init failed: %s\n", esp_err_to_name(result));
    while (true)
      delay(1000);
  }
  zeroBootId = esp_random();
  localLed.begin();
  localLed.setBrightness(16);
  V5Network::begin();
  bootStartedMs = millis();
  prepareStatus(0, false);
  Serial.println("MILESTONE_V5_ZERO_RUNTIME");
  Serial.println(MilestoneV5::FIRMWARE_VERSION);
}

void loop() {
  static uint32_t sampled = 0;
  if (!sampled || millis() - sampled >= 1000) {
    sampled = millis();
    localTemperature = temperatureRead();
    thermal.sample(localTemperature);
    thermalStop = thermal.stopped;
    if (getCpuFrequencyMhz() != (thermal.throttled ? 80UL : 240UL))
      setCpuFrequencyMhz(thermal.throttled ? 80 : 240);
    time_t localEpoch = time(nullptr) + 9 * 3600;
    struct tm localTime{};
    gmtime_r(&localEpoch, &localTime);
    uint16_t minute = localTime.tm_hour * 60 + localTime.tm_min;
    const auto &settings = V5Network::settings;
    bool night =
        localEpoch > 1704067200 &&
        (settings.nightStart > settings.nightEnd
             ? (minute >= settings.nightStart || minute < settings.nightEnd)
             : (minute >= settings.nightStart && minute < settings.nightEnd));
    localLed.setBrightness(settings.ledEnabled
                               ? (night ? settings.ledNight : settings.ledDay)
                               : 0);
    localLed.setPixelColor(
        0, thermalStop ? localLed.Color(255, 0, 0)
           : (V5ArtworkWorker::state.load() == 1 || remoteDownload.active)
               ? localLed.Color(0, 100, 255)
           : V5Network::connecting ? localLed.Color(255, 100, 0)
           : V5Ams::bluetoothNowPlayingHasLiveConnection()
               ? localLed.Color(0, 0, 255)
               : localLed.Color(0, 80, 0));
    localLed.show();
  }
  if (!bootValidated && millis() - bootStartedMs >= 10000 &&
      (mainSessionKnown || millis() - bootStartedMs >= 60000))
    bootValidated = MilestoneV5::finishBootCandidate(
        mainSessionKnown && ESP.getFreeHeap() > 16384 && !thermalStop);
  remoteDownload.service(!thermalStop && !otaReceiver.active());
  if (!thermalStop && !otaReceiver.needsQuiescence())
    V5Network::service(millis(),
                       !remoteDownload.active &&
                           (V5Ams::bluetoothNowPlayingHasLiveConnection() ||
                            V5ArtworkWorker::state.load() == 1),
                       remoteDownload.active);
  V5Ams::bluetoothInitialNetworkGate =
      !V5Ams::bluetoothNowPlaying.initialized &&
      (V5Network::connecting || V5Network::syncing);
  V5Ams::runtimeState = V5Network::connecting ? V5Ams::RuntimeState::CONNECTING
                        : V5Network::syncing ? V5Ams::RuntimeState::TIME_SYNCING
                                             : V5Ams::RuntimeState::READY;
  if (thermalStop || otaReceiver.needsQuiescence()) {
    V5Ams::suspendBluetoothNowPlaying();
    V5ArtworkWorker::cancel.store(true);
    V5DownloadWorker::cancel.store(true);
    if (V5ArtworkWorker::state.load() != 1 &&
        V5DownloadWorker::state.load() != 1) {
      esp_sntp_stop();
      if (WiFi.getMode() != WIFI_OFF)
        WiFi.mode(WIFI_OFF);
      V5Network::connecting = V5Network::syncing = false;
    }
  } else if (remoteDownload.active || V5Network::testing) {
    V5Ams::suspendBluetoothNowPlaying();
    V5ArtworkWorker::cancel.store(true);
  } else {
    if (V5Ams::bluetoothNowPlaying.suspended)
      V5Ams::resumeBluetoothNowPlaying();
    V5Ams::processBluetoothNowPlaying();
  }
  otaReceiver.service(!V5Ams::bluetoothNowPlayingHasLiveConnection() &&
                          V5ArtworkWorker::state.load() != 1 &&
                          V5DownloadWorker::state.load() != 1,
                      !thermalStop && ESP.getFreeHeap() > 16384, millis());
  if (!transactionQueued) {
    memset(rxSlot, 0, sizeof(rxSlot));
    transaction = {};
    transaction.length = MilestoneV5::kSpiSlotSize * 8;
    transaction.tx_buffer = txSlot;
    transaction.rx_buffer = rxSlot;
    transactionQueued =
        spi_slave_queue_trans(SPI2_HOST, &transaction, 0) == ESP_OK;
    if (!transactionQueued) {
      delay(1);
      return;
    }
  }
  spi_slave_transaction_t *completed = nullptr;
  const esp_err_t result = spi_slave_get_trans_result(SPI2_HOST, &completed, 0);
  // A timeout leaves the queued transaction and its buffers owned by DMA.
  if (result == ESP_ERR_TIMEOUT) {
    delay(1);
    return;
  }
  if (result != ESP_OK) {
    // The queued DMA transaction is no longer usable after a non-timeout
    // error. Release the software state so the next loop can queue again.
    transactionQueued = false;
    gpio_set_level(
        static_cast<gpio_num_t>(MilestoneV5::ZeroPins::kLinkReady), 0);
    if (Serial.availableForWrite() >= 64) {
      Serial.printf("ZERO SPI transaction failed: %s\n",
                    esp_err_to_name(result));
    }
    delay(1);
    return;
  }
  transactionQueued = false;

  MilestoneV5::DecodedFrame decoded = {};
  MilestoneV5::DecodeStatus frameStatus = MilestoneV5::DecodeStatus::kTooShort;
  const bool valid =
      completed == &transaction &&
      transaction.trans_len == sizeof(rxSlot) * 8 &&
      MilestoneV5::decodeSpiSlot(rxSlot, sizeof(rxSlot), decoded,
                                 frameStatus) == MilestoneV5::SlotStatus::kOk &&
      decoded.fields.flags == MilestoneV5::kFlagAckRequired &&
      decoded.fields.leaseId == 0;
  bool newSession = false;
  if (valid && decoded.fields.type == MilestoneV5::MessageType::kHello) {
    MilestoneV5::HelloPayload h{};
    newSession = MilestoneV5::decodeHelloPayload(decoded.payload,
                                                 decoded.payloadLength, h) &&
                 h.board == 1 &&
                 MilestoneV5::negotiateProtocolVersion(1, 1, h.minimumVersion,
                                                       h.maximumVersion) &&
                 (!mainSessionKnown || h.bootId != mainBootId);
  }
  if (valid && requestRemembered && !newSession) {
    if (decoded.fields.sequence == rememberedSequence) {
      memcpy(txSlot, rememberedResponse, sizeof(txSlot));
      return;
    }
    if (int32_t(decoded.fields.sequence - rememberedSequence) <= 0) {
      prepareStatus(decoded.fields.sequence, false);
      return;
    }
  }
  uint32_t ackSequence =
      mainSequences.initialized() ? mainSequences.latest() : 0;
  if (valid) {
    ++validFrames;
    ackSequence = decoded.fields.sequence;
    reportLinkPeriodically();
  } else {
    ++invalidFrames;
  }
  if (valid && decoded.fields.type == MilestoneV5::MessageType::kHello) {
    MilestoneV5::HelloPayload hello = {};
    if (MilestoneV5::decodeHelloPayload(decoded.payload, decoded.payloadLength,
                                        hello) &&
        hello.board == 1 &&
        MilestoneV5::negotiateProtocolVersion(
            MilestoneV5::kProtocolVersion, MilestoneV5::kProtocolVersion,
            hello.minimumVersion, hello.maximumVersion) != 0) {
      if (!mainSessionKnown || mainBootId != hello.bootId) {
        mainSequences.reset();
        mainBootId = hello.bootId;
        mainSessionKnown = true;
        provisionSeen = false;
        artworkTransfer = 0;
        requestRemembered = false;
      }
      mainSequences.observe(decoded.fields.sequence);
      prepareHelloReply(ackSequence);
    } else {
      prepareStatus(ackSequence, false);
    }
  } else if (valid && mainSessionKnown &&
             (decoded.fields.type == MilestoneV5::MessageType::kOtaControl ||
              decoded.fields.type == MilestoneV5::MessageType::kOtaChunk)) {
    uint8_t response[10];
    size_t n = 0;
    if (otaReceiver.request(decoded.payload, decoded.payloadLength, response,
                            n)) {
      MilestoneV5::FrameFields fields{MilestoneV5::MessageType::kOtaControl,
                                      MilestoneV5::kFlagResponse, 0,
                                      ++txSequence, decoded.fields.sequence};
      MilestoneV5::encodeSpiSlot(fields, response, n, txSlot, sizeof(txSlot));
    } else
      prepareStatus(ackSequence, false);
  } else if (valid && mainSessionKnown &&
             decoded.fields.type == MilestoneV5::MessageType::kTaskRequest &&
             decoded.payloadLength == 97 && decoded.payload[0] == 34) {
    auto next = V5Network::settings;
    bool ok = next.apply(decoded.payload + 1) && next.save();
    if (ok)
      V5Network::settings = next;
    uint8_t response[] = {34, uint8_t(ok ? 0 : 1)};
    MilestoneV5::FrameFields f{MilestoneV5::MessageType::kTaskResult,
                               MilestoneV5::kFlagResponse, 0, ++txSequence,
                               decoded.fields.sequence};
    MilestoneV5::encodeSpiSlot(f, response, sizeof(response), txSlot,
                               sizeof(txSlot));
  } else if (valid && mainSessionKnown &&
             decoded.fields.type == MilestoneV5::MessageType::kTaskRequest &&
             decoded.payloadLength == 2 &&
             (decoded.payload[0] == 32 || decoded.payload[0] == 33)) {
    uint8_t response[MilestoneV5::kLegacySnapshotBytes + 2]{};
    response[0] = decoded.payload[0];
    size_t n = 2;
    if (response[0] == 32) {
      if (MilestoneV5::legacySnapshot(response + 2)) {
        response[1] = 1;
        n += MilestoneV5::kLegacySnapshotBytes;
      }
    } else {
      MilestoneV5::WifiCredentials v;
      if (V5Network::store.load(v, decoded.payload[1])) {
        response[1] = 1;
        MilestoneV5::encodeWifi(v, response + 2);
        n += MilestoneV5::kWifiWireBytes;
      }
    }
    MilestoneV5::FrameFields f{MilestoneV5::MessageType::kTaskResult,
                               MilestoneV5::kFlagResponse, 0, ++txSequence,
                               decoded.fields.sequence};
    MilestoneV5::encodeSpiSlot(f, response, n, txSlot, sizeof(txSlot));
  } else if (valid && mainSessionKnown &&
             decoded.fields.type == MilestoneV5::MessageType::kTaskRequest &&
             decoded.payloadLength >= 5 && decoded.payload[0] >= 16 &&
             decoded.payload[0] <= 18) {
    uint8_t response[474];
    size_t n = 0;
    if (remoteDownload.request(decoded.payload, decoded.payloadLength, response,
                               n, !thermalStop && !otaReceiver.active())) {
      MilestoneV5::FrameFields f{MilestoneV5::MessageType::kTaskResult,
                                 MilestoneV5::kFlagResponse, 0, ++txSequence,
                                 decoded.fields.sequence};
      MilestoneV5::encodeSpiSlot(f, response, n, txSlot, sizeof(txSlot));
    } else
      prepareStatus(ackSequence, false);
  } else if (valid && mainSessionKnown &&
             decoded.fields.type == MilestoneV5::MessageType::kTaskRequest &&
             decoded.payloadLength >= 5 &&
             (decoded.payload[0] == 3 || decoded.payload[0] == 4)) {
    prepareArtwork(decoded);
  } else if (valid && mainSessionKnown &&
             decoded.fields.type == MilestoneV5::MessageType::kTaskRequest &&
             (decoded.payloadLength == 99 ||
              decoded.payloadLength == 1 + MilestoneV5::kWifiWireBytes) &&
             decoded.payload[0] == 1) {
    lastProvisionResult = remoteDownload.active || otaReceiver.active()
                              ? 1
                              : V5Network::provision(decoded.payload + 1,
                                                     decoded.payloadLength - 1);
    uint8_t result[] = {1, lastProvisionResult};
    MilestoneV5::FrameFields f{MilestoneV5::MessageType::kTaskResult,
                               MilestoneV5::kFlagResponse, 0, ++txSequence,
                               decoded.fields.sequence};
    MilestoneV5::encodeSpiSlot(f, result, sizeof(result), txSlot,
                               sizeof(txSlot));
  } else {
    const bool accepted =
        valid && mainSessionKnown &&
        decoded.fields.type == MilestoneV5::MessageType::kHeartbeat &&
        decoded.payloadLength == 3 && decoded.payload[0] <= 2 &&
        decoded.payload[1] <= 1 && decoded.payload[2] <= 1;
    if (accepted) {
      mainSequences.observe(decoded.fields.sequence);
      if (!V5Network::testing)
        V5Network::testFinished = false;
    }
    if (accepted && ++statusTurn >= 10 && time(nullptr) > 1704067200 &&
        !V5Network::syncing) {
      prepareTime(ackSequence);
      statusTurn = 0;
    } else if (accepted && sendMetadataNext)
      prepareMetadata(ackSequence);
    else
      prepareStatus(ackSequence, accepted);
    sendMetadataNext = !sendMetadataNext;
  }
  if (valid && mainSessionKnown) {
    rememberedSequence = decoded.fields.sequence;
    requestRemembered = true;
    memcpy(rememberedResponse, txSlot, sizeof(txSlot));
  }
}
