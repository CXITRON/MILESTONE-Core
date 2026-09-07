# MILESTONE v5.0 설계 및 구현 이력

> 상태: v5.0.1 구현·릴리스 완료. 이 문서는 설계 결정과 단계별 구현 이력을
> 보존하며, 현재 동작은 소스와 `README.md`, `v5/IMPLEMENTATION_STATUS.md`를
> 기준으로 한다.
>
> 작성 기준: 2026-09-01
>
> 보존된 v3.3.4의 동작이나 빌드 계약을 변경하는 문서가 아니다. v5의 현재
> 배포 기준은 5.0.1이며 이후 변경은 소스, 테스트와 릴리스 문서를 함께
> 동기화한다.

이 문서는 듀얼 ESP, 외장 microSD, RTC, 환경센서, 128×160 SPI TFT와 5버튼을 사용하는 MILESTONE v5.0의 개발 방향을 보존한다. 현재 `AGENTS.md`, `MILESTONE_PROJECT_CONTEXT.md`, `README.md`가 설명하는 v3.x 제품과 혼동하지 않는다.

대화와 작업 설명에서만 다음 별칭을 사용한다. 기존 코드, 로그, 파일명과 공개 문서는 구현 필요 없이 일괄 개명하지 않는다.

- **ESP MAIN**: GOOUUU ESP32-S3 N16R8, 16MB Flash / 8MB PSRAM
- **ESP ZERO**: Waveshare ESP32-S3-Zero, 4MB Flash / 2MB PSRAM

## 1. 제품 방향

- CORE, MEDIA, NOW를 ESP MAIN의 단일 런타임 펌웨어로 통합한다.
- 프로필 전환은 펌웨어 교체가 아니라 실행 중 모드 전환으로 처리한다.
- ESP ZERO는 BLE/AMS와 인터넷 작업, 필요 시 보조 연산을 맡는다.
- ESP MAIN은 화면, 버튼, SD, RTC, 환경센서, 설정 포탈, 미디어와 복구를 맡는다.
- 상태를 가진 연결은 안정성을 위해 소유권을 유지하고, 독립적인 인터넷 작업은 두 ESP 사이에서 동적으로 배정한다.
- 실시간 브라우저 스트리밍은 제거하고 SD 로컬 미디어 재생에 집중한다.
- SD는 미디어, 앨범 아트, 상세 로그, Stable/Backup/Recovery 펌웨어 저장소로 사용한다.
- 선택 장치나 파일 하나의 실패가 CORE, 버튼, 화면, 복구 기능을 막지 않게 한다.

## 2. 하드웨어 배선 기준

### 2.1 전원 원칙

보조배터리의 두 USB 출력으로 ESP MAIN과 ESP ZERO를 각각 공급한다.

```text
보조배터리 USB 1 -> ESP MAIN USB-C
보조배터리 USB 2 -> ESP ZERO USB-C
```

- ESP MAIN의 5V와 ESP ZERO의 5V를 서로 연결하지 않는다.
- ESP MAIN의 3V3와 ESP ZERO의 3V3를 서로 연결하지 않는다.
- 두 ESP의 GND는 SPI 신호 기준을 위해 공통 연결한다.
- 주변장치 전원은 ESP MAIN에서만 공급한다.

### 2.2 GND 버스

```text
GND BUS
|- ESP MAIN GND
|- ESP ZERO GND
|- TFT GND
|- microSD GND
|- RTC GND
|- 환경센서 GND
|- PREV 버튼 GND
|- NEXT 버튼 GND
|- OK 버튼 GND
|- BACK 버튼 GND
`- MODE 버튼 GND
```

### 2.3 ESP MAIN 3V3 버스

```text
ESP MAIN 3V3 -> 3V3 BUS

3V3 BUS
|- TFT VCC
|- TFT LED
|- RTC VCC
`- 환경센서 VDD
```

microSD 전원은 검증한 모듈의 `V5 IN`/`VCC`에 ESP MAIN 5V를 직접 연결한다.

### 2.4 TFT

```text
TFT LED   -> ESP MAIN 3V3 BUS
TFT SCK   -> ESP MAIN GPIO12
TFT SDA   -> ESP MAIN GPIO11  (SPI MOSI)
TFT A0    -> ESP MAIN GPIO21  (DC)
TFT RESET -> ESP MAIN GPIO6
TFT CS    -> ESP MAIN GPIO47
TFT GND   -> GND BUS
TFT VCC   -> ESP MAIN 3V3 BUS
```

TFT LED는 3V3 직결이다. 소프트웨어 화면 톤은 지원하지만 이 배선에서는 하드웨어 백라이트 PWM을 사용하지 않는다.

### 2.5 버튼 5개

권장 전면 배치는 다음과 같다.

```text
PREV     NEXT
    OK
BACK     MODE
```

