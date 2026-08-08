# MILESTONE Core — Codex Project Context

> 기준 소스: **MILESTONE Core v1.5.5**  
> 대상 하드웨어: **Waveshare ESP32-S3-Zero + SH1107 128×128 OLED**  
> 프로젝트 저장소: `CXITRON/MILESTONE-Core`  
> 문서 목적: Codex 또는 다른 코드 에이전트가 프로젝트의 기능, 구조, 설계 배경, 현재 동작과 수정 시 영향 범위를 빠르게 이해하기 위한 개발 컨텍스트 문서

---

## 0. 이 문서를 읽는 Codex에게

이 문서는 v1.5.5의 **현재 구현 상태를 설명하는 기준 문서**이다.

현재 구현을 반드시 그대로 보존해야 한다는 의미는 아니다. 필요하다면 리팩터링, 구조 변경, 기능 제거/복원, 동작 방식 변경을 할 수 있다. 다만 변경 전에는 현재 코드가 어떤 이유로 그렇게 구성되어 있는지, 어떤 기능들과 결합되어 있는지 확인하고 회귀 가능성을 함께 고려하는 것이 좋다.

특히 BOOT 버튼, OTA, Wi-Fi 상태기계, NVS 저장, 디스플레이 루프, 온도 보호는 서로 영향을 주기 때문에 한 부분을 단독으로 수정했을 때 다른 기능이 깨질 수 있다.

작업 시 권장 흐름:

1. 이 문서에서 관련 서브시스템의 현재 동작을 확인한다.
2. 실제 `MILESTONE_Core.ino` 구현을 다시 확인한다.
3. 변경 이유와 기대 동작을 명확히 한다.
4. 다른 상태기계와의 상호작용을 확인한다.
5. 컴파일뿐 아니라 실제 보드에서 검증할 항목을 제시한다.
6. 버전 변경이 필요한 작업이면 `FIRMWARE_VERSION`, Release manifest 흐름까지 함께 갱신한다.

---

# 1. 프로젝트 개요

**MILESTONE Core**는 ESP32-S3 기반의 개인용 데스크 디스플레이 펌웨어이다.

핵심 목적은 다음과 같다.

- D-Day 표시
- 현재 날짜/시간 표시
- 사용자 메시지 표시
- 여러 화면 자동 순환
- BOOT 버튼을 이용한 화면 전환 및 설정 진입
- 스마트폰/PC에서 접근 가능한 로컬 설정 포털
- 여러 Wi-Fi 네트워크 저장 및 자동 연결
- NTP 기반 시간 동기화
- 오프라인에서도 마지막 동기화 정보를 이용한 동작
- RGB LED를 이용한 시스템 상태 표시
- ESP32-S3 내부 온도 표시 및 보호 동작
- GitHub Release를 이용한 HTTPS OTA 펌웨어 업데이트
- 메모리/네트워크/업데이트 상태 등을 보여주는 기기 정보 화면

즉, 단순 OLED 시계 스케치가 아니라 여러 런타임 상태기계가 결합된 **소형 임베디드 애플리케이션**이다.

---

# 2. 기준 버전

현재 문서 기준 펌웨어:

```cpp
constexpr char FIRMWARE_VERSION[] = "1.5.5";
constexpr uint16_t CONFIG_VERSION = 6;
```

Firmware version과 Config/NVS schema version은 서로 다른 개념이다.

- `FIRMWARE_VERSION`: 사용자에게 보이는 펌웨어 버전 및 OTA 버전 비교에 사용
- `CONFIG_VERSION`: NVS 저장 구조 마이그레이션에 사용

기능 변경에 따라 펌웨어 버전만 올릴 수도 있고, NVS 구조가 바뀌는 경우 `CONFIG_VERSION`도 함께 올려 migration logic을 작성해야 한다.

---

# 3. 파일 구조

현재 프로젝트 루트:

```text
MILESTONE_Core/
├── MILESTONE_Core.ino
├── PortalPage.h
├── UpdateCertificates.h
├── README.md
├── .gitignore
└── tools/
    └── make-release.sh
```

## `MILESTONE_Core.ino`

펌웨어의 대부분이 들어 있는 메인 구현 파일이다.

담당 범위:

- 설정 구조체
- NVS 로딩/저장/migration
- OLED 렌더링
- BOOT 버튼
- Wi-Fi
- 설정 AP
- captive portal
- HTTP API
- NTP
- D-Day 계산
- 자동 화면 순환
- RGB 상태 LED
- 내부 온도 센서
- thermal protection
- OTA manifest 조회
- OTA download/verify/install
- runtime state machine

현재는 하나의 큰 `.ino` 파일에 통합되어 있다. 향후 기능이 더 커지면 display/network/update/config/input 등으로 분리할 수 있으나, Arduino 빌드 및 전역 상태 의존성을 고려해야 한다.

## `PortalPage.h`

설정 포털의 HTML/CSS/JavaScript 페이지가 들어 있다.

ESP32의 로컬 HTTP 서버가 이 페이지를 제공하고, 브라우저 JavaScript가 `/api/...` endpoint와 통신한다.

포털 UI를 변경할 때는 펌웨어의 HTTP API와 프론트엔드 필드명이 일치하는지 반드시 함께 확인해야 한다.

## `UpdateCertificates.h`

GitHub 및 GitHub Release asset HTTPS 검증에 사용할 Root CA bundle이다.

현재 여러 root CA가 포함되어 있다.

OTA 보안 설계상 TLS 인증서 검증이 활성화되어 있으며, 단순화를 목적으로 `setInsecure()`로 변경하면 MITM 방어가 사라진다. 보안 수준을 의도적으로 바꾸는 작업이라면 그 영향까지 명시해야 한다.

## `tools/make-release.sh`

OTA Release용 파일 생성 스크립트.

입력 BIN을 검사하고 다음을 생성한다.

```text
release/MILESTONE_Core.bin
release/MILESTONE_Core.json
```

manifest에는 다음이 포함된다.

```json
{
  "version": "1.5.5",
  "size": 123456,
  "sha256": "...",
  "notes": "..."
}
```

스크립트는 다음도 검사한다.

- version 형식
- source의 `FIRMWARE_VERSION`과 지정 버전 일치
- BIN보다 최신 source가 존재하는지
- ESP32 app image magic byte `0xE9`
- BIN 내부에 firmware version 문자열이 존재하는지
- 실제 파일 크기
- SHA-256

---

# 4. 하드웨어

## MCU

**Waveshare ESP32-S3-Zero**

ESP32-S3 기반.

현재 README 기준 확인된 보드 환경:

```text
esp32 by Espressif Systems: 3.3.11
Board: Waveshare ESP32-S3-Zero
USB CDC On Boot: Enabled
```

## OLED

SH1107 계열 128×128 monochrome OLED.

핀:

| OLED | ESP32-S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 8 |
| SCL | GPIO 9 |

코드:

```cpp
constexpr uint8_t PIN_SDA = 8;
constexpr uint8_t PIN_SCL = 9;
```

OLED I2C 주소는 부팅 시 다음 순서로 탐색한다.

```text
0x3C
0x3D
```

현재 U8g2 constructor:

```cpp
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C display(...);
```

이 선택에는 이유가 있다. 일반 `SH1107_128X128` profile은 해당 디스플레이에서 column offset 문제를 일으켜 왼쪽 32px이 오른쪽으로 wrap되는 문제가 있었고, 현재 profile은 그 현상을 피하기 위해 사용된다.

## BOOT 버튼

```cpp
constexpr uint8_t PIN_BOOT = 0;
```

`INPUT_PULLUP`으로 읽는다.

Pressed = LOW.

## RGB LED

내장 WS2812 1개.

```cpp
constexpr uint8_t PIN_RGB_LED = 21;
```

라이브러리:

```text
Adafruit NeoPixel
```

픽셀 형식:

```cpp
NEO_GRB + NEO_KHZ800
```

---

# 5. 권장 Arduino 설정

현재 README 기준:

```text
Board                 Waveshare ESP32-S3-Zero
USB CDC On Boot       Enabled
Partition Scheme      Minimal SPIFFS (약 1.9MB APP + OTA)
PSRAM                  Disabled 권장
```

## OTA partition

v1.5.0 이후 OTA가 있으므로 **2개의 OTA application slot**이 필요하다.

`Huge APP`처럼 OTA slot이 없는 partition은 현재 OTA 설계와 맞지 않는다.

## PSRAM

ESP32-S3FH4R2에는 PSRAM이 존재하지만 현재 MILESTONE firmware는 주요 버퍼를 내부 RAM에 배치하고 있어 필수는 아니다.

README에서는 안정성을 위해 Disabled를 권장한다.

다만 사용자가 실제 환경에서 PSRAM을 Enabled로 사용할 수 있으며, 향후 코드가 PSRAM을 명시적으로 활용하도록 수정될 수도 있다. 변경할 경우 heap/PSRAM allocation 정책과 OTA 메모리 조건을 재검토한다.

---

# 6. 외부 라이브러리 / ESP32 구성요소

별도 설치 라이브러리:

```text
U8g2
Adafruit NeoPixel
```

ESP32 Arduino Core에서 사용하는 주요 구성요소:

```text
WiFi
WebServer
DNSServer
Preferences
HTTPClient
NetworkClientSecure
Update
ESP SNTP APIs
mbedtls SHA-256
ESP OTA APIs
ESP temperature sensor APIs
```

---

# 7. 전반적인 런타임 구조

Arduino entry point는 매우 얇다.

```cpp
void setup() {
    Milestone::setupFirmware();
}

void loop() {
    Milestone::loopFirmware();
}
```

실제 애플리케이션은 `Milestone` namespace 안에 있다.

메인 loop 순서:

```cpp
processThermalProtection();
processButton();
processViewStateSave();
processNetwork();
processFirmwareUpdate();
processCycle();
processDisplay();
processLed();
delay(1);
```

이 순서는 시스템 특성을 이해하는 데 중요하다.

특히:

1. thermal protection이 먼저 실행됨
2. 입력 처리
3. 지연된 설정 저장
4. 네트워크 상태기계
5. OTA 상태기계
6. 화면 자동순환
7. OLED 렌더링
8. RGB LED
9. 1ms yield

대부분의 application work는 `millis()` 기반 non-blocking state machine 형태다.

일부 초기화/리셋/OTA 완료 경로에는 의도적인 짧은 `delay()`가 존재한다.

---

# 8. Runtime State Machine

```cpp
enum class RuntimeState : uint8_t {
  BOOTING,
  UNPROVISIONED,
  SETUP_AP,
  CONNECTING,
  TIME_SYNCING,
  RUNNING_ONLINE,
  RUNNING_OFFLINE,
  WIFI_SLEEP,
  ERROR_DISPLAY
};
```

의미:

## `BOOTING`

초기 부팅 단계.

## `UNPROVISIONED`

저장된 Wi-Fi가 없는 상태.

이후 설정 AP를 연다.

## `SETUP_AP`

설정용 AP/captive portal 실행 상태.

## `CONNECTING`

저장된 Wi-Fi에 연결 중.

## `TIME_SYNCING`

NTP 동기화 중.

## `RUNNING_ONLINE`

Wi-Fi 연결 및 시간 검증이 정상적으로 된 상태.

## `RUNNING_OFFLINE`

현재 인터넷/NTP를 사용할 수 없지만 display 기능은 계속 유지하는 상태.

## `WIFI_SLEEP`

사용자가 Wi-Fi 절전을 켠 경우 필요한 작업이 끝난 뒤 Wi-Fi radio를 꺼 둔 상태.

## `ERROR_DISPLAY`

OLED 초기화 실패 등의 display error 상태.

---

# 9. Config 구조

현재 `Config` 핵심 필드:

```cpp
struct Config {
  uint16_t version;

  SavedNetwork savedNetworks[8];
  uint8_t savedNetworkCount;

  TopMode mode;
  View lastView;

  String title;
  String target;
  String message;

  bool ddayTextStyle;
  bool afterComplete;

  bool messageLeft;
  bool messageScroll;
  uint8_t scrollSpeed;

  bool hour24;
  bool showSeconds;
  bool showChipTemperature;

  bool bootSync;
  uint32_t ntpPeriodSec;
  uint32_t ddayPeriodSec;
  uint32_t retryPeriodSec;

  bool wifiSleep;

  uint8_t brightness;
  uint8_t nightLevel;

  bool ledEnabled;
  uint8_t ledBrightness;
  uint8_t ledNightLevel;

  uint16_t nightStartMin;
  uint16_t nightEndMin;

  bool burninShift;
  uint16_t screenOffMin;

  uint8_t cycleMask;
  uint8_t cycleOrder[7];
  uint8_t cycleIntervalSec;
  uint8_t cycleIndex;

  uint64_t lastSync;
  int32_t lastDday;
};
```

기본값 중 주요 항목:

```text
title               2027 수능
target              2026-11-19
message             오늘도 한 칸 앞으로
showChipTemperature true
bootSync            true
ntpPeriodSec         21600 = 6시간
retryPeriodSec       300 = 5분
brightness           180
nightLevel           45
LED brightness       24
LED night            6
night start          22:00
night end            07:00
burninShift          true
screenOffMin         0
cycleMask            0x7F
cycleIntervalSec     8
```

---

# 10. NVS / Preferences

Preferences namespace:

```cpp
constexpr char PREFS_NS[] = "milestone";
```

NVS에는 다음 계열 데이터가 저장된다.

- display/config settings
- saved Wi-Fi list
- last manually selected view
- cycle index
- last synchronization data
- OTA result/status bookkeeping
- last successful OTA check time
- last discovered firmware version

## 설정 저장 특성

전체 설정 저장과 화면 상태 저장이 구분되어 있다.

수동 화면 전환은 즉시 NVS write를 반복하지 않고 다음 지연을 사용한다.

```cpp
constexpr uint32_t VIEW_SAVE_DELAY_MS = 1500;
```

즉, 연속적인 화면 변경 시 NVS write를 병합할 수 있도록 설계되어 있다.

이는 flash write 빈도를 줄이는 역할도 한다.

---

# 11. Config migration

현재 config schema:

```text
CONFIG_VERSION = 6
```

과거 버전에서 업데이트된 사용자의 설정을 유지하기 위해 migration logic이 존재한다.

README 기준 v1.5.0 설정에서 schema 6으로 이동할 때:

- 기존 Wi-Fi 유지
- 기존 6개 화면 cycle order 유지
- DEVICE_INFO view를 끝에 추가
- 기존 사용자의 자동 cycle에는 DEVICE_INFO를 강제로 추가하지 않음

새로운 config field를 추가할 때는 다음을 고려한다.

1. `Config` default
2. fresh installation default
3. old schema migration
4. missing NVS key 처리
5. factory reset
6. settings reset
7. portal serialization/deserialization

---

# 12. Wi-Fi 저장 시스템

최대 저장 네트워크:

```cpp
constexpr uint8_t MAX_SAVED_NETWORKS = 8;
```

각 항목:

```cpp
struct SavedNetwork {
    String ssid;
    String password;
};
```

동작:

- 최대 8개 저장
- 최근 성공한 Wi-Fi가 우선순위 상위
- 부팅 시 최근 성공 network부터 시도
- 필요하면 saved network scan 후 신호가 잡히는 후보를 순차 시도
- 새로운 네트워크 추가 시 오래된 항목 제거 가능
- 이미 저장된 SSID는 password 입력 없이 기존 password 재사용 가능
- portal에서 개별 삭제 가능

Wi-Fi password는 status API/OLED/serial log에서 노출하지 않는 것이 현재 방향이다.

---

# 13. 초기 설정 AP

저장 Wi-Fi가 없으면 AP를 연다.

SSID:

```text
MILESTONE-D1-SETUP
```

비밀번호:

- 매번 임의 생성
- 8자리
- OLED에 표시

기본 AP IP:

```text
192.168.4.1
```

