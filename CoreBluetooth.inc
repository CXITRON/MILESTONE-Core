// Bluetooth Now Playing integration for MILESTONE Core.
// iOS exposes Apple Media Service (AMS) over BLE.  MILESTONE acts as a BLE
// peripheral for pairing/advertising and uses NimBLE's GATT client procedures
// on the same connection to subscribe to the iPhone-hosted AMS service.

bool bluetoothNowPlayingConfigured() {
  return MILESTONE_HAS_BLUETOOTH &&
         (MILESTONE_BLUETOOTH_ALWAYS_ON || config.bluetoothNowPlaying);
}

#if MILESTONE_HAS_BLUETOOTH && defined(CONFIG_NIMBLE_ENABLED)

constexpr uint8_t AMS_ENTITY_PLAYER = 0;
constexpr uint8_t AMS_ENTITY_TRACK = 2;
constexpr uint8_t AMS_PLAYER_NAME = 0;
constexpr uint8_t AMS_PLAYER_PLAYBACK_INFO = 1;
constexpr uint8_t AMS_TRACK_ARTIST = 0;
constexpr uint8_t AMS_TRACK_TITLE = 2;
constexpr uint8_t AMS_TRACK_DURATION = 3;
constexpr uint8_t AMS_FLAG_TRUNCATED = 0x01;
constexpr uint32_t AMS_DISCOVERY_RETRY_MS = 5000UL;
constexpr uint16_t AMS_CONN_INTERVAL_MIN = 80;   // 100 ms in 1.25 ms units.
constexpr uint16_t AMS_CONN_INTERVAL_MAX = 160;  // 200 ms.
constexpr uint16_t AMS_CONN_LATENCY = 3;
constexpr uint16_t AMS_CONN_TIMEOUT = 600;       // 6 seconds in 10 ms units.
constexpr size_t AMS_NOTIFICATION_MAX = 244;
constexpr uint8_t AMS_NOTIFICATION_QUEUE = 6;
constexpr uint32_t BLE_ADVERTISING_RETRY_MS = 5000UL;
constexpr uint32_t BLE_DISCONNECT_TIMEOUT_MS = 2500UL;
constexpr uint32_t BLE_SHUTDOWN_QUIESCE_MS = 75UL;
constexpr char BLE_DEVICE_NAME[] = "MILESTONE";
// make-release.sh verifies this executed marker in the compiled NOW image,
// preventing a BLE stub from being published as a working build.
constexpr char BLE_RUNTIME_MARKER[] = "MILESTONE_BLE_AMS_RUNTIME_V3";

enum class BluetoothRuntimeStage : uint8_t {
  OFF,
  INITIALIZING,
  ADVERTISING,
  CONNECTED,
  SECURING,
  DISCOVERING,
  READY,
  SUSPENDED,
  ERROR
};

static const ble_uuid128_t AMS_SERVICE_UUID = BLE_UUID128_INIT(
    0xdc, 0xf8, 0x55, 0xad, 0x02, 0xc5, 0xf4, 0x8e,
    0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50, 0xd3, 0x89);
static const ble_uuid128_t AMS_ENTITY_UPDATE_UUID = BLE_UUID128_INIT(
    0x02, 0xc1, 0x96, 0xba, 0x92, 0xbb, 0x0c, 0x9a,
    0x1f, 0x41, 0x8d, 0x80, 0xce, 0xab, 0x7c, 0x2f);
static const ble_uuid128_t AMS_ENTITY_ATTRIBUTE_UUID = BLE_UUID128_INIT(
    0xd7, 0xd5, 0xbb, 0x70, 0xa8, 0xa3, 0xab, 0xa6,
    0xd8, 0x46, 0xab, 0x23, 0x8c, 0xf3, 0xb2, 0xc6);
// Keep the CCCD UUID in stable storage. NimBLE's convenience declaration
// macro expands to a C compound literal; taking its address is rejected by the
// C++ compiler used by Arduino-ESP32 3.3.11.
static const ble_uuid16_t BLE_CCCD_UUID = BLE_UUID16_INIT(BLE_GATT_DSC_CLT_CFG_UUID16);

struct BluetoothNowPlayingState {
  bool initialized = false;
  bool connected = false;
  bool secured = false;
  bool amsReady = false;
  bool suspended = false;
  bool discoveryActive = false;
  uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
  uint16_t serviceStart = 0;
  uint16_t serviceEnd = 0;
  uint16_t entityUpdateHandle = 0;
  uint16_t entityAttributeHandle = 0;
  uint16_t entityUpdateCccdHandle = 0;
  uint32_t nextDiscoveryMs = 0;
  uint32_t nextAdvertisingRetryMs = 0;
  uint32_t nextInitializationRetryMs = 0;
  BluetoothRuntimeStage stage = BluetoothRuntimeStage::OFF;
  const char *lastError = "";
  int lastErrorCode = 0;
  String playerName;
  String title;
  String artist;
  uint8_t playbackState = 0;
  float playbackRate = 0.0f;
  float elapsedSeconds = 0.0f;
  float durationSeconds = 0.0f;
  uint32_t playbackAnchorMs = 0;
  uint32_t metadataChangedMs = 0;
  String address;
};