```text
PREV 신호 -> ESP MAIN GPIO4
NEXT 신호 -> ESP MAIN GPIO7
OK 신호   -> ESP MAIN GPIO15
BACK 신호 -> ESP MAIN GPIO5
MODE 신호 -> ESP MAIN GPIO14

각 버튼 반대편 -> GND BUS
```

모든 버튼은 active-low `INPUT_PULLUP`으로 사용하며 외부 풀업 저항을 추가하지 않는다. 4핀 택트스위치에서 상시 도통되는 두 다리는 같은 묶음이므로 GPIO와 GND는 서로 반대 묶음에 연결한다.

### 2.6 I2C 버스

RTC와 환경센서는 같은 I2C 버스를 병렬 공유한다.

```text
SDA BUS: ESP MAIN GPIO41 + RTC SDA + 환경센서 SDA
SCL BUS: ESP MAIN GPIO42 + RTC SCL + 환경센서 SCL

RTC VCC      -> 3V3 BUS
RTC GND      -> GND BUS
환경센서 VDD -> 3V3 BUS
환경센서 GND -> GND BUS
RTC SQW/32K  -> 미연결
```

환경센서 측정부는 내부 발열 영향을 줄이기 위해 기기 상단 외부로 노출한다.

### 2.7 microSD

ESP MAIN에서 외부 장치에 사용할 수 있는 두 하드웨어 SPI 호스트를 효율적으로 사용하기 위해 TFT와 microSD가 SCK/MOSI를 공유한다. 각 장치의 CS는 분리하며 펌웨어가 SPI transaction으로 접근을 직렬화한다. MAIN-ZERO 링크는 나머지 하드웨어 SPI 호스트를 전용으로 사용한다.

```text
microSD CS   -> ESP MAIN GPIO10
microSD SCK  -> ESP MAIN GPIO12  (TFT SCK와 공유)
microSD MOSI -> ESP MAIN GPIO11  (TFT MOSI와 공유)
microSD MISO -> ESP MAIN GPIO13
microSD VCC  -> ESP MAIN 5V
microSD GND  -> GND BUS
```

TFT에는 MISO가 없으므로 SD MISO는 GPIO13에만 연결한다. TFT CS(GPIO47)와 SD CS(GPIO10)는 사용하지 않는 장치를 항상 HIGH로 유지한다. GPIO41/42는 I2C로 사용하고, TFT와 SD의 공용 SPI는 GPIO11/12를 사용한다.

### 2.8 MAIN-ZERO SPI

```text
ESP MAIN GPIO8  -> ESP ZERO GPIO9   : CLOCK
ESP MAIN GPIO9  -> ESP ZERO GPIO8   : MAIN -> ZERO DATA
ESP MAIN GPIO16 <- ESP ZERO GPIO10  : ZERO -> MAIN DATA
ESP MAIN GPIO17 -> ESP ZERO GPIO7   : CHIP SELECT
ESP MAIN GPIO18 <- ESP ZERO GPIO6   : READY / IRQ
ESP MAIN GND    <-> ESP ZERO GND    : COMMON GND
```

- 두 ESP 사이에는 위 신호 5개와 GND만 연결한다.
- UART 보조선은 추가하지 않는다.
- 두 보드의 GPIO0은 BOOT 용도로 외부 연결하지 않는다.
- ESP MAIN GPIO45는 VDD_SPI 스트래핑 핀이므로 외부 장치에 사용하지 않는다.
- ESP MAIN GPIO43/44는 시리얼 로그와 복구용으로 비워둔다.
- ESP ZERO GPIO21은 내장 RGB LED용이므로 사용하지 않는다.

## 3. 소프트웨어 역할 분담

### 3.1 ESP MAIN

- CORE/MEDIA/NOW 통합 런타임
- TFT 렌더링과 5버튼 UI
- SoftAP와 설정 포탈
- SD, RTC, 환경센서
- 사진, 영상, 앨범 아트 표시와 파일 관리
- 설정 저장과 진단
- OTA 설치, 내부 안전모드와 SD 복구
- 작업 조정기가 지정한 경우 인터넷 STA 작업

### 3.2 ESP ZERO

- iPhone BLE/AMS 연결과 음악 상태 수신
- 저장된 Wi-Fi STA 연결
- HTTP, NTP, 릴리스 확인과 다운로드
- MAIN에 앨범 아트와 OTA 데이터 전달
- 필요 시 보조 연산

ESP ZERO가 실패해도 ESP MAIN의 화면, 버튼, CORE, SD와 안전모드는 계속 작동한다. 인터넷과 NOW BLE만 제한한다.

## 4. MAIN-ZERO 통신 계약

SPI 메시지는 최소한 다음 필드를 갖는다.

```text
protocol version
message type
request/lease id
sequence number
payload length
payload
CRC
```

지원 동작:

- ACK와 제한된 재전송
- 타임아웃과 세션 재동기화
- heartbeat
- 중복 요청 제거
- 이전/새 펌웨어 사이 프로토콜 버전 협상
- AP, BLE, STA, OTA, SD, 네트워크 부하 상태 공유