AP timeout:

```cpp
constexpr uint32_t AP_TIMEOUT_MS = 10 * 60 * 1000;
```

즉 기본 10분.

설정 성공 후 portal을 즉시 닫지 않고 약간 유지한다.

```cpp
PORTAL_SUCCESS_HOLD_MS = 3000
```

---

# 14. Captive Portal

설정 AP에서 모바일 OS가 captive portal을 자동 감지하도록 여러 URL을 root page로 연결한다.

현재 route:

```text
/
/generate_204
/hotspot-detect.html
/ncsi.txt
```

알 수 없는 요청도 setup AP에서 들어온 경우 ESP32의 portal IP로 redirect한다.

---

# 15. HTTP API

현재 endpoint:

```text
GET  /
GET  /api/status
GET  /api/config
GET  /api/wifi/scan
POST /api/config
POST /api/wifi/test
POST /api/wifi/delete
POST /api/time/sync
POST /api/update/check
POST /api/update/install
POST /api/settings/reset
POST /api/reset
```

## `/api/status`

runtime 상태와 Wi-Fi/NTP/update/온도 등 실시간 정보를 제공한다.

## `/api/config`

GET: 현재 설정 반환  
POST: 화면/시간/LED 등의 설정 저장

## `/api/wifi/scan`

주변 Wi-Fi scan.

비동기 scan 흐름을 사용하여 scan 중에도 loop가 계속 실행되는 구조가 포함되어 있다.

## `/api/wifi/test`

새 Wi-Fi를 바로 저장하지 않고 먼저 연결 + NTP를 시험한다.

성공 후에만 credential을 확정 저장한다.

## `/api/wifi/delete`

저장된 Wi-Fi 개별 삭제.

## `/api/time/sync`

수동 NTP sync 요청.

## `/api/update/check`

GitHub 최신 Release manifest 확인.

## `/api/update/install`

확인된 새 firmware 설치.

## `/api/settings/reset`

표시 관련 설정만 default로 되돌린다.

Wi-Fi는 유지한다.

## `/api/reset`

factory reset.

Wi-Fi까지 제거한다.

---

# 16. Portal 접근 제어

설정 변경 API는 setup AP에서의 요청인지 확인하는 로직이 있다.

또한 session token이 cookie 기반으로 사용된다.

예:

```text
MILESTONE_TOKEN=<random session token>
```

portal security를 변경할 경우 captive portal 사용성, 동일 LAN 접근 가능성, CSRF/unauthorized request 범위 등을 함께 고려할 수 있다.

---

# 17. Wi-Fi 시험 후 저장 방식

새 network는 곧바로 NVS에 쓰지 않는다.

대략적인 흐름:

```text
사용자 SSID/password 제출
        ↓
pending credential 저장(RAM)
        ↓
Wi-Fi 연결 시도
        ↓
연결 성공
        ↓
NTP 시간 확인
        ↓
성공
        ↓
saved network에 upsert
        ↓
NVS 저장
```

실패하면 기존 Wi-Fi configuration을 복구하려고 시도한다.

이 구조는 잘못된 비밀번호를 저장해 기기가 접속 불능 상태가 되는 문제를 줄인다.

---

# 18. NTP / 시간

Timezone:

```cpp
constexpr char TZ_INFO[] = "KST-9";
```

목표 지역:

```text
Asia/Seoul / KST
```

유효한 시간이 있는지 판단하기 위한 minimum epoch:

```cpp
MIN_VALID_EPOCH = 2024-01-01 UTC
```

NTP timeout:

```text
18초
```

기본 periodic NTP interval:

```text
6시간
```

시간 sync가 실패하더라도 마지막 D-Day 정보 및 기존 clock state를 활용하여 offline display가 가능한 방향으로 설계되어 있다.

---

# 19. D-Day 계산

기본 target:

```text
2026-11-19
```

`YYYY-MM-DD` 문자열을 parsing한다.

마지막 계산 결과는 저장되어 offline fallback에 사용된다.

`ddayPeriodSec == 0`이면 local midnight 기준으로 다시 계산하는 의미다.

---

# 20. 화면 시스템

현재 실제 View는 7개다.

```cpp
enum class View : uint8_t {
  DDAY_TIME = 0,
  DDAY_MESSAGE = 1,
  MESSAGE_ONLY = 2,
  CLOCK_ONLY = 3,
  MESSAGE_CLOCK = 4,
  DASHBOARD = 5,
  DEVICE_INFO = 6
};
```

## 0 — `DDAY_TIME`

D-Day + 시간 중심.

현재 시간 영역은 스크롤하지 않고 고정 레이아웃을 사용하도록 개선된 상태다.

## 1 — `DDAY_MESSAGE`

D-Day + 사용자 메시지.

## 2 — `MESSAGE_ONLY`

사용자 문구만 표시.

긴 문구는 설정에 따라 scroll 가능.

## 3 — `CLOCK_ONLY`

시간 중심 화면.

## 4 — `MESSAGE_CLOCK`

메시지 + 시간.

현재 상단 title 역할은 “오늘 날짜” 표현으로 사용된다.

## 5 — `DASHBOARD`

D-Day, 시간, 기타 정보를 종합한 화면.

## 6 — `DEVICE_INFO`

기기 세부 정보.

---

# 21. TopMode

설정에서의 상위 표시 모드:

```cpp
enum class TopMode : uint8_t {
  DDAY_TIME,
  DDAY_MESSAGE,
  MESSAGE_ONLY,
  CLOCK_ONLY,
  MESSAGE_CLOCK,
  DASHBOARD,
  SELECTED_CYCLE,
  DEVICE_INFO
};
```

`SELECTED_CYCLE`은 독립적인 화면이 아니라 선택한 View들을 순환하는 mode다.

---

# 22. 자동 화면 순환

관련 설정:

```text
cycleMask
cycleOrder[7]
cycleIntervalSec
cycleIndex
```

기본:

```text
interval = 8 sec
```

`cycleIntervalSec == 0`이면 자동 전환은 하지 않는다.

다만 BOOT short press로 다음 화면으로 이동할 수 있다.

자동 전환은 `advanceView(false)`를 사용하므로 last view를 NVS에 저장하지 않는다.

수동 버튼 전환은 `advanceView(true)`를 사용한다.

---

# 23. BOOT 버튼 현재 동작

현재 v1.5.5에서 BOOT 버튼 입력은 **GPIO interrupt 기반이 아니라 loop polling 방식**이다.

```cpp
const bool rawPressed = digitalRead(PIN_BOOT) == LOW;
```

Debounce:

```cpp
BUTTON_DEBOUNCE_MS = 30
```

Raw state가 30ms 이상 안정적으로 유지된 경우 stable state로 인정한다.

버튼 action은 **release 시점의 hold duration**으로 결정한다.

현재 범위:

| 누른 시간 | 동작 |
|---|---|
| `< 50ms` | 무시 |
| `50ms ~ <1s` | 다음 화면 |
| `1s ~ <3s` | no-op |
| `3s ~ <8s` | 설정 AP 열기 |
| `>=8s` | factory reset 확인 화면 |

`1~3초` no-op 구간은 설정 AP가 실수로 열리는 것을 줄이기 위한 완충 구간이다.

현재 프로젝트의 요구에 따라 이 시간대나 기능 배치는 향후 변경될 수 있다.

---

# 24. 버튼 UI overlay

BOOT를 누르는 동안 `drawButtonOverlay()`가 현재 hold duration을 이용해 feedback을 표시한다.

즉 실제 action은 release에 확정되지만 누르는 중에는 사용자가 어느 단계에 있는지 볼 수 있다.

RGB LED도 `BUTTON_HOLD` 상태로 hold 단계에 따라 변화한다.

현재 README 설명:

```text
흰색 → 청록색 → 빨간색
```

---

# 25. Factory Reset 확인 절차

8초 이상 BOOT를 누른 뒤 놓으면 바로 삭제하지 않고 reset confirmation 상태로 들어간다.

관련 값:

```cpp
RESET_CONFIRM_WINDOW_MS = 5000
RESET_CONFIRM_HOLD_MS   = 3000
```

동작:

1. BOOT 8초 이상 hold 후 release
2. reset confirmation 화면
3. 5초 안에 다시 BOOT press 시작
4. 3초 이상 hold
5. release
6. factory reset

짧게 누르거나 제한 시간 안에 시작하지 않으면 cancel.

Factory reset은:

```cpp
WiFi.disconnect(true, true);
prefs.clear();
ESP.restart();
```

방식으로 Wi-Fi driver credential과 application Preferences를 제거한다.

---

# 26. Settings Reset과 Factory Reset의 차이

## Settings Reset

표시/시간/LED 등의 설정을 default로 되돌린다.

- 저장 Wi-Fi 유지
- 일반적으로 즉시 reboot하지 않음
- 마지막 시간 동기화 등 일부 runtime continuity data는 유지

## Factory Reset

- display/settings default
- saved Wi-Fi 제거
- ESP Wi-Fi credential 제거
- reboot

둘은 의도적으로 다른 기능이다.

---

# 27. 수동 View 저장

수동 BOOT short press로 이동한 화면은 last view로 저장된다.

하지만 즉시 `Preferences.put...`을 호출하지 않고 pending save를 둔다.

```text
VIEW_SAVE_DELAY_MS = 1500ms
```

의도:

- 빠른 연속 클릭 시 write merge
- NVS write 감소
- 불필요한 flash wear 감소

자동 cycle은 persist하지 않는다.

---

# 28. Device Info 화면

DEVICE_INFO에는 5페이지가 존재한다.

```cpp
DEVICE_INFO_PAGE_COUNT = 5
DEVICE_INFO_PAGE_MS = 5000
```

페이지는 5초마다 자동 변경된다.

다른 view로 나갔다가 DEVICE_INFO로 다시 들어가면 page 1부터 시작한다.

과거에는 BOOT hold로 device info page를 수동 변경하는 시도가 있었으나 v1.5.5에서는 제거되어 자동 5초 순환이다.

이는 현재 구현 상태에 대한 설명이며, 향후 입력 UX를 다시 설계할 수 있다.

표시 대상에는 다음이 포함된다.

### System

- firmware version
- uptime
- reset reason
- chip model
- chip revision
- CPU cores
- CPU MHz
- internal temperature

### Memory

- free heap
- minimum free heap
- largest allocatable block
- PSRAM 상태/여유

### Storage / OTA

- flash size
- running app size
- OTA free space
- partition 관련 값

### Network

- SSID
- RSSI
- Wi-Fi channel
- IP
- gateway
- MAC

### Time / Update

- NTP status
- last sync
- latest firmware state
- OTA result

---

# 29. OLED 렌더링

화면 크기:

```text
128 × 128
```

라이브러리:

```text
U8g2 full framebuffer
```

주요 helper:

```text
drawCenteredStr
drawCenteredUtf8
drawAlignedUtf8
drawScrollingUtf8
splitMessageLines
addEllipsisToFit
drawTopTitle
```

한글 렌더링을 위해 U8g2 Unicode font를 사용한다.

v1.5.x에서 여러 텍스트 중앙 정렬 문제를 수정한 이력이 있으므로 새로운 문자열/폰트/레이아웃을 추가할 때 실제 pixel width 기반 중앙 정렬 여부를 확인하는 것이 좋다.

---

# 30. 긴 문구 처리

메시지는 한 줄 또는 두 줄로 분리할 수 있다.

화면 폭을 초과하면 설정에 따라 scroll한다.

관련 config:

```text
messageLeft
messageScroll
scrollSpeed
```

scroll animation state는 `scrollStartedMs`를 사용한다.

View가 바뀌면 scroll 기준 시간을 재설정한다.

---

# 31. Burn-in shift

OLED burn-in 완화를 위해 화면 전체에 작은 offset을 줄 수 있다.

설정:

```text
burninShift = true
```

`getBurninOffset()`이 x/y offset을 계산한다.

새 화면을 구현할 때는 가능한 경우 이 offset을 동일하게 적용해야 전체 UI가 함께 이동한다.

---

# 32. 자동 화면 꺼짐

설정:

```text
screenOffMin
```

0이면 비활성.

마지막 interaction 이후 지정 시간이 지나면 OLED power save를 사용한다.

다음과 같은 중요 상태에서는 자동 screen-off가 억제된다.

- thermal safe mode
- boot splash
- portal
- reset confirmation
- BOOT hold
- OTA checking/downloading/verifying/reboot
- update prompt

사용자 interaction은 display를 wake한다.

---

# 33. 부팅 화면

부팅 시 MILESTONE splash가 약 3초 표시된다.

```cpp
BOOT_SPLASH_MS = 3000
```

부팅 처리 자체를 3초 blocking하는 것이 아니라 별도의 display state로 유지하는 구조다.

---

# 34. RGB LED 상태 시스템

`LedState`:

```cpp
enum class LedState : uint8_t {
  BOOTING,
  SETUP,
  CONNECTING,
  TIME_SYNCING,
  ONLINE,
  WIFI_SLEEP,
  WIFI_ERROR,
  NTP_ERROR,
  DISPLAY_ERROR,
  THERMAL_WARNING,
  TEMPERATURE_SENSOR_ERROR,
  THERMAL_CRITICAL,
  UPDATE_CHECKING,
  UPDATE_AVAILABLE,
  UPDATE_DOWNLOADING,
  UPDATE_ERROR,
  BUTTON_HOLD,
  RESET_WARNING
};
```

README의 사용자 의미:

| 표현 | 의미 |
|---|---|
| 민트 점등 | Wi-Fi/인터넷/NTP 정상 |
| 낮은 청록 | 정상 Wi-Fi 절전 |
| 청록 호흡 | 설정 AP |
| 파랑 호흡 | Wi-Fi 연결 |
| 보라 호흡 | NTP |
| 빨강 호흡 | Wi-Fi 실패/offline |
| 주황 호흡 | Wi-Fi 연결 + NTP 실패 |
| 자홍 빠른 호흡 | OLED 오류 |
| 주황 빠른 호흡 | 고온 경고 |
| 주황/빨강 더 빠른 호흡 | temperature sensor 오류 |
| 빨강 매우 빠른 호흡 | thermal safe mode |
| 파랑 2회 flash | update check |
| 흰색 2회 flash | update available |
| 보라 빠른 호흡 | firmware download/verify |
| 빨강 3회 flash | update error |

LED는 사용자가 disable할 수 있다.

밝기는 일반/야간이 따로 있다.

---

# 35. 야간 밝기

OLED와 LED 모두 day/night brightness 설정이 존재한다.

기본 야간:

```text
22:00 ~ 07:00
```

OLED:

```text
brightness
nightLevel
```

LED:

```text
ledBrightness
ledNightLevel
```

시간이 유효한 경우 현재 local time으로 야간 여부를 판단한다.

---

# 36. ESP32-S3 내부 온도

ESP32-S3 internal temperature sensor를 사용한다.

표시는 상단 status symbol 옆에 다음과 같은 형식이다.

```text
42C
```

사용자가 `showChipTemperature`를 끄면 평상시 숫자 표시는 숨겨진다.

하지만 thermal protection 자체는 계속 작동한다.

센서 자체가 오류 상태가 되면 `!TC` 같은 경고를 표시할 수 있다.

이 온도는 ambient temperature가 아니라 **chip die/internal sensor 값**이다.

---

# 37. 온도 센서 range 전환

정상 영역과 고온 영역에서 sensor range를 바꾼다.

현재 threshold:

```text
high-range enter: 85°C
high-range exit : 65°C
```

일반적으로 낮은 영역에서 정밀도가 더 적합한 range를 쓰고 고온 접근 시 높은 range로 전환한다.

---

# 38. Thermal Protection 정책

현재 threshold:

```cpp
THERMAL_WARNING_C             = 70°C
THERMAL_WARNING_CLEAR_C       = 65°C
THERMAL_THROTTLE_C            = 80°C
THERMAL_CRITICAL_C            = 90°C
THERMAL_THROTTLE_RECOVERY_C   = 75°C
THERMAL_SAFE_RECOVERY_C       = 70°C
```

## 70°C 이상

- OLED warning indicator
- RGB thermal warning

## 80°C 이상

CPU frequency를 다음으로 낮춘다.

```text
80 MHz
```

원래 CPU frequency는 부팅 시 기록한다.

## 90°C 이상이 10초 지속

Thermal Safe Mode.

```text
THERMAL_CRITICAL_HOLD_MS = 10 sec
```

Safe mode에서는 자체 발열 감소를 위해 Wi-Fi 등을 중지한다.

## 회복

throttle 상태:

```text
75°C 이하 30초
```

이면 원래 CPU clock 복구 시도.

safe mode:

```text
70°C 이하 60초
```

유지 시 정상 mode 복귀.

---

# 39. Temperature Sensor Fault 처리

연속 sensor read 실패 count:

```text
3회
```

이상이면 sensor fault로 본다.

현재 정책:

- warning
- CPU 80MHz throttle
- 약 60초 지속 시 sensor-fault safe mode

Sensor fault safe mode에서도 Wi-Fi/AP를 중지하여 시스템 부하를 낮춘다.

---

# 40. Thermal Safe Mode와 다른 시스템 관계

`thermalSafeMode == true`이면 network processing이 사실상 중단된다.

OTA check/install도 thermal warning/sensor fault/safe mode 조건에서 실행되지 않는다.

즉 thermal system은 다른 subsystem보다 우선순위가 높다.

온도 보호를 수정할 때는 다음과의 관계를 같이 확인한다.

- Wi-Fi
- captive portal
- update
- OLED status
- LED status
- CPU frequency
- recovery logic

---

# 41. Wi-Fi Sleep

사용자 설정:

```text
wifiSleep
```

이 기능은 Wi-Fi modem power-save 정도가 아니라 필요한 작업이 끝난 뒤:

```cpp
WiFi.disconnect(true, false);
WiFi.mode(WIFI_OFF);
```

형태로 radio를 끄는 흐름을 포함한다.

NTP periodic sync 또는 OTA check가 필요할 때 다시 saved Wi-Fi 연결 sequence를 실행한다.

---

# 42. OTA 개요

OTA source:

```text
https://github.com/CXITRON/MILESTONE-Core
```

Manifest latest URL:

```text
https://github.com/CXITRON/MILESTONE-Core/releases/latest/download/MILESTONE_Core.json
```

Binary는 manifest version에 해당하는 Release tag의 asset을 받는다.

기본 asset name:

```text
MILESTONE_Core.bin
```

---

# 43. OTA Update State

```cpp
enum class UpdateState : uint8_t {
  IDLE,
  CHECKING,
  AVAILABLE,
  DOWNLOADING,
  VERIFYING,
  READY_TO_REBOOT,
  CURRENT,
  ERROR_STATE
};
```

## `IDLE`

update activity 없음.

## `CHECKING`

manifest fetch.

## `AVAILABLE`

현재보다 새로운 semver firmware 발견.

## `DOWNLOADING`

binary stream download.

## `VERIFYING`

size/SHA-256 등 verify.

## `READY_TO_REBOOT`

새 app image 기록 성공, reboot 단계.

## `CURRENT`

서버 최신과 현재 version이 동일.

## `ERROR_STATE`

manifest/download/verify/install error.

---

# 44. OTA Version 비교

version format은 strict 3-part semantic numeric version 형태를 기대한다.

예:

```text
1.5.5
1.6.0
2.0.0
```

각 component를 숫자로 parsing해서 비교한다.

현재 parser는 prerelease (`1.6.0-beta`) 같은 형태를 지원하지 않는다.

Release version policy를 바꾸려면 firmware parser와 release tooling을 같이 바꿔야 한다.

---

# 45. OTA 자동 확인 주기

부팅:

```text
매 부팅마다 latest release 확인 시도
```

장시간 켜져 있을 때:

```text
마지막 정상 확인으로부터 7일
```

```cpp
UPDATE_WEEKLY_SEC = 7 days
```

실패 시 retry:

```text
6시간
```

```cpp
UPDATE_RETRY_MS = 6 hours
```

---

# 46. OTA 사용자 승인

새 version 발견 시 OLED prompt는 약 15초 유지된다.

```cpp
UPDATE_PROMPT_MS = 15000
```

그때:

```text
BOOT <1초 press-release → 설치 승인
15초 timeout → 이번에는 보류
```

Portal에서도 update check/install을 실행할 수 있다.

자동 check는 존재하지만 자동 install은 기본 정책이 아니다.

---

# 47. OTA TLS

`NetworkClientSecure`와 Root CA bundle을 사용한다.

Certificate verification을 위해 시스템 시간이 유효해야 한다.

그래서 사용자가 boot NTP sync를 끈 경우라도, 완전 power loss 후 시간이 유효하지 않으면 secure OTA check를 위해 NTP가 필요할 수 있다.

---

# 48. OTA 검증

manifest에서 최소 다음을 가져온다.

```text
version
size
sha256
notes
```

검증:

1. JSON field parsing
2. version 형식
3. SHA-256 64 hex chars
4. URL/TLS
5. expected binary size
6. streamed SHA-256
7. actual downloaded byte count
8. Update API write result

크기 또는 SHA가 맞지 않으면 새 firmware로 확정하지 않는다.

---

# 49. OTA Memory Safety

관련 threshold:

```cpp
UPDATE_MANIFEST_MAX_BYTES      = 2048
UPDATE_DOWNLOAD_BUFFER_BYTES   = 2048
UPDATE_MIN_FREE_HEAP           = 55000
UPDATE_MIN_LARGEST_BLOCK       = 32768
```

OTA 시작 전 memory condition을 확인한다.

과거 1.5.0에는 TLS/HTTP object와 4KB local buffer가 같은 loopTask stack에 놓이면서 OTA 시작 직후 stack 부족 재부팅 문제가 있었다.

그 이후 download buffer를 global/static 쪽으로 옮기고 크기를 줄이는 방향의 수정이 이루어졌다.

향후 OTA 코드를 다시 구조화하는 것은 가능하지만 ESP32 Arduino task stack과 TLS memory consumption을 고려하는 것이 중요하다.

---

# 50. OTA Download Stall

```cpp
UPDATE_DOWNLOAD_STALL_MS = 20000
```

일정 시간 동안 download progress가 없으면 실패 처리한다.

HTTP timeout:

```text
15초
```

---

# 51. OTA Reboot / Boot Validation

OTA installation이 새 firmware image를 기록한 뒤 reboot한다.

NVS에는 OTA 진행 stage/target/result를 기록하여 다음 boot에서 이전 OTA가 어디까지 진행됐는지 판단한다.

새 firmware boot 후:

```text
10초 정상 loop 유지
```

뒤 app valid marking을 시도한다.

```cpp
OTA_BOOT_CONFIRM_MS = 10000
```

함수:

```cpp
esp_ota_mark_app_valid_cancel_rollback();
```

Bootloader rollback이 활성화된 환경에서는 이 marking이 실제 rollback flow와 연동된다.

Arduino 기본 환경에서는 rollback 기능이 완전히 활성화되지 않을 수도 있다.

---

# 52. OTA failure 철학

다운로드/검증 실패 시 현재 실행 firmware는 유지된다.

새 app partition에 write가 완전히 성공하기 전에 running app을 교체하지 않는다.

따라서 network failure 자체가 곧 기존 firmware 손상으로 이어지지 않도록 구성되어 있다.

---

# 53. Update와 Wi-Fi Sleep의 관계

Wi-Fi sleep mode에서도 update check가 필요할 때:

1. saved Wi-Fi 연결
2. 필요 시 NTP
3. update check
4. prompt/install 또는 종료
5. 다시 Wi-Fi sleep

이를 위해 `wifiSleepDeferredForUpdate` 같은 상태가 있다.

---

# 54. Update와 Thermal 상태 관계