struct AmsNotificationPacket {
  uint16_t len = 0;
  uint8_t data[AMS_NOTIFICATION_MAX] = {};
};

BluetoothNowPlayingState bluetoothNowPlaying;
BLEServer *bluetoothServer = nullptr;
BLEAdvertising *bluetoothAdvertising = nullptr;
portMUX_TYPE amsNotificationMux = portMUX_INITIALIZER_UNLOCKED;
AmsNotificationPacket amsNotificationQueue[AMS_NOTIFICATION_QUEUE];
// Queue indices are accessed only while amsNotificationMux is held. volatile
// does not provide inter-core synchronization and causes deprecated volatile
// ++/-- diagnostics under the ESP32-S3 C++ toolchain, so keep them ordinary
// objects protected by the critical section.
uint8_t amsNotificationHead = 0;
uint8_t amsNotificationTail = 0;
uint8_t amsNotificationCount = 0;
volatile bool bluetoothConnectedEvent = false;
volatile bool bluetoothDisconnectedEvent = false;
volatile bool bluetoothEncryptedEvent = false;
volatile bool bluetoothConnectFailedEvent = false;
volatile uint16_t bluetoothEventConnHandle = BLE_HS_CONN_HANDLE_NONE;
volatile int bluetoothEncryptionStatus = 0;
volatile int bluetoothConnectFailureStatus = 0;

const char *bluetoothRuntimeStageName(BluetoothRuntimeStage stage) {
  switch (stage) {
    case BluetoothRuntimeStage::OFF: return "off";
    case BluetoothRuntimeStage::INITIALIZING: return "initializing";
    case BluetoothRuntimeStage::ADVERTISING: return "advertising";
    case BluetoothRuntimeStage::CONNECTED: return "connected";
    case BluetoothRuntimeStage::SECURING: return "securing";
    case BluetoothRuntimeStage::DISCOVERING: return "discovering";
    case BluetoothRuntimeStage::READY: return "ready";
    case BluetoothRuntimeStage::SUSPENDED: return "suspended";
    case BluetoothRuntimeStage::ERROR: return "error";
  }
  return "unknown";
}

void setBluetoothError(const char *message, int code = 0) {
  bluetoothNowPlaying.lastError = message ? message : "unknown";
  bluetoothNowPlaying.lastErrorCode = code;
  bluetoothNowPlaying.stage = BluetoothRuntimeStage::ERROR;
}

void clearBluetoothError() {
  bluetoothNowPlaying.lastError = "";
  bluetoothNowPlaying.lastErrorCode = 0;
}

void scheduleBluetoothInitializationRetry() {
  bluetoothNowPlaying.nextInitializationRetryMs = millis() + BLE_ADVERTISING_RETRY_MS;
}

void resetAmsHandles() {
  bluetoothNowPlaying.amsReady = false;
  bluetoothNowPlaying.discoveryActive = false;
  bluetoothNowPlaying.serviceStart = 0;
  bluetoothNowPlaying.serviceEnd = 0;
  bluetoothNowPlaying.entityUpdateHandle = 0;
  bluetoothNowPlaying.entityAttributeHandle = 0;
  bluetoothNowPlaying.entityUpdateCccdHandle = 0;
}

void clearBluetoothNowPlayingMetadata() {
  bluetoothNowPlaying.playerName = "";
  bluetoothNowPlaying.title = "";
  bluetoothNowPlaying.artist = "";
  bluetoothNowPlaying.playbackState = 0;
  bluetoothNowPlaying.playbackRate = 0.0f;
  bluetoothNowPlaying.elapsedSeconds = 0.0f;
  bluetoothNowPlaying.durationSeconds = 0.0f;
  bluetoothNowPlaying.playbackAnchorMs = millis();
  bluetoothNowPlaying.metadataChangedMs = millis();
}

void queueAmsNotification(const os_mbuf *om) {
  if (!om) return;
  const uint32_t rawLen = OS_MBUF_PKTLEN(om);
  const uint16_t packetLen = static_cast<uint16_t>(rawLen > AMS_NOTIFICATION_MAX ? AMS_NOTIFICATION_MAX : rawLen);
  if (packetLen < 3) return;

  // Copy the mbuf before entering the critical section. The GAP callback is a
  // task context, not an ISR; keeping the lock limited to queue bookkeeping
  // avoids holding off the other core while NimBLE walks an mbuf chain.
  AmsNotificationPacket incoming;
  incoming.len = packetLen;
  if (os_mbuf_copydata(const_cast<os_mbuf *>(om), 0, packetLen, incoming.data) != 0) return;

  portENTER_CRITICAL(&amsNotificationMux);
  if (amsNotificationCount == AMS_NOTIFICATION_QUEUE) {
    amsNotificationTail = (amsNotificationTail + 1U) % AMS_NOTIFICATION_QUEUE;
    amsNotificationCount -= 1U;
  }
  amsNotificationQueue[amsNotificationHead] = incoming;
  amsNotificationHead = (amsNotificationHead + 1U) % AMS_NOTIFICATION_QUEUE;
  amsNotificationCount += 1U;
  portEXIT_CRITICAL(&amsNotificationMux);
}