공유 상태 예:

```text
AP_IDLE / AP_ACTIVE
BLE_IDLE / BLE_ACTIVE
STA_IDLE / STA_ACTIVE
OTA_ACTIVE
SD_BUSY
NETWORK_BUSY
```

## 5. 동적 무선 작업 조정

연결을 유지해야 하는 역할은 함부로 옮기지 않는다.

```text
BLE 연결 주체 -> ESP ZERO
활성 설정 AP  -> ESP MAIN
화면/SD/센서  -> ESP MAIN
```

다음 작업은 현재 부하에 따라 MAIN 또는 ZERO가 맡는다.

- HTTP 요청
- 앨범 아트 다운로드
- NTP
- 업데이트 확인과 OTA 파일 다운로드
- 서버 상태 확인
- 독립적인 보조 연산

예상 스케줄:

```text
MAIN 포탈 사용 중:
  MAIN = AP
  ZERO = 인터넷

ZERO가 BLE로 바쁘고 MAIN AP 접속자가 없음:
  MAIN이 AP를 잠시 종료
  MAIN이 저장 Wi-Fi로 인터넷 작업
  완료 후 AP 복구

MAIN 포탈과 ZERO BLE가 모두 바쁨:
  사용자 직접 요청만 처리
  자동 앨범 아트와 백그라운드 작업은 대기

OTA:
  필요하면 ZERO BLE를 일시 중지
  인터넷 다운로드와 무결성 작업을 우선
```

우선순위:

```text
1. 과열, watchdog, OTA, 복구 무결성
2. 버튼과 화면 반응
3. 사용자 직접 요청
4. SD 기록과 파일 검증
5. BLE/AMS 음악 상태
6. 자동 앨범 아트
7. 업데이트 확인, 로그, 캐시 정리
```

BLE는 정상 NOW 사용 중 우선 처리한다. 비정상 고부하에서는 중복 알림 병합, 진행 위치 갱신 완화, 광고 일시 중지 순으로 단계적으로 낮추며, 연결 해제는 OTA/복구 같은 중요 작업의 최후 수단으로만 사용한다.

### 5.1 보드별 독립 LED 정책

각 보드의 기존 RGB 상태 LED 정책은 유지하되 LED 상태를 MAIN-ZERO 사이에서 동기화하거나 복제하지 않는다.

- ESP MAIN의 RGB LED는 ESP MAIN이 직접 수행 중인 AP, SD, 화면, 복구, OTA 설치 등 로컬 상태만 표시한다.
- ESP ZERO의 RGB LED는 ESP ZERO가 직접 수행 중인 BLE, 인터넷 연결, 다운로드, ZERO OTA 등 로컬 상태만 표시한다.
- MAIN이 ZERO에 인터넷 작업을 요청해도 MAIN LED가 ZERO의 인터넷 상태를 대신 표시하지 않는다.
- 동적 작업 분배로 인터넷 담당이 바뀌면 실제 작업을 수행하는 보드의 LED만 해당 정책에 따라 변한다.
- 상대 보드 LED의 색상이나 점멸을 제어하는 SPI 명령은 만들지 않는다.
- PWR LED와 USB-UART TX LED는 하드웨어 고유 동작을 유지하며 상태 정책 대상으로 사용하지 않는다.

이 정책으로 사용자는 어느 보드가 BLE 또는 인터넷 작업을 실제로 처리 중인지 두 보드의 LED를 각각 보고 구분할 수 있다.

## 6. 통합 프로필과 5버튼 UX

ESP MAIN의 한 펌웨어에서 다음 프로필을 전환한다.

```text
CORE
MEDIA
NOW
```

전환 시 현재 프로필의 작업, 파일과 메모리를 안전하게 정리한 뒤 새 프로필을 초기화한다. 펌웨어 다운로드나 재설치는 하지 않는다. 마지막 프로필은 NVS에 저장한다.

버튼 의미:

```text
PREV -> 이전 항목/파일
NEXT -> 다음 항목/파일
OK   -> 선택, 재생/일시정지
BACK -> 취소, 뒤로, 재생 종료
MODE -> 프로필 메뉴 열기/닫기
```

프로필 메뉴:

```text
CORE
MEDIA
NOW
Wi-Fi 설정 AP
RESTART
SAFE MODE
EXIT
```

현재 프로필을 다시 선택하면 프로필 메뉴를 닫는다.
전면 MODE 메뉴의 `Wi-Fi 설정 AP`에서 설정 AP를 열 수 있고, AP 화면에서는
MODE 또는 BACK으로 닫는다. 기판의 BOOT 버튼은 같은 기능의 보조 진입 수단으로
유지한다.

## 7. 공통 UI와 CORE

- v3의 큰 시계, D-day, 문구, 구분선, NOW/MEDIA 화면 구성과 U8g2 글꼴을
  128×128 본문에서 유지한다. 듀얼 보드 통합은 내부 런타임 경계만 바꾸며
  사용자 화면 디자인을 단순 텍스트 목록으로 대체하지 않는다.