다음 상태에서는 update install/check를 시작하지 않도록 조건이 있다.

```text
thermal safe mode
temperature sensor fault
thermal warning
reset confirmation
Wi-Fi test 진행 중
portal 일부 상태
```

이러한 조건은 현재 안전 정책이다. 수정 시에는 flash write + TLS + Wi-Fi가 만드는 부하를 고려할 수 있다.

---

# 55. Update prompt와 BOOT 버튼 관계

일반 상태에서 short BOOT는 다음 view지만, update prompt가 보이는 동안에는 short press가 **install approval**로 우선 처리된다.

따라서 버튼 handler의 action priority를 변경하면 update UX에 영향을 줄 수 있다.

---

# 56. Display / OTA 상호작용

OTA 중에는 OLED가 update state/progress를 표시한다.

Auto cycle과 screen-off는 OTA critical state 중 억제된다.

사용자가 업데이트 도중 현재 화면을 저장하는 등의 불필요한 작업은 최소화하는 구조다.

---

# 57. Serial Log

baud:

```text
115200
```

log prefix:

```text
[MILESTONE]
```

부팅 때 다음 진단을 출력한다.

- reset reason
- free heap
- minimum heap
- largest block
- free PSRAM

Wi-Fi password는 log에 출력하지 않는다.

Arch Linux 예:

```bash
picocom -b 115200 /dev/ttyACM0
```

---

# 58. 초기 부팅 흐름

대략:

```text
ESP reset
 ↓
Serial / CPU freq / RGB init
 ↓
BOOT pin init
 ↓
Timezone/SNTP callback init
 ↓
Preferences open
 ↓
이전 OTA stage 검사
 ↓
Config load + migration
 ↓
OLED detect/init
 ↓
View initialize
 ↓
저장 Wi-Fi 존재?
 ├─ yes → saved Wi-Fi connection sequence
 └─ no  → setup AP start
```

OLED가 정상이라면 boot splash timer가 시작된다.

---

# 59. Main Loop 설계 의도

현재 application은 가능한 한 long blocking call을 피한다.

이유:

- BOOT button 반응성
- OLED refresh
- RGB animation
- portal HTTP handling
- DNS captive portal
- Wi-Fi connect timeout
- NTP timeout
- OTA progress
- thermal sampling

모두 동시에 진행되어야 하기 때문이다.

따라서 새로운 기능을 넣을 때 몇 초짜리 `delay()` 또는 긴 blocking network call을 loop 경로에 추가하면 다른 subsystem이 멈출 수 있다.

다만 간단한 초기화/재부팅 직전 시각 효과 등 제한된 구간에서는 blocking이 허용될 수 있다.

---

# 60. 주요 timing constant 요약

| 항목 | 현재값 |
|---|---:|
| Setup AP timeout | 10분 |
| Wi-Fi connect timeout | 20초 |
| NTP timeout | 18초 |
| Portal success hold | 3초 |
| Display refresh | 250ms |
| BOOT debounce | 30ms |
| View NVS save delay | 1.5초 |
| Factory reset confirm window | 5초 |
| Factory reset confirm hold | 3초 |
| LED refresh | 20ms |
| Wi-Fi scan timeout | 12초 |
| Temperature refresh | 5초 |
| Critical temp hold | 10초 |
| Throttle recovery hold | 30초 |
| Safe-mode recovery hold | 60초 |
| Sensor-fault safe hold | 60초 |
| OTA prompt | 15초 |
| Boot splash | 3초 |
| Device-info page | 5초 |
| OTA retry | 6시간 |
| OTA normal check | 7일 |
| HTTP timeout | 15초 |
| Download stall | 20초 |
| OTA boot confirmation | 10초 |

---

# 61. 상태 우선순위 관점

여러 UI/기능 상태가 동시에 발생할 수 있다.

대략 높은 우선순위로 생각할 수 있는 것:

```text
Thermal safe / sensor safety
        ↓
Factory reset confirmation
        ↓
OTA critical state / update prompt
        ↓
Setup portal
        ↓
BOOT hold feedback
        ↓
Normal view
```

실제 정확한 우선순위는 `processDisplay()`, `drawMainScreen()`, `processButton()`, `processFirmwareUpdate()`, `processThermalProtection()` 코드를 기준으로 확인한다.

---

# 62. 화면 중앙 정렬

v1.5.5에서는 OLED의 여러 텍스트가 화면 중앙에 맞도록 정렬 helper를 사용한다.

새 UI를 추가할 때 권장 방식:

```text
문자 개수로 center 계산 X
실제 U8g2 text pixel width 측정 O
```

특히 한글 UTF-8 text는 byte length와 glyph width가 일치하지 않는다.

`drawCenteredUtf8()` 등의 기존 helper를 우선 확인한다.

---

# 63. 상태바

상단에는 상태 icon과 optional chip temperature가 표시된다.

온도 표시가 켜져 있으면 상태 icon과 겹치지 않도록 position이 계산된다.

고온 또는 sensor fault는 사용자가 temperature display를 꺼도 안전 indicator를 보여줄 수 있다.

---

# 64. Version 1.5.5에서 특히 알아둘 현재 동작

### BOOT

- polling + 30ms debounce
- release 기반 hold 판단
- short press: view advance
- 1~3s: no-op
- 3~8s: setup AP
- 8s+: factory reset confirmation

### Device Info

- 5 pages
- 5초 자동 순환
- 별도 2초 BOOT page advance 없음

### View persistence

- manual view change만 저장
- auto cycle은 저장 안 함
- NVS save 1.5초 지연

### OTA

- GitHub Release manifest
- TLS certificate verification
- size + SHA-256
- 2KB global download buffer
- heap/largest-block 사전 검사
- 새 firmware boot 후 10초 stability confirmation

### Display

- 3초 non-blocking boot splash
- update CURRENT 상태 약 1초 표시
- 여러 중앙 정렬 수정 반영

---

# 65. 과거 문제와 배경

Codex가 현재 코드의 일부가 왜 복잡한지 이해하는 데 도움이 되는 이력이다.

## OTA 시작 직후 재부팅

과거 1.5.0 download 구현에서 loopTask stack 사용량이 커져 install 시작 직후 ESP가 재부팅되는 문제가 있었다.

원인 후보/확인 내용:

- TLS objects
- HTTPClient
- local download buffer
- stack pressure

현재는 download buffer를 2KB global buffer로 둔다.

## SH1107 화면 offset

일반 profile 사용 시 왼쪽 32pixel wrap 현상이 있어 현재 PIMORONI profile 사용.

## BOOT hold 안정성

버튼 hold 처리와 display/UI 상호작용을 여러 차례 수정한 이력이 있다.

v1.5.5는 polling debounce + release action 방식.

## Device Info manual page change

2초 hold 기능을 실험했지만 안정성 문제 때문에 제거되고 현재는 5초 auto-page 방식.

이 이력은 “앞으로 절대 바꾸지 말 것”을 의미하지 않는다. 같은 방식으로 돌아가려면 이전에 발생했던 문제도 함께 해결해야 한다는 참고다.

---

# 66. 코드를 수정할 때 영향도를 확인할 부분

## BOOT 관련 수정

확인:

- update prompt 승인
- reset confirmation
- display wake
- manual view persistence
- portal entry
- button overlay
- RGB feedback

## Wi-Fi 관련 수정

확인:

- first provisioning
- existing saved Wi-Fi boot
- multiple saved networks
- failed network fallback
- NTP
- OTA
- Wi-Fi sleep
- portal AP+STA coexistence

## Config 관련 수정

확인:

- fresh defaults
- migration
- portal JSON
- settings reset
- factory reset
- NVS backward compatibility

## OLED 관련 수정

확인:

- 128×128 bounds
- Korean glyphs
- burn-in offsets
- long text
- status icons
- chip temperature
- screen sleep
- OTA/reset/thermal overlay

## OTA 관련 수정

확인:

- partition scheme
- TLS
- correct clock
- heap
- largest block
- flash space
- manifest version
- binary size
- SHA-256
- interrupted OTA bookkeeping
- boot validation
- Wi-Fi sleep return