bool popAmsNotification(AmsNotificationPacket &packet) {
  bool available = false;
  portENTER_CRITICAL(&amsNotificationMux);
  if (amsNotificationCount > 0) {
    packet = amsNotificationQueue[amsNotificationTail];
    amsNotificationTail = (amsNotificationTail + 1U) % AMS_NOTIFICATION_QUEUE;
    amsNotificationCount -= 1U;
    available = true;
  }
  portEXIT_CRITICAL(&amsNotificationMux);
  return available;
}

int milestoneBluetoothGapEvent(struct ble_gap_event *event, void *) {
  if (!event) return 0;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        bluetoothEventConnHandle = event->connect.conn_handle;
        bluetoothConnectedEvent = true;
      } else {
        bluetoothConnectFailureStatus = event->connect.status;
        bluetoothConnectFailedEvent = true;
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      bluetoothEventConnHandle = event->disconnect.conn.conn_handle;
      bluetoothDisconnectedEvent = true;
      break;
    case BLE_GAP_EVENT_ENC_CHANGE:
      bluetoothEventConnHandle = event->enc_change.conn_handle;
      bluetoothEncryptionStatus = event->enc_change.status;
      bluetoothEncryptedEvent = true;
      break;
    case BLE_GAP_EVENT_NOTIFY_RX:
      if (event->notify_rx.conn_handle == bluetoothNowPlaying.connHandle &&
          event->notify_rx.attr_handle == bluetoothNowPlaying.entityUpdateHandle) {
        queueAmsNotification(event->notify_rx.om);
      }
      break;
    default:
      break;
  }
  return 0;
}

void scheduleAmsDiscoveryRetry() {
  resetAmsHandles();
  bluetoothNowPlaying.nextDiscoveryMs = millis() + AMS_DISCOVERY_RETRY_MS;
}

int amsDescriptorDiscoveryCallback(uint16_t connHandle, const ble_gatt_error *error,
                                   uint16_t chrValHandle, const ble_gatt_dsc *descriptor, void *);
int amsCharacteristicDiscoveryCallback(uint16_t connHandle, const ble_gatt_error *error,
                                       const ble_gatt_chr *characteristic, void *);
int amsServiceDiscoveryCallback(uint16_t connHandle, const ble_gatt_error *error,
                                const ble_gatt_svc *service, void *);
int amsSubscriptionWriteCallback(uint16_t connHandle, const ble_gatt_error *error,
                                 ble_gatt_attr *attribute, void *arg);

bool beginAmsDiscovery() {
  if (!bluetoothNowPlaying.connected || !bluetoothNowPlaying.secured ||
      bluetoothNowPlaying.suspended || bluetoothNowPlaying.discoveryActive ||
      bluetoothNowPlaying.connHandle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }
  resetAmsHandles();
  bluetoothNowPlaying.discoveryActive = true;
  bluetoothNowPlaying.stage = BluetoothRuntimeStage::DISCOVERING;
  const int rc = ble_gattc_disc_svc_by_uuid(bluetoothNowPlaying.connHandle,
                                             &AMS_SERVICE_UUID.u,
                                             amsServiceDiscoveryCallback, nullptr);
  if (rc != 0) {
    setBluetoothError("ams-service-discovery-start-failed", rc);
    scheduleAmsDiscoveryRetry();
    return false;
  }
  return true;
}

int amsServiceDiscoveryCallback(uint16_t connHandle, const ble_gatt_error *error,
                                const ble_gatt_svc *service, void *) {
  if (connHandle != bluetoothNowPlaying.connHandle || !error) return 0;
  if (error->status == 0 && service) {
    bluetoothNowPlaying.serviceStart = service->start_handle;
    bluetoothNowPlaying.serviceEnd = service->end_handle;
    return 0;
  }
  if (error->status == BLE_HS_EDONE && bluetoothNowPlaying.serviceStart != 0) {
    const int rc = ble_gattc_disc_all_chrs(connHandle,
                                            bluetoothNowPlaying.serviceStart,
                                            bluetoothNowPlaying.serviceEnd,
                                            amsCharacteristicDiscoveryCallback, nullptr);
    if (rc == 0) return 0;
  }
  if (error->status != BLE_HS_EDONE || bluetoothNowPlaying.serviceStart == 0) {
    setBluetoothError("ams-service-not-found", error->status);
  }
  scheduleAmsDiscoveryRetry();
  return 0;
}