- 우상단 상태 아이콘을 의미와 색상으로 명확히 구분한다.
- Wi-Fi 절전/대기 표시는 가운데가 빈 원을 사용한다.
- 화면 전체 톤, 대비와 색감을 설정 포탈에서 조절한다.
- 실제 TFT 백라이트 밝기 제어는 하지 않는다.
- 128×128 본문 위 16픽셀 띠에는 프로필 약어와 AHT20 외부 온도/습도를 표시한다.
- 128×128 본문 아래 16픽셀 띠에는 ESP MAIN과 ESP ZERO의 내부 다이 온도를 각각 표시한다.
- 상하단 상태 띠는 본문 프로필 화면과 독립적으로 부분 갱신해 전체 화면 깜빡임을 피한다.
- 기존 128×128 본문과 장치 정보 화면의 칩 온도 표기는 제거하고, 칩 온도는 하단 상태 띠에서만 표시한다.
- CORE의 시간, 날짜, 문구, 일정 등 요소별 색상을 설정한다.
- 설정 포탈에서 현재 색상을 즉시 미리보기한다.
- 시스템 정보에 PSRAM 전체/사용/여유 용량을 표시한다.
- RTC와 환경센서 정보를 표시한다.
- 설정 페이지 자동 전환을 제거하고 OK로 다음 페이지를 연다.
- PREV/NEXT는 일반 화면 이동에 사용한다.

### 7.1 환경센서 소프트웨어

실기기에서 확인된 AHT20 주소 0x38을 사용해 외부 온도와 습도를 측정한다. 조합 모듈에 표기된 BMP280은 실기 I2C 버스에서 응답하지 않았고 제품 기능에서도 기압을 사용하지 않는다. AHT20 측정 CRC-8과 유효 범위를 검사하며, 내부 ESP 다이 온도와 외부 환경센서 온도는 명칭과 저장 항목을 분리한다.

상하단 상태 띠의 기본 표기 예시는 다음과 같다. 실제 글꼴 폭에 맞춰 단위와 소수점 자릿수를 축약한다.

```text
상단: C 23.4° 48% 1013h
하단: M 61° | Z 55°
```

- `C/M/N`은 현재 CORE/MEDIA/NOW 프로필 약어다.
- 상단 온도/습도는 외부 AHT20 값이다.
- `M`은 ESP MAIN, `Z`는 ESP ZERO의 내부 다이 온도다.
- ESP ZERO 온도는 SPI 상태 패킷으로 MAIN에 전달한다.
- 센서 미응답이나 ZERO 단절은 `--`로 표시한다.
- 정상/경고/위험 임계값에 따라 숫자 색상을 일반색/주황/빨강으로 구분한다.
- ESP 내부 다이 온도는 주변 기온이 아니라 칩 열 상태 진단값으로 취급한다.

기본 동작:

- 외부 온도와 습도는 상단 상태 띠와 설정 포탈 상태에서 제공하고 128×128 본문에는 중복 표시하지 않는다.
- RTC 시각을 기준으로 SD에 선택적 환경 이력을 기록한다.
- 순간 노이즈를 줄이기 위해 유효 범위 검사와 짧은 이동 평균/필터를 적용한다.
- 센서 자체 발열을 줄이도록 필요한 주기에만 측정한다.
- 마지막 정상값, 측정 시각, stale 상태와 I2C 오류 횟수를 진단에 제공한다.

표시 위치 변경과 온도 안전 정책은 분리한다. 기존 경고, 감속, 보호, 진단 로직은 유지하며 두 ESP가 각각 자기 다이 온도를 로컬에서 감시한다. ESP ZERO는 경고 단계와 온도를 SPI로 MAIN에 전달하고, 어느 한쪽이 위험 임계값에 도달하면 해당 보드가 우선 자기 작업을 제한한다. 온도 센서 오류는 오류 상태를 표시하되 임의의 정상값으로 간주하지 않는다.

설정 포탈 항목:

```text
환경센서 사용 여부
온도 단위
온도/습도 보정값
화면 갱신 주기
SD 기록 사용 여부와 기록 간격
표시 항목과 경고 임계값
센서 재검색/진단
```

SD 기록은 일자별 파일로 분리하고 임시 기록 후 안전하게 확정한다. 기록 실패나 센서 미응답은 전체 펌웨어 롤백 사유가 아니며, 마지막 값에 `STALE`을 표시한 뒤 제한된 간격으로 재검색한다. 잘못된 값은 화면, 로그와 경고 판정에 반영하지 않는다.

## 8. MEDIA

실시간 스트리밍을 완전히 제거한다.

제거 대상:

- WebSocket/HTTP 실시간 프레임 전송
- 브라우저 실시간 캡처와 JPEG 인코딩
- 스트리밍 버퍼, ACK, RTT 진단
- 스트리밍 전용 페이지와 실행 모드

MEDIA는 SD 로컬 미디어 전용으로 만든다.