---

# 67. 기능 추가 시 권장 구현 스타일

현재 architecture와 자연스럽게 맞는 방식:

### 시간 기반 동작

`delay()`보다:

```cpp
if (elapsed(now, startedMs, interval)) {
    ...
}
```

### 비동기 상태

`enum class` + state variable + timestamp.

### 설정

`Config`에 추가 → default → load → migration → save → portal API → UI.

### display

별도 `drawXxx()` 함수.

### network action

가능하면 main loop가 계속 돌 수 있도록 state machine으로 분할.

---

# 68. Code Style

현재 특징:

- namespace `Milestone`
- `constexpr` 상수 적극 사용
- strongly typed `enum class`
- global runtime state 사용
- Arduino `String` 사용
- helper function 분리
- `millis()` rollover-safe helper 사용

대표 helper:

```cpp
bool elapsed(uint32_t now, uint32_t since, uint32_t period);
bool deadlineReached(uint32_t now, uint32_t deadline);
```

millis rollover를 고려한 비교 helper를 사용하는 것이 좋다.

---

# 69. 보안 관점

현재 기본 방향:

- Wi-Fi password API/log 노출 최소화
- OTA HTTPS 인증서 검증
- firmware SHA-256 검증
- manifest size 제한
- update version parsing
- setup AP access 확인
- session token cookie

향후 보안을 강화한다면 후보:

- portal request CSRF 강화
- API token validation 통일
- signed manifest
- firmware public-key signature
- secure boot / flash encryption

단, 이는 ESP32 provisioning 및 recovery 편의성과 trade-off가 있다.

---

# 70. 현재 OTA 신뢰 모델

현재 firmware authenticity는 실질적으로 다음에 의존한다.

```text
HTTPS certificate trust
+ GitHub repository/release account integrity
+ manifest SHA-256 integrity
```

SHA-256은 download corruption/tampering detection 역할을 하지만 manifest 자체가 공격자에게 함께 변조되면 별도 digital signature가 없으므로 완전한 cryptographic publisher signature는 아니다.

더 강한 모델이 필요하면 manifest 또는 binary signature 검증을 추가할 수 있다.

---

# 71. Release workflow

Arduino IDE:

```text
Sketch → Export Compiled Binary
```

그 후:

```bash
chmod +x tools/make-release.sh
./tools/make-release.sh 1.5.5 auto "release note"
```

생성:

```text
release/MILESTONE_Core.bin
release/MILESTONE_Core.json
```

GitHub Release:

```text
Tag: v1.5.5
Release title: MILESTONE Core v1.5.5
Assets:
  MILESTONE_Core.bin
  MILESTONE_Core.json
```

OTA URL이 asset name을 고정적으로 기대하기 때문에 file name 변경 시 firmware code도 같이 변경해야 한다.

---

# 72. 버전을 올릴 때

예: `1.5.5 → 1.5.6`

최소 확인:

```cpp
FIRMWARE_VERSION = "1.5.6";
```

그 후 clean compile/export.

Release script가 source version과 argument를 비교한다.

NVS schema가 바뀌지 않았다면 `CONFIG_VERSION`을 무조건 올릴 필요는 없다.

---

# 73. 테스트 체크리스트 — 일반

소스 변경 후 최소한 다음을 생각한다.

### Build

- Arduino compile 성공
- flash size 한계
- OTA slot size 한계
- RAM warning

### Boot

- 3초 splash
- OLED detect
- reset reason log
- saved config load

### UI

- 7 view
- 한글
- 중앙 정렬
- long text
- auto cycle
- manual cycle

### Input

- short BOOT
- 1~3s
- 3~8s
- >=8s
- reset confirmation

### Wi-Fi

- no saved Wi-Fi
- correct Wi-Fi
- wrong password
- multiple saved network
- network unavailable
- recovery

### NTP

- success
- timeout
- offline mode
- periodic resync

### Portal

- Android/iOS/desktop access
- captive portal
- scan
- save
- settings reset
- factory reset

### Thermal

- normal sensor read
- warning state
- throttle
- recovery
- simulated/read failure

### OTA

- current version
- new version
- manifest failure
- TLS failure
- SHA mismatch
- size mismatch
- download interruption
- successful reboot

---

# 74. 실제 보드 검증 시 특히 중요한 부분

에뮬레이션이나 compile만으로 확인하기 어려운 것:

```text
BOOT electrical behavior
GPIO debounce
OLED physical alignment
I2C stability
WS2812 timing/color perception
Wi-Fi AP/STA coexistence
Captive portal OS behavior
TLS RAM consumption
OTA partition write
actual reboot reason
ESP32 internal temperature sensor
CPU frequency throttle
USB CDC behavior
```

따라서 Codex가 큰 변경을 했다면 “컴파일 성공 = 완료”로 판단하기보다 실제 보드 테스트 항목을 함께 제시하는 것이 좋다.

---

# 75. README와 코드의 관계

README는 사용자 설치/사용 설명을 포함한다.

코드 변경 후 다음이 달라지면 README도 갱신해야 한다.

- version
- BOOT timing
- screen count
- setup process
- Wi-Fi policy
- temperature thresholds
- LED meanings
- OTA process
- Arduino settings
- partition scheme
- PSRAM recommendation
- Release workflow

---

# 76. Codex 작업 시 출력에 포함하면 좋은 내용

코드를 수정한 뒤 가능하면 다음을 함께 보고한다.

```text
1. 변경한 파일
2. 변경 이유
3. 실제 동작 변화
4. 기존 동작과의 차이
5. 잠재적 회귀 위험
6. 테스트 항목
7. 펌웨어 버전 변경 여부
8. NVS schema 변경 여부
9. README 수정 여부
10. Release note / commit message
```

이 프로젝트에서는 사용자가 릴리즈용 설명과 commit message도 자주 필요로 하므로 변경 완료 시 함께 준비하면 편리하다.

---

# 77. 현재 프로젝트에서 중요하게 취급되는 사용자 경험

기능 자체 외에도 다음이 중요하다.

- OLED 텍스트가 시각적으로 중앙 정렬될 것
- 버튼 입력이 예측 가능할 것
- 사용 중 갑작스러운 reboot가 없을 것
- Wi-Fi 설정 실패가 기존 설정을 망가뜨리지 않을 것
- OTA 실패가 현재 firmware를 깨뜨리지 않을 것
- 설정은 재부팅 후 유지될 것
- 자동 screen cycle과 manual selection의 의미가 구분될 것
- 설정 포털이 모바일에서도 정상 동작할 것
- 상태 LED만으로 대략적인 기기 상태를 알 수 있을 것
- 사용자가 온도 표시를 숨겨도 보호 기능은 유지될 것

---

# 78. 변경 가능성과 설계 자유도

현재 동작은 요구사항의 결과이지 불변 규칙이 아니다.

예를 들어 다음은 필요하면 다시 설계할 수 있다.

- BOOT hold timing
- polling → interrupt
- DEVICE_INFO page input 방식
- auto cycle 방식
- file/module 분리
- Wi-Fi state machine
- portal framework
- OTA transport
- NVS schema
- LED colors
- thermal threshold
- display layout

다만 변경 시에는 관련 subsystem과 이전 문제의 원인을 확인하여 **의도적인 변경인지 우발적 회귀인지 구분**할 수 있어야 한다.

---

# 79. 현재 핵심 상수 빠른 참조

```cpp
FIRMWARE_VERSION             "1.5.5"
CONFIG_VERSION               6
VIEW_COUNT                   7
MAX_SAVED_NETWORKS           8

PIN_SDA                      8
PIN_SCL                      9
PIN_BOOT                     0
PIN_RGB_LED                  21

OLED_ADDR_PRIMARY            0x3C
OLED_ADDR_SECONDARY          0x3D

BUTTON_DEBOUNCE_MS           30
VIEW_SAVE_DELAY_MS           1500
BOOT_SPLASH_MS               3000
DEVICE_INFO_PAGE_MS          5000

THERMAL_WARNING_C            70
THERMAL_THROTTLE_C           80
THERMAL_CRITICAL_C           90
THERMAL_THROTTLE_CPU_MHZ     80

UPDATE_PROMPT_MS             15000
UPDATE_DOWNLOAD_BUFFER_BYTES 2048
UPDATE_MIN_FREE_HEAP         55000
UPDATE_MIN_LARGEST_BLOCK     32768
OTA_BOOT_CONFIRM_MS          10000
```