int amsCharacteristicDiscoveryCallback(uint16_t connHandle, const ble_gatt_error *error,
                                       const ble_gatt_chr *characteristic, void *) {
  if (connHandle != bluetoothNowPlaying.connHandle || !error) return 0;
  if (error->status == 0 && characteristic) {
    if (ble_uuid_cmp(&characteristic->uuid.u, &AMS_ENTITY_UPDATE_UUID.u) == 0) {
      bluetoothNowPlaying.entityUpdateHandle = characteristic->val_handle;
    } else if (ble_uuid_cmp(&characteristic->uuid.u, &AMS_ENTITY_ATTRIBUTE_UUID.u) == 0) {
      bluetoothNowPlaying.entityAttributeHandle = characteristic->val_handle;
    }
    return 0;
  }
  if (error->status == BLE_HS_EDONE && bluetoothNowPlaying.entityUpdateHandle != 0) {
    const int rc = ble_gattc_disc_all_dscs(connHandle,
                                            bluetoothNowPlaying.entityUpdateHandle,
                                            bluetoothNowPlaying.serviceEnd,
                                            amsDescriptorDiscoveryCallback, nullptr);
    if (rc == 0) return 0;
  }
  if (error->status != BLE_HS_EDONE || bluetoothNowPlaying.entityUpdateHandle == 0) {
    setBluetoothError("ams-characteristic-not-found", error->status);
  }
  scheduleAmsDiscoveryRetry();
  return 0;
}

int amsDescriptorDiscoveryCallback(uint16_t connHandle, const ble_gatt_error *error,
                                   uint16_t chrValHandle, const ble_gatt_dsc *descriptor, void *) {
  if (connHandle != bluetoothNowPlaying.connHandle || !error) return 0;
  if (error->status == 0 && descriptor) {
    if (chrValHandle == bluetoothNowPlaying.entityUpdateHandle &&
        ble_uuid_cmp(&descriptor->uuid.u, &BLE_CCCD_UUID.u) == 0) {
      bluetoothNowPlaying.entityUpdateCccdHandle = descriptor->handle;
    }
    return 0;
  }
  if (error->status == BLE_HS_EDONE && bluetoothNowPlaying.entityUpdateCccdHandle != 0) {
    static const uint8_t enableNotifications[2] = {0x01, 0x00};
    const int rc = ble_gattc_write_flat(connHandle, bluetoothNowPlaying.entityUpdateCccdHandle,
                                         enableNotifications, sizeof(enableNotifications),
                                         amsSubscriptionWriteCallback,
                                         reinterpret_cast<void *>(static_cast<uintptr_t>(0)));
    if (rc == 0) return 0;
  }
  if (error->status != BLE_HS_EDONE || bluetoothNowPlaying.entityUpdateCccdHandle == 0) {
    setBluetoothError("ams-cccd-not-found", error->status);
  }
  scheduleAmsDiscoveryRetry();
  return 0;
}

int amsSubscriptionWriteCallback(uint16_t connHandle, const ble_gatt_error *error,
                                 ble_gatt_attr *, void *arg) {
  if (connHandle != bluetoothNowPlaying.connHandle || !error) return 0;
  if (error->status != 0) {
    setBluetoothError("ams-subscription-write-failed", error->status);
    scheduleAmsDiscoveryRetry();
    return 0;
  }
  const uintptr_t stage = reinterpret_cast<uintptr_t>(arg);
  if (stage == 0) {
    static const uint8_t trackSubscription[] = {
        AMS_ENTITY_TRACK, AMS_TRACK_ARTIST, AMS_TRACK_TITLE, AMS_TRACK_DURATION};
    const int rc = ble_gattc_write_flat(connHandle, bluetoothNowPlaying.entityUpdateHandle,
                                         trackSubscription, sizeof(trackSubscription),
                                         amsSubscriptionWriteCallback,
                                         reinterpret_cast<void *>(static_cast<uintptr_t>(1)));
    if (rc == 0) return 0;
  } else if (stage == 1) {
    static const uint8_t playerSubscription[] = {
        AMS_ENTITY_PLAYER, AMS_PLAYER_NAME, AMS_PLAYER_PLAYBACK_INFO};
    const int rc = ble_gattc_write_flat(connHandle, bluetoothNowPlaying.entityUpdateHandle,
                                         playerSubscription, sizeof(playerSubscription),
                                         amsSubscriptionWriteCallback,
                                         reinterpret_cast<void *>(static_cast<uintptr_t>(2)));
    if (rc == 0) return 0;
  } else {
    bluetoothNowPlaying.amsReady = true;
    bluetoothNowPlaying.discoveryActive = false;
    bluetoothNowPlaying.nextDiscoveryMs = 0;
    bluetoothNowPlaying.stage = BluetoothRuntimeStage::READY;
    clearBluetoothError();
    return 0;
  }
  setBluetoothError("ams-subscription-command-failed");
  scheduleAmsDiscoveryRetry();
  return 0;
}

bool startBluetoothAdvertising() {
  if (!bluetoothNowPlaying.initialized || bluetoothNowPlaying.suspended ||
      !bluetoothNowPlayingConfigured() || bluetoothNowPlaying.connected || !bluetoothAdvertising) return false;
  if (bluetoothAdvertising->isAdvertising()) {
    bluetoothNowPlaying.stage = BluetoothRuntimeStage::ADVERTISING;
    return true;
  }
  if (!bluetoothAdvertising->start() || !bluetoothAdvertising->isAdvertising()) {
    setBluetoothError("advertising-start-failed");
    bluetoothNowPlaying.nextAdvertisingRetryMs = millis() + BLE_ADVERTISING_RETRY_MS;
    logLine("BLE advertising start failed; retry scheduled");
    return false;
  }
  bluetoothNowPlaying.stage = BluetoothRuntimeStage::ADVERTISING;
  bluetoothNowPlaying.nextAdvertisingRetryMs = 0;
  logLine(String(BLE_RUNTIME_MARKER) + " advertising as " + BLE_DEVICE_NAME +
          " address=" + bluetoothNowPlaying.address);
  return true;
}