```text
MEDIA
|- PHOTO -> 사진 목록 -> 선택 -> 전체 화면
`- VIDEO -> 영상 목록 -> 선택 -> 재생
```

지원 기능:

- 목록, 선택, 이전/다음
- 재생/일시정지와 BACK 종료
- 반복 재생과 정렬
- 컬러/흑백 선택
- PSRAM 선읽기 버퍼
- 손상 파일 격리

일반 MP4/H.264를 ESP에서 직접 재생하지 않는다. PC에서 128×160 TFT에 맞는 전용 로컬 영상 포맷으로 변환해 SD에 저장한다.

## 9. NOW와 앨범 아트

- ESP ZERO가 BLE/AMS 음악 정보를 수신해 MAIN에 전달한다.
- 연결 해제 후 광고 복구, 재연결 후 현재 음악 정보 재요청을 보장한다.
- AMS READY나 광고 상태 고착을 방지한다.
- 앨범 아트는 항상 컬러로 표시한다.
- SD 캐시를 먼저 확인하고 이미지가 없을 때만 인터넷을 사용한다.

앨범 아트 기본 정책:

```text
저장 위치: ESP MAIN의 SD
기본 한도: 2GB
정리 방식: LRU
최소 SD 여유 공간: 1GB
```

사용자 관리 기능:

- 앨범, 아티스트, 곡 검색
- 저장 이미지 미리보기
- JPG/PNG 직접 업로드와 교체
- 자동 이미지 삭제와 서버 재요청
- 사용자 이미지 고정
- 자동 서버 요청 차단
- 캐시 사용량, 한도, 이미지 수 확인

이미지 상태:

```text
AUTO    서버 자동 저장
CUSTOM  사용자 지정, LRU 삭제 제외
BLOCKED 서버 요청 금지
MISSING 이미지 없음
```

설정 포탈의 `이미지 다시 받기` 흐름:

```text
사용자 -> MAIN AP 포탈
MAIN -> SPI 요청 -> ZERO
ZERO -> 저장 Wi-Fi로 서버 다운로드
ZERO -> SPI 스트림 -> MAIN
MAIN -> SD 임시 파일
MAIN -> 크기/형식/해시/디코딩 검증
MAIN -> 기존 이미지와 원자적 교체
```

사용자가 이미지를 직접 업로드할 때는 휴대폰에서 MAIN AP를 통해 MAIN SD로 바로 보낸다.

## 10. OTA와 자동 동반 업데이트

인터넷 작업을 맡은 ESP가 릴리스 묶음을 내려받고 MAIN이 SD 임시 영역에 보존한다.

```text
다운로드
-> MAIN/ZERO BIN과 manifest 저장
-> 대상/버전/크기/SHA-256/서명 검증
-> MAIN 비활성 OTA 파티션 설치
-> MAIN 시험 부팅과 자체검사
-> MAIN 확정
-> 필요할 때 ZERO BIN을 SPI 전송
-> ZERO 비활성 OTA 파티션 설치
-> ZERO 시험 부팅과 SPI 자체검사
-> 전체 성공 후 SD Stable 승격
```

- ZERO 변경이 없으면 MAIN만 업데이트한다.
- 새 MAIN은 업데이트 전 ZERO와도 통신 가능해야 한다.
- ZERO 업데이트 실패 시 MAIN은 유지하고 ZERO는 이전 OTA로 롤백한다.
- ZERO의 부트로더나 파티션 테이블까지 손상되면 USB 유선 복구가 필요하다.

## 11. 부트로더 롤백과 내부 안전모드

ESP-IDF의 실제 A/B 롤백을 사용한다.

```text
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

Arduino 코어가 새 앱을 즉시 승인하지 않도록 `verifyRollbackLater()`를 재정의하고, 자체검사 뒤에만 다음 중 하나를 호출한다.

```text
성공: esp_ota_mark_app_valid_cancel_rollback()
실패: esp_ota_mark_app_invalid_rollback_and_reboot()
```

SD Stable/Recovery 다운그레이드를 허용하기 위해 eFuse 기반 `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`은 사용하지 않는다.

내부 플래시에는 네트워크 없이 부팅 가능한 최소 안전모드를 둔다.

- TFT 복구 메뉴와 5버튼 입력
- SD와 펌웨어 검증
- OTA 파티션 복원
- 기본 하드웨어 진단
- 정상/이전 펌웨어 선택

SD 펌웨어 계층:

```text
Stable   충분히 실사용 검증된 버전
Backup   업데이트 직전 버전
Recovery 자동으로 덮어쓰지 않는 비상 복구 버전
```

복구 순서:

```text
현재 펌웨어
-> 이전 내부 OTA
-> 내부 안전모드
-> SD Stable
-> SD Recovery
-> USB 유선 복구
```

안전모드와 Stable 전환은 TFT와 버튼으로 완결한다. 별도 복구 AP를 자동으로 열지 않는다.

## 12. SD 저장 구조 초안