---

# 80. 빠른 architecture map

```text
                         ┌─────────────────────┐
                         │     Config / NVS    │
                         └─────────┬───────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
              ▼                    ▼                    ▼
       ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
       │   Display   │      │   Network   │      │    Input    │
       │  7 Views    │      │ WiFi / NTP  │      │ BOOT GPIO  │
       └──────┬──────┘      └──────┬──────┘      └──────┬──────┘
              │                    │                    │
              │              ┌─────▼──────┐             │
              │              │   Portal   │             │
              │              │ HTTP + DNS │             │
              │              └─────┬──────┘             │
              │                    │                    │
              │              ┌─────▼──────┐             │
              │              │    OTA     │◄────────────┘
              │              │ GitHub TLS │
              │              └─────┬──────┘
              │                    │
              └─────────┬──────────┘
                        ▼
                ┌───────────────┐
                │ Runtime State │
                └───────┬───────┘
                        │
                ┌───────▼───────┐
                │ Thermal / LED │
                └───────────────┘
```

---

# 81. Codex가 먼저 읽으면 좋은 코드 위치

전체 코드를 처음 분석하는 경우 추천 순서:

1. 파일 상단 constants / enum / `Config`
2. `loadDefaults()`
3. `loadConfig()` / `saveConfigAll()`
4. `drawMainScreen()`과 각 draw function
5. `startPortal()` / `registerPortalRoutes()`
6. HTTP handlers
7. saved Wi-Fi connection functions
8. `processNetwork()`
9. update manifest / install functions
10. `processFirmwareUpdate()`
11. temperature read / `processThermalProtection()`
12. `handleButtonRelease()` / `processButton()`
13. `processCycle()`
14. `processDisplay()`
15. `setupFirmware()`
16. `loopFirmware()`

이 순서로 보면 전체 state flow를 비교적 빠르게 파악할 수 있다.

---

# 82. 작업 예시: 새로운 설정 하나 추가

예를 들어 `showSomething` 설정을 추가한다고 가정한다.

수정 후보:

```text
Config struct
loadDefaults()
saveConfigAll()
loadConfig()
old-version migration if needed
/api/config GET JSON
/api/config POST parser
PortalPage.h UI
runtime rendering logic
settings reset logic
README
```

단순히 `Config` field만 추가하면 portal/NVS와 실제 UI가 서로 불일치할 수 있다.

---

# 83. 작업 예시: 새로운 View 추가

현재 `VIEW_COUNT = 7`이라는 가정이 여러 곳에 사용된다.

새 view를 추가한다면 확인:

```text
VIEW_COUNT
View enum
TopMode if direct-selectable
cycleMask width
cycleOrder size/default
migration
parseCycleOrder
portal view list
drawMainScreen switch
manual advance
DEVICE_INFO assumptions
NVS serialization
README
```

8개를 넘어가면 `uint8_t cycleMask` 자체는 여전히 8bit까지 가능하지만 그 이상이면 자료형 확장이 필요하다.

---

# 84. 작업 예시: 버튼 동작 변경

변경 대상 후보:

```text
BUTTON_DEBOUNCE_MS
handleButtonRelease()
processButton()
drawButtonOverlay()
processLed() BUTTON_HOLD mapping
update prompt button handling
reset confirmation logic
README
```

현재 action이 release 시점에 확정된다는 점도 UX 변화에 영향을 준다.

---

# 85. 작업 예시: OTA server 변경

GitHub 이외 server로 바꾸려면:

```text
manifest URL
release asset URL composition
TLS root CA
redirect handling
manifest format
version policy
release script
README
```

서버가 HTTP redirect를 반환하는 경우 HTTPS downgrade를 허용할지 여부도 보안 정책으로 결정해야 한다.

---

# 86. 작업 예시: PSRAM 활용

향후 large portal page, image buffer, OTA buffer 등을 PSRAM에 옮기고 싶다면 확인:

```text
board PSRAM mode
psramFound()
heap_caps_malloc(... MALLOC_CAP_SPIRAM ...)
internal RAM이 필요한 DMA/driver buffer 여부
TLS allocation behavior
allocation failure fallback
free heap metrics의 의미 변화
README build setting
```

현재 코드는 PSRAM 없이도 동작하도록 설계된 상태다.

---

# 87. 작업 예시: portal을 항상 LAN에서 접근 가능하게 변경

현재 setup AP 중심 접근 제어를 변경한다면 고려:

```text
WebServer lifecycle
mDNS
STA IP
authentication
session token
CSRF
Wi-Fi password exposure
factory reset endpoint
OTA install endpoint
local-network threat model
```

특히 reset/update endpoint가 같은 LAN의 다른 기기에서 무단 호출되지 않도록 정책을 정해야 한다.

---

# 88. 알려진 구조적 기술 부채 후보

현재 코드가 동작하는 것과 별개로 장기적으로 검토할 수 있는 부분:

1. `MILESTONE_Core.ino`가 매우 큼
2. 전역 mutable state가 많음
3. UI/network/update 로직의 coupling
4. Arduino `String` 사용으로 heap fragmentation 가능성
5. 직접 작성한 lightweight JSON field parser
6. HTTP server가 synchronous `WebServer`
7. OTA trust가 signed manifest가 아닌 TLS + hash 수준
8. 많은 설정이 하나의 Config struct/NVS namespace에 집중

이들은 즉시 고쳐야 하는 버그라는 뜻은 아니다.

기능 안정성과 flash/RAM 제약을 고려하면서 단계적으로 개선할 수 있다.

---

# 89. 리팩터링 후보 구조

향후 분리 예:

```text
MILESTONE_Core.ino
ConfigStore.h/.cpp
DisplayManager.h/.cpp
InputManager.h/.cpp
NetworkManager.h/.cpp
PortalServer.h/.cpp
TimeManager.h/.cpp
ThermalManager.h/.cpp
UpdateManager.h/.cpp
StatusLed.h/.cpp
PortalPage.h
UpdateCertificates.h
```

하지만 Arduino IDE 프로젝트에서 `.cpp` 분리 시 include/declaration order와 ESP32/Arduino build semantics를 확인해야 한다.

---

# 90. 최종 요약

MILESTONE Core v1.5.5는 다음 8개 축으로 이해하면 된다.

```text
1. Display
   7개의 OLED View + 자동 순환 + 메시지 scrolling

2. Input
   GPIO0 BOOT polling/debounce/hold action

3. Configuration
   Preferences/NVS + migration + settings reset/factory reset

4. Network
   최대 8개 Wi-Fi + AP/STA + captive portal + fallback

5. Time
   KST NTP + offline D-Day continuity

6. Status/Safety
   WS2812 state LED + ESP32 internal thermal protection

7. Update
   GitHub Release + HTTPS + manifest + SHA-256 + OTA partition

8. Runtime coordination
   non-blocking millis-based state machines in one Arduino loop
```

Codex가 이 프로젝트를 수정할 때 가장 중요한 것은 현재 구현을 무조건 보존하는 것이 아니라 **한 subsystem의 변경이 다른 subsystem에 미치는 영향을 이해한 상태에서 의도적으로 변경하는 것**이다.

---

## Source of truth

이 문서는 설명용이다.

동작에 대한 최종 source of truth는 항상 현재 작업 tree의 다음 파일이다.

```text
MILESTONE_Core.ino
PortalPage.h
UpdateCertificates.h
tools/make-release.sh
```

문서와 코드가 불일치하면 먼저 코드의 실제 동작을 확인하고, 의도된 변경인지 판단한 뒤 문서도 함께 갱신한다.