bool configureBluetoothAdvertising() {
  if (!bluetoothAdvertising) {
    setBluetoothError("advertising-object-missing");
    return false;
  }
  BLEAdvertisementData advertisementData;
  advertisementData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  // Complete list of 128-bit service solicitation UUIDs (AD type 0x15).
  // UUID payload is little-endian on the air.
  char solicitation[] = {
      0x11, static_cast<char>(ESP_BLE_AD_TYPE_128SOL_SRV_UUID),
      static_cast<char>(0xdc), static_cast<char>(0xf8), 0x55, static_cast<char>(0xad), 0x02, static_cast<char>(0xc5),
      static_cast<char>(0xf4), static_cast<char>(0x8e), 0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50,
      static_cast<char>(0xd3), static_cast<char>(0x89)};
  advertisementData.addData(solicitation, sizeof(solicitation));
  // Flags (3 bytes) + AMS solicitation (18 bytes) leave exactly 10 bytes in
  // the legacy 31-byte packet.  Include an eight-character shortened name so
  // passive scans can identify the device without a scan response.
  advertisementData.setShortName("MILESTON");
  BLEAdvertisementData scanResponse;
  scanResponse.setName(BLE_DEVICE_NAME);
  scanResponse.addTxPower();
  const bool advertisementDataReady = bluetoothAdvertising->setAdvertisementData(advertisementData);
  const bool scanResponseReady = bluetoothAdvertising->setScanResponseData(scanResponse);
  // Service solicitation is useful only when iOS can actively scan and open a
  // connection. Do not rely on library defaults that may change by board core.
  bluetoothAdvertising->setScanResponse(true);
  bluetoothAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
  bluetoothAdvertising->setScanFilter(false, false);
  // 160-240 ms advertising interval reduces idle RF duty while remaining easy
  // to discover when the user enables the feature.
  bluetoothAdvertising->setMinInterval(256);
  bluetoothAdvertising->setMaxInterval(384);
  if (!advertisementDataReady) {
    setBluetoothError("advertising-data-rejected");
    return false;
  }
  if (!scanResponseReady) {
    setBluetoothError("scan-response-rejected");
    return false;
  }
  return true;
}

bool initializeBluetoothNowPlaying() {
  if (!bluetoothNowPlayingConfigured() || mediaStreamActive || bluetoothNowPlaying.initialized) {
    return bluetoothNowPlaying.initialized;
  }
  bluetoothNowPlaying.stage = BluetoothRuntimeStage::INITIALIZING;
  clearBluetoothError();
  if (!BLEDevice::init(BLE_DEVICE_NAME)) {
    setBluetoothError("stack-initialization-failed");
    scheduleBluetoothInitializationRetry();
    logLine("BLE initialization failed");
    return false;
  }
  if (BLEDevice::getBLEStack() != BLEStack::NIMBLE) {
    setBluetoothError("nimble-stack-required");
    scheduleBluetoothInitializationRetry();
    logLine("BLE Now Playing requires NimBLE on this build");
    BLEDevice::deinit(false);
    return false;
  }
  BLEDevice::setMTU(185);
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setForceAuthentication(true);
  BLEDevice::setCustomGapHandler(milestoneBluetoothGapEvent);

  bluetoothServer = BLEDevice::createServer();
  if (!bluetoothServer) {
    setBluetoothError("server-creation-failed");
    scheduleBluetoothInitializationRetry();
    logLine("BLE server creation failed");
    BLEDevice::deinit(false);
    return false;
  }
  bluetoothServer->start();
  bluetoothAdvertising = BLEDevice::getAdvertising();
  if (!configureBluetoothAdvertising()) {
    scheduleBluetoothInitializationRetry();
    logLine(String("BLE advertising configuration failed: ") + bluetoothNowPlaying.lastError);
    BLEDevice::deinit(false);
    bluetoothServer = nullptr;
    bluetoothAdvertising = nullptr;
    return false;
  }
  bluetoothNowPlaying.initialized = true;
  bluetoothNowPlaying.nextInitializationRetryMs = 0;
  bluetoothNowPlaying.suspended = false;
  bluetoothNowPlaying.address = BLEDevice::getAddress().toString();
  resetAmsHandles();
  clearBluetoothNowPlayingMetadata();
  if (!startBluetoothAdvertising()) return false;
  logLine(String("BLE Now Playing ready via ") + BLEDevice::getBLEStackString());
  return true;
}