```text
/firmware/
|- main/
|  |- stable/
|  |- backup/
|  `- recovery/
`- zero/
   |- stable/
   |- backup/
   `- recovery/

/media/
|- photo/
`- video/

/now/
|- art-cache/
`- art-index-a/b

/logs/
```

- 펌웨어와 인덱스는 임시 파일 기록, 검증, 원자적 이름 교체 순서로 갱신한다.
- Recovery는 자동 교체나 캐시 정리 대상이 아니다.
- 앨범 아트 LRU는 사진, 영상, 펌웨어, 설정과 로그를 삭제하지 않는다.

## 13. 장애 격리

다음 실패만으로 전체 펌웨어를 롤백하지 않는다.

- SD 미삽입 또는 마운트 실패
- RTC/환경센서 미응답
- ZERO 일시적 통신 실패
- Wi-Fi/BLE 연결 실패
- 앨범 아트 서버/다운로드 실패
- 손상된 사진이나 영상

관련 기능만 비활성화하고 CORE, 화면, 버튼과 내부 안전모드를 유지한다.

## 14. 구현 단계

1. ESP MAIN/ESP ZERO 개별 보드와 전원 검증
2. TFT, 5버튼, I2C, SD 개별 드라이버 검증
3. SPI 패킷/CRC/재동기화/heartbeat 호스트 테스트
4. ZERO BLE/AMS와 Wi-Fi 인터넷 작업 조정
5. MAIN 단일 CORE/MEDIA/NOW 상태머신
6. SD 사진/영상 브라우저와 전용 영상 포맷
7. 앨범 아트 캐시, LRU와 사용자 관리 포탈
8. 동적 Radio Broker와 작업 임대
9. MAIN/제로 A/B OTA와 동반 업데이트
10. 내부 안전모드와 SD Stable/Recovery
11. 전원 차단, 손상 파일, 무선 단절, SPI 단절 회귀 시험
12. v5.0 문서 동기화와 통합 릴리스

## 14.1 하드웨어 제작 진행 기록

### 2026-09-05 — MAIN-ZERO SPI 배선 실기 검증 완료

- ESP MAIN과 ESP ZERO는 핀헤더에 장착하고 아래 여섯 가닥을 납땜했다.
  - MAIN GPIO8 -> ZERO GPIO9: SCK
  - MAIN GPIO9 -> ZERO GPIO8: MAIN에서 ZERO로 전송
  - ZERO GPIO10 -> MAIN GPIO16: ZERO에서 MAIN으로 전송
  - MAIN GPIO17 -> ZERO GPIO7: CS
  - ZERO GPIO6 -> MAIN GPIO18: READY
  - MAIN GND <-> ZERO GND: 공통 기준 전압
- 두 보드는 시험 중 각자 USB로 전원을 공급했고 5V와 3V3 전원선은 서로
  연결하지 않았다.
- ZERO에는 `tools/hardware-test-v5/esp-zero-test`, MAIN에는
  `tools/hardware-test-v5/esp-main-spi-test` 임시 펌웨어를 유선 업로드했다.
- SPI mode 0, 1MHz, 32바이트 packed 패킷으로 양방향 전송하고 magic,
  protocol version, packet type와 CRC-32를 검증했다.
- ZERO의 응답 sequence, nonce와 내부 온도도 MAIN에서 정상 수신했다.
- ZERO의 닫힌 USB CDC에 주기적으로 로그를 쓰면서 READY가 잠깐 멈추는 시험
  코드 문제가 확인되어 런타임 로그를 `availableForWrite()` 기준 비차단으로
  수정했다. 수정 후 8초 동안 약 80회가 중단이나 CRC 오류 없이 연속 통과했다.
- 따라서 현재 SCK, MOSI, MISO, CS, READY, 공통 GND 납땜은 전기적으로
  정상으로 판정한다. 최종 목표 클럭과 장시간 오류율 시험은 통합 펌웨어 단계에
  별도로 수행한다.
- 두 ESP에는 현재 제품 펌웨어가 아니라 위 SPI 점검용 임시 펌웨어가 설치돼 있다.

다음 제작 순서는 TFT/SD 공용 SPI, I2C 버스, 5버튼, 전원 버스다. 각 묶음을
완료할 때마다 무전원 상태에서 연속성과 3V3-GND/5V-GND 쇼트를 검사한다.

### 2026-09-06 — 전체 주변장치 배선 실기 검증 완료

- TFT와 microSD의 GPIO11 MOSI/GPIO12 SCK 공용 SPI 배선을 완료했다.
- TFT 색상·방향·갱신과 microSD 약 60,350MiB 카드의 마운트 및 512바이트
  임시 파일 쓰기·읽기·삭제를 통과했다.
- DS3231 RTC는 0x68, 모듈 EEPROM은 0x57에서 응답했고 시각 증가를
  확인했다. OSF=1은 초기 시각/백업 배터리 상태로 분리해 판정했다.
- 조합 모듈의 AHT20은 0x38에서 응답했으며 측정 CRC-8을 포함한 온도·습도
  읽기를 통과했다. 같은 모듈의 BMP280은 응답하지 않아 제품에서는 기압을
  사용하지 않고 AHT20 온습도만 사용하기로 확정했다.
- PREV GPIO4, NEXT GPIO7, OK GPIO15, BACK GPIO5, MODE GPIO14의 active-low
  입력을 모두 확인했다.
- 전체 주변장치가 연결된 상태에서도 MAIN-ZERO 1MHz SPI가 351회 연속
  성공하고 CRC 실패는 0회였다. 당시 MAIN/ZERO 내부 온도는 약 32/37~38°C,
  AHT20 외부 측정값은 약 29°C/71~73%였다.

## 14.2 선행 소프트웨어 구현 기록

### 2026-09-05 — 듀얼 펌웨어 골격과 공용 통신 계층 구현

- 현재 v3.3.4 제품 소스와 릴리스 도구를 변경하지 않는 별도 `v5/` 트리를
  추가했다.
- `MilestoneV5Core` 공용 라이브러리에 다음 순수 C++ 계층을 구현했다.
  - 명시적 little-endian 32바이트 프레임 헤더
  - protocol version, message type, flags, lease ID, sequence와 ACK sequence
  - 헤더 CRC-32와 payload CRC-32 독립 검증
  - 4096바이트 일반 payload 상한과 512바이트 고정 SPI mailbox
  - SPI 길이/complement guard와 트랜잭션당 476바이트 payload 상한
  - sequence 최초/정상/누락/중복/stale 판정과 32비트 wrap 처리
  - ACK sequence 일치 검증, 고정 상한 재전송과 heartbeat stale 판정
  - CORE/MEDIA/NOW quiesce-start 프로필 전환 상태머신
  - AP/BLE/STA/OTA 상태를 입력으로 받는 네트워크 작업 배정기
  - 단일 소유자 task lease 획득, 갱신, 해제와 wrap-safe 만료
  - 선택 장치 오류가 펌웨어 롤백으로 번지지 않는 장애 분류
- 확정된 상위 기능을 하드웨어 독립 상태 로직으로 먼저 구현했다.
  - AHT20 주소·CRC-8 판별, 온습도 범위 검사, 보정, 저비용 필터와 stale
  - 전체 MODE 메뉴와 MEDIA 사진/영상 탐색·재생·일시정지·손상 항목 격리
  - 앨범아트 AUTO/CUSTOM/BLOCKED/MISSING 정책과 CUSTOM 보호 LRU
  - leaf 파일명 검증과 임시 기록-검증-commit 원자적 저장 순서
  - 2GB 캐시/1GB 여유 공간을 포함한 비영구 draft 설정 검증
  - MAIN/ZERO 자산 대상·크기·SHA-256·서명·peer protocol 검증
  - 순차 chunk 조립, MAIN 자체검사, 선택적 ZERO 설치와 실패 범위 분리
  - 현재/이전 OTA/내부 안전모드/SD Stable/SD Recovery/USB 복구 선택
- MAIN 선행 스케치는 5버튼 debounce와 6항목 MODE 메뉴, 프로필 선택,
  SAFE MODE/RESTART 요청, ZERO protocol negotiation과 heartbeat, 링크 stale 시
  MAIN 독립 동작을 구현한다. 아직 TFT, SD, RTC, 환경센서, Wi-Fi, NVS와 OTA는
  초기화하지 않는다.
- ZERO 선행 스케치는 SPI slave mailbox, CRC 검증, sequence 추적, protocol
  negotiation, ACK와 명시적 endian의 내부 온도/heap/PSRAM 상태 응답을
  구현한다. BLE와 Wi-Fi는 아직 초기화하지 않는다.
- `tools/test-v5.sh`의 protocol/runtime host 테스트를 추가했고 GCC의
  `-Wall -Wextra -Wpedantic -Werror` 조건에서 통과했다.
- Arduino-ESP32 3.3.11로 MAIN N16R8과 Waveshare ZERO 빌드가 모두 통과했다.
  이 단계의 측정 크기는 MAIN 310,432바이트, ZERO 318,732바이트다.
- 기존 `tools/test-core.sh` 전체를 다시 통과해 v3.3.4 릴리스 계약에 영향이
  없음을 확인했다.

현재 SPI wire version 1은 선행 구현 계약이다. 실제 장시간 시험에서 slot 크기나
전송 방식 변경이 필요하면 v5.0 공개 릴리스 전에 protocol version을 올리거나
호환 협상을 구현한다. 이 선행 스케치는 제품용 OTA 대상으로 사용하지 않는다.

### 2026-09-05 — 장치 런타임 및 포털 연결 진행

위 14.2는 최초 골격 구현 당시의 기록이다. 이후 실제 장치 초기화와 기능 연결이
진행되었으며 당시 범위와 이후 완료 상태는 `v5/IMPLEMENTATION_STATUS.md`에서
구분한다. 순수 상태머신이 존재한다는 이유만으로 OTA, 복구, 설정 마이그레이션이
완성됐다고 판단하지 않는다. 특히 14.2의 `signatureVerified` 입력 검사는
실제 암호학적 서명 검증을 수행하는 코드가 아니다.

현재 TFT/SD/RTC/환경센서, BLE/AMS, Wi-Fi/NTP, CORE 화면과 설정, 로컬 미디어,
앨범아트 전송·캐시·관리 포털, MAIN STA 작업을 런타임에 연결했다.
MAIN SD 복원에는 별도의 실제 SHA-256/서명 검증기를 추가했으며 릴리스 공개키가
설정되지 않으면 Flash 쓰기를 거부한다. ZERO의 직접 SD 복원도 양쪽 서명 검증,
SPI 청크 설치와 부팅 승인 확인까지 연결했다. SD에 준비한 서명 묶음에 대해
MAIN 승인→ZERO 설치/승인→10분 안정화→Stable/Backup A/B 인덱스 승격과
NVS 재부팅 복구를 런타임에 연결했다. 링크 장애/과열은 안정화 시간을 다시 시작한다.
이 기록 시점에는 인터넷 묶음 다운로드, 독립 안전 앱과 통합 릴리스 계약이
미완료였다.
제품용 업로드나 v5 릴리스는 수행하지 않았다.

### 2026-09-06 — 계획 소프트웨어 연결 완료

위 2026-09-05 문단은 당시 진행 기록이다. 이후 인터넷 묶음 다운로드, 독립
SAFE 앱, 최종 파티션, 설정 마이그레이션과 통합 릴리스 계약까지 연결했다.

- MAIN 또는 ZERO의 bounded HTTPS worker가 고정 GitHub Release 자산을
  CA 검증과 제한 redirect로 내려받고, MAIN이 SD에 기록하면서 bundle/manifest
  서명·대상·버전·peer protocol·크기·SHA-256을 검증한다.
- MAIN 시험 부팅 승인 뒤 선택적 ZERO 설치·승인, 10분 안정화와
  Stable/Backup A/B 승격을 완료하며 Recovery는 자동 변경하지 않는다.
- 16MiB MAIN 표에 2MiB factory SAFE와 6MiB MAIN A/B를 배치했다. SAFE는
  네트워크 없이 TFT·5버튼·서명 SD 복원·내부 A/B 선택·기본 진단을 수행한다.
- 최대 8개 Personal/Open/PEAP Wi-Fi, 연결 시험 후 저장, 양 보드 복제,
  시스템/LED/NTP/절전 설정, v3 schema 12 CORE·Wi-Fi 가져오기를 연결했다.
- CORE 세부 표시·자동 순환·스크롤·번인 이동, NOW 4개 배치, 환경 mask와
  임계값, 진단 A/B ring, 앨범아트 A/B 영구 인덱스와 요청 완료 UI를 연결했다.
- 기존 `milestone-release`가 v5.0.0일 때 MAIN/ZERO/SAFE를 빌드하고 공개키를
  포함하며 서명 manifest/bundle/catalog와 MAIN/ZERO 초기 USB 이미지를
  생성·재검증하도록 확장했다. 별도 게시 워크플로는 만들지 않았다.
- 기존 v3 회귀, v5 호스트 테스트와 ASan/UBSan, Arduino-ESP32 3.3.11의
  MAIN/ZERO/SAFE 빌드, 임시 P-256 키의 13개 릴리스 자산 생성·검증이 통과했다.

계획된 소프트웨어 항목은 이 시점에 모두 반영됐다. 제품 키를 넣지 않은 기본
빌드는 업데이트를 fail-closed로 거부한다. 제품 업로드·커밋·태그·GitHub Release는
하지 않았으며, 남은 일은 아래의 조립 완료 후 실기 검증과 그 결과에 따른 상수
보정이다. 세부 근거는 `v5/IMPLEMENTATION_STATUS.md`를 따른다.

## 15. 배포 후 계속 실측할 항목

- AHT20 실제 조립 위치에 따른 온습도 오프셋 실측
- GOOUUU 외장 안테나 단자의 규격과 내장/외장 선택 회로
- 최종 조립체에서 MAIN/SAFE 16MiB 파티션과 ZERO 4MiB 초기 이미지 업로드 검증
- MVJ1 JPEG 프레임률·PSRAM·SD 지연과 발열 실측에 따른 변환 기본값 보정
- 기존 artwork worker/MAC1 계약의 실서비스 가용성 및 평문 메타데이터 정책 확인
- MAIN과 ZERO 사이 SPI 1MHz/512바이트 slot의 장시간 오류율과 배선 길이 실측
- AP/STA/BLE 동적 전환 임계값과 실기기 무선 시험

이 항목들은 구현과 실측으로 결정하며, 확정 전 임의로 영구 호환 계약으로 만들지 않는다.