bool waitForBluetoothDisconnectBeforeShutdown() {
  if (!bluetoothNowPlaying.connected || !bluetoothServer ||
      bluetoothNowPlaying.connHandle == BLE_HS_CONN_HANDLE_NONE) return true;

  const uint16_t connHandle = bluetoothNowPlaying.connHandle;
  bluetoothDisconnectedEvent = false;
  const int disconnectResult = bluetoothServer->disconnect(connHandle);
  if (disconnectResult == BLE_HS_ENOTCONN) return true;
  if (disconnectResult != 0 && disconnectResult != BLE_HS_EALREADY) {
    logLine(String("BLE disconnect request failed before shutdown: ") + disconnectResult);
    return false;
  }

  const uint32_t startedMs = millis();
  while (!(bluetoothDisconnectedEvent && bluetoothEventConnHandle == connHandle)) {
    if (elapsed(millis(), startedMs, BLE_DISCONNECT_TIMEOUT_MS)) {
      logLine("BLE disconnect confirmation timed out; stack deinit refused");
      return false;
    }
    delay(10);  // Yield so the NimBLE host can deliver GAP DISCONNECT.
  }

  bluetoothDisconnectedEvent = false;
  bluetoothNowPlaying.connected = false;
  bluetoothNowPlaying.secured = false;
  bluetoothNowPlaying.connHandle = BLE_HS_CONN_HANDLE_NONE;
  resetAmsHandles();
  clearBluetoothNowPlayingMetadata();
  logLine("BLE disconnect confirmed before stack shutdown");
  return true;
}

bool shutdownBluetoothNowPlaying() {
  if (!bluetoothNowPlaying.initialized) return true;
  bluetoothNowPlaying.suspended = true;
  bluetoothNowPlaying.stage = BluetoothRuntimeStage::SUSPENDED;
  if (bluetoothAdvertising) bluetoothAdvertising->stop();
  if (!waitForBluetoothDisconnectBeforeShutdown()) {
    return false;
  }
  // GAP disconnect can be followed by queued security/GATT callbacks. Give
  // the NimBLE host a bounded quiesce window before BLEDevice deletes its
  // server and stops the host/controller tasks.
  delay(BLE_SHUTDOWN_QUIESCE_MS);
  BLEDevice::deinit(false);
  bluetoothServer = nullptr;
  bluetoothAdvertising = nullptr;
  bluetoothNowPlaying = BluetoothNowPlayingState();
  portENTER_CRITICAL(&amsNotificationMux);
  amsNotificationHead = 0;
  amsNotificationTail = 0;
  amsNotificationCount = 0;
  portEXIT_CRITICAL(&amsNotificationMux);
  logLine("BLE Now Playing stopped");
  return true;
}

bool isolateBluetoothForFirmwareOperation() {
  if (!bluetoothNowPlayingConfigured() || bluetoothFirmwareOperationIsolated) return true;
  bluetoothFirmwareOperationIsolated = true;
  const uint32_t heapBefore = ESP.getFreeHeap();
  const uint32_t largestBefore = ESP.getMaxAllocHeap();
  if (bluetoothNowPlaying.initialized && !shutdownBluetoothNowPlaying()) {
    bluetoothFirmwareOperationIsolated = false;
    bluetoothNowPlaying.suspended = false;
    if (!bluetoothNowPlaying.connected) startBluetoothAdvertising();
    return false;
  }
  // Give the controller task a short opportunity to return its internal-RAM
  // allocations before TLS performs its largest contiguous allocations.
  delay(20);
  logLine(String("BLE isolated for firmware HTTPS: heap=") + heapBefore + "->" +
          ESP.getFreeHeap() + " max=" + largestBefore + "->" + ESP.getMaxAllocHeap());
  return true;
}

void restoreBluetoothAfterFirmwareOperation() {
  if (!bluetoothFirmwareOperationIsolated) return;
  bluetoothFirmwareOperationIsolated = false;
  // Do not synchronously rebuild NimBLE in the HTTPS cleanup path. The normal
  // cooperative Bluetooth service starts it on the next loop iteration.
  logLine("BLE firmware HTTPS isolation released");
}

void suspendBluetoothNowPlaying() {
  if (!bluetoothNowPlaying.initialized || bluetoothNowPlaying.suspended) return;
  bluetoothNowPlaying.suspended = true;
  bluetoothNowPlaying.stage = BluetoothRuntimeStage::SUSPENDED;
  if (bluetoothAdvertising) bluetoothAdvertising->stop();
  if (bluetoothNowPlaying.connected && bluetoothServer &&
      bluetoothNowPlaying.connHandle != BLE_HS_CONN_HANDLE_NONE) {
    bluetoothServer->disconnect(bluetoothNowPlaying.connHandle);
  }
  resetAmsHandles();
  clearBluetoothNowPlayingMetadata();
  logLine("BLE Now Playing suspended for STREAM_MODE");
}

void resumeBluetoothNowPlaying() {
  if (!bluetoothNowPlayingConfigured()) return;
  if (!bluetoothNowPlaying.initialized) {
    if (deadlineReached(millis(), bluetoothNowPlaying.nextInitializationRetryMs)) {
      initializeBluetoothNowPlaying();
    }
    return;
  }
  bluetoothNowPlaying.suspended = false;
  startBluetoothAdvertising();
  logLine("BLE Now Playing resumed");
}

void parseAmsPlaybackInfo(const String &value) {
  const int first = value.indexOf(',');
  const int second = first >= 0 ? value.indexOf(',', first + 1) : -1;
  if (first <= 0 || second <= first) return;
  bluetoothNowPlaying.playbackState = static_cast<uint8_t>(value.substring(0, first).toInt());
  bluetoothNowPlaying.playbackRate = value.substring(first + 1, second).toFloat();
  bluetoothNowPlaying.elapsedSeconds = max(0.0f, value.substring(second + 1).toFloat());
  bluetoothNowPlaying.playbackAnchorMs = millis();
}

void applyAmsNotification(const AmsNotificationPacket &packet) {
  if (packet.len < 3) return;
  const uint8_t entity = packet.data[0];
  const uint8_t attribute = packet.data[1];
  const uint8_t flags = packet.data[2];
  String value;
  value.reserve(packet.len - 3);
  for (uint16_t i = 3; i < packet.len; ++i) value += static_cast<char>(packet.data[i]);

  if (entity == AMS_ENTITY_PLAYER) {
    if (attribute == AMS_PLAYER_NAME) bluetoothNowPlaying.playerName = value;
    else if (attribute == AMS_PLAYER_PLAYBACK_INFO) parseAmsPlaybackInfo(value);
  } else if (entity == AMS_ENTITY_TRACK) {
    if (attribute == AMS_TRACK_ARTIST) bluetoothNowPlaying.artist = value;
    else if (attribute == AMS_TRACK_TITLE) {
      if (bluetoothNowPlaying.title != value) scrollStartedMs = millis();
      bluetoothNowPlaying.title = value;
    } else if (attribute == AMS_TRACK_DURATION) {
      bluetoothNowPlaying.durationSeconds = max(0.0f, value.toFloat());
    }
  }
  bluetoothNowPlaying.metadataChangedMs = millis();
  if (flags & AMS_FLAG_TRUNCATED) {
    // iOS marks oversized notifications. MTU 185 covers most metadata; the
    // displayed value remains a safe prefix rather than blocking the loop on
    // a synchronous extended-attribute read.
  }
}

void processBluetoothNowPlaying() {
  if (bluetoothFirmwareOperationIsolated) return;
  if (!bluetoothNowPlayingConfigured()) {
    if (bluetoothNowPlaying.initialized) shutdownBluetoothNowPlaying();
    return;
  }
  if (mediaStreamActive) return;
  if (!bluetoothNowPlaying.initialized) {
    initializeBluetoothNowPlaying();
    return;
  }
  if (bluetoothConnectFailedEvent) {
    bluetoothConnectFailedEvent = false;
    setBluetoothError("connection-attempt-failed", bluetoothConnectFailureStatus);
    bluetoothNowPlaying.nextAdvertisingRetryMs = millis() + BLE_ADVERTISING_RETRY_MS;
    if (bluetoothAdvertising && bluetoothAdvertising->isAdvertising()) {
      bluetoothNowPlaying.stage = BluetoothRuntimeStage::ADVERTISING;
    }
    logLine(String("BLE connection attempt failed status=") + bluetoothConnectFailureStatus);
  }
  if (bluetoothConnectedEvent) {
    bluetoothConnectedEvent = false;
    bluetoothNowPlaying.connHandle = bluetoothEventConnHandle;
    bluetoothNowPlaying.connected = true;
    bluetoothNowPlaying.secured = false;
    bluetoothNowPlaying.stage = BluetoothRuntimeStage::CONNECTED;
    clearBluetoothError();
    resetAmsHandles();
    clearBluetoothNowPlayingMetadata();
    if (bluetoothServer) {
      bluetoothServer->requestConnParams(bluetoothNowPlaying.connHandle,
                                          AMS_CONN_INTERVAL_MIN, AMS_CONN_INTERVAL_MAX,
                                          AMS_CONN_LATENCY, AMS_CONN_TIMEOUT);
    }
    int securityRc = 0;
    bluetoothNowPlaying.stage = BluetoothRuntimeStage::SECURING;
    if (!BLESecurity::startSecurity(bluetoothNowPlaying.connHandle, &securityRc) && securityRc != BLE_HS_EALREADY) {
      setBluetoothError("security-start-failed", securityRc);
      if (bluetoothServer) bluetoothServer->disconnect(bluetoothNowPlaying.connHandle);
    }
    logLine("BLE iPhone connected; securing AMS session");
  }
  if (bluetoothEncryptedEvent) {
    bluetoothEncryptedEvent = false;
    if (bluetoothEventConnHandle == bluetoothNowPlaying.connHandle) {
      bluetoothNowPlaying.secured = bluetoothEncryptionStatus == 0;
      if (bluetoothNowPlaying.secured) {
        ble_gattc_exchange_mtu(bluetoothNowPlaying.connHandle, nullptr, nullptr);
        bluetoothNowPlaying.nextDiscoveryMs = millis();
        logLine("BLE bond/encryption ready; AMS discovery queued");
      } else {
        setBluetoothError("pairing-encryption-failed", bluetoothEncryptionStatus);
        logLine(String("BLE pairing/encryption failed status=") + bluetoothEncryptionStatus);
        if (bluetoothServer) bluetoothServer->disconnect(bluetoothNowPlaying.connHandle);
      }
    }
  }
  if (bluetoothDisconnectedEvent) {
    bluetoothDisconnectedEvent = false;
    bluetoothNowPlaying.connected = false;
    bluetoothNowPlaying.secured = false;
    bluetoothNowPlaying.connHandle = BLE_HS_CONN_HANDLE_NONE;
    resetAmsHandles();
    clearBluetoothNowPlayingMetadata();
    if (!bluetoothNowPlaying.suspended) startBluetoothAdvertising();
    logLine("BLE iPhone disconnected");
  }

  AmsNotificationPacket packet;
  while (popAmsNotification(packet)) applyAmsNotification(packet);

  if (!bluetoothNowPlaying.connected && !bluetoothNowPlaying.suspended &&
      bluetoothAdvertising && !bluetoothAdvertising->isAdvertising() &&
      deadlineReached(millis(), bluetoothNowPlaying.nextAdvertisingRetryMs)) {
    startBluetoothAdvertising();
  }

  if (bluetoothNowPlaying.connected && bluetoothNowPlaying.secured &&
      !bluetoothNowPlaying.amsReady && !bluetoothNowPlaying.discoveryActive &&
      deadlineReached(millis(), bluetoothNowPlaying.nextDiscoveryMs)) {
    beginAmsDiscovery();
  }
}

bool bluetoothNowPlayingVisible() {
  return bluetoothNowPlayingConfigured() && bluetoothNowPlaying.initialized &&
         bluetoothNowPlaying.connected && bluetoothNowPlaying.amsReady &&
         !bluetoothNowPlaying.suspended && bluetoothNowPlaying.title.length() > 0;
}

bool bluetoothNowPlayingRuntimeActive() { return bluetoothNowPlaying.initialized && !bluetoothNowPlaying.suspended; }
bool bluetoothNowPlayingConnected() { return bluetoothNowPlaying.connected; }
bool bluetoothNowPlayingAmsReady() { return bluetoothNowPlaying.amsReady; }
bool bluetoothNowPlayingAdvertising() {
  return bluetoothNowPlaying.initialized && !bluetoothNowPlaying.suspended &&
         bluetoothAdvertising && bluetoothAdvertising->isAdvertising();
}
const char *bluetoothNowPlayingStage() { return bluetoothRuntimeStageName(bluetoothNowPlaying.stage); }
const char *bluetoothNowPlayingError() { return bluetoothNowPlaying.lastError; }
int bluetoothNowPlayingErrorCode() { return bluetoothNowPlaying.lastErrorCode; }
const char *bluetoothNowPlayingAddress() { return bluetoothNowPlaying.address.c_str(); }

const char *bluetoothNowPlayingTitle() { return bluetoothNowPlaying.title.c_str(); }
const char *bluetoothNowPlayingArtist() { return bluetoothNowPlaying.artist.c_str(); }
bool bluetoothNowPlayingIsPlaying() { return bluetoothNowPlaying.playbackState == 1; }
float bluetoothNowPlayingDurationSeconds() { return bluetoothNowPlaying.durationSeconds; }

float bluetoothCurrentElapsedSeconds() {
  float elapsedSeconds = bluetoothNowPlaying.elapsedSeconds;
  if (bluetoothNowPlaying.playbackRate != 0.0f) {
    elapsedSeconds += (millis() - bluetoothNowPlaying.playbackAnchorMs) / 1000.0f *
                      bluetoothNowPlaying.playbackRate;
  }
  if (bluetoothNowPlaying.durationSeconds > 0.0f) {
    elapsedSeconds = min(elapsedSeconds, bluetoothNowPlaying.durationSeconds);
  }
  return max(0.0f, elapsedSeconds);
}

#else

bool initializeBluetoothNowPlaying() {
  if (bluetoothNowPlayingConfigured()) logLine("BLE Now Playing unavailable in this firmware profile/build");
  return false;
}
bool shutdownBluetoothNowPlaying() { return true; }
bool isolateBluetoothForFirmwareOperation() { return true; }
void restoreBluetoothAfterFirmwareOperation() {}
void suspendBluetoothNowPlaying() {}
void resumeBluetoothNowPlaying() {}
void processBluetoothNowPlaying() {}
bool bluetoothNowPlayingVisible() { return false; }
bool bluetoothNowPlayingRuntimeActive() { return false; }
bool bluetoothNowPlayingConnected() { return false; }
bool bluetoothNowPlayingAmsReady() { return false; }
bool bluetoothNowPlayingAdvertising() { return false; }
const char *bluetoothNowPlayingStage() { return "unsupported"; }
const char *bluetoothNowPlayingError() {
#if MILESTONE_HAS_BLUETOOTH
  return "nimble-build-required";
#else
  return "switch-to-core-profile";
#endif
}
int bluetoothNowPlayingErrorCode() { return 0; }
const char *bluetoothNowPlayingAddress() { return ""; }
const char *bluetoothNowPlayingTitle() { return ""; }
const char *bluetoothNowPlayingArtist() { return ""; }
bool bluetoothNowPlayingIsPlaying() { return false; }
float bluetoothNowPlayingDurationSeconds() { return 0.0f; }
float bluetoothCurrentElapsedSeconds() { return 0.0f; }

#endif
