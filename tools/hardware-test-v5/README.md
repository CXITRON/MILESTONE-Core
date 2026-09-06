# MILESTONE v5 회로 점검용 임시 펌웨어

이 디렉터리는 완성된 회로를 유선으로 처음 검사하기 위한 독립 스케치다. 현재 v3.3.4 제품 펌웨어와 OTA 릴리스 파일은 변경하지 않는다. MAIN 스케치는 제품용 NVS 설정을 쓰지 않으며 SD에는 `/MILESTONE_HW_TEST.TMP`만 기록한 뒤 즉시 삭제한다.

RTC만 먼저 납땜 검사할 때는 `esp-main-rtc-test/esp-main-rtc-test.ino`를 ESP MAIN에 업로드한다. GPIO41=SDA, GPIO42=SCL, 3V3=VCC, GND=GND 네 선만 연결하면 되며 115200 baud 로그에서 DS3231 주소 0x68, 선택적 24C32 주소 0x57, 시각 증가, RTC 내부 온도와 OSF 상태를 확인할 수 있다. 열로 밀린 GPIO1과 그 주변은 사용하지 않는다.

## 최종 검사 배선

### ESP MAIN

| 기능 | GPIO |
|---|---:|
| I2C SDA (RTC + 환경센서 공용) | 41 |
| I2C SCL (RTC + 환경센서 공용) | 42 |
| PREV | 4 |
| BACK | 5 |
| TFT RESET | 6 |
| NEXT | 7 |
| MAIN-ZERO SCK | 8 |
| MAIN-ZERO MOSI | 9 |
| SD CS | 10 |
| SD MISO | 13 |
| MODE | 14 |
| OK | 15 |
| MAIN-ZERO MISO | 16 |
| MAIN-ZERO CS | 17 |
| MAIN-ZERO READY | 18 |
| TFT MOSI + SD MOSI 공용 | 11 |
| TFT SCK + SD SCK 공용 | 12 |
| TFT DC/A0 | 21 |
| TFT CS | 47 |
| 내장 RGB LED | 48 |

TFT VCC/LED, RTC VCC, 환경센서 VDD는 MAIN 3V3에 연결한다. SD 모듈 VCC/V5IN은 MAIN 5V에 연결한다. 각 버튼은 해당 GPIO와 공통 GND 사이에 연결한다.

### ESP MAIN ↔ ESP ZERO

| ESP MAIN | ESP ZERO | 기능 |
|---:|---:|---|
| GPIO8 | GPIO9 | SCK |
| GPIO9 | GPIO8 | MAIN → ZERO |
| GPIO16 | GPIO10 | ZERO → MAIN |
| GPIO17 | GPIO7 | CS |
| GPIO18 | GPIO6 | READY |
| GND | GND | 공통 기준 전압 |

두 보드는 각자 USB 5V로 전원을 공급하고 5V/3V3 전원선을 서로 연결하지 않는다. GND만 공통으로 연결한다.

## 업로드 순서

1. ESP ZERO만 PC에 연결하고 `esp-zero-test/esp-zero-test.ino`를 업로드한다.
2. ZERO의 USB 전원은 유지한 채 ESP MAIN을 PC에 연결한다.
3. `esp-main-test/esp-main-test.ino`를 업로드한다.
4. MAIN 시리얼 모니터를 115200 baud로 연다. 필요하면 ZERO 로그도 별도 포트에서 115200 baud로 연다.

ESP 간 SPI 다섯 신호와 공통 GND만 먼저 검사할 때는 ZERO에 기존
`esp-zero-test/esp-zero-test.ino`, MAIN에
`esp-main-spi-test/esp-main-spi-test.ino`를 업로드한다. MAIN 로그에
`SPI PASS`가 100ms마다 계속 증가하면 SCK/MOSI/MISO/CS/READY와 패킷 CRC가
모두 정상이다. `WAIT READY`는 ZERO 전원·READY·공통 GND를, `invalid packet`은
SCK/MOSI/MISO/CS 배선을 우선 검사한다.

### 2026-09-05 실기 결과

- MAIN `/dev/ttyUSB0`, ZERO `/dev/ttyACM0`에서 각각 유선 업로드에 성공했다.
- 1MHz SPI mode 0에서 32바이트 패킷의 양방향 전송과 CRC-32 검증을 통과했다.
- ZERO sequence, nonce와 내부 온도가 MAIN에 정상 전달됐다.
- 닫힌 ZERO USB CDC 로그가 슬레이브 루프를 잠깐 막지 않도록 런타임 출력을
  비차단 처리한 뒤 8초 동안 약 80회 연속 `SPI PASS`를 확인했다.
- 현재 여섯 가닥 납땜은 정상이며 양쪽 보드에는 SPI 점검용 임시 펌웨어가 남아 있다.

Arduino CLI에서 검증한 보드 옵션:

```bash
/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli compile \
  --fqbn 'esp32:esp32:waveshare_esp32_s3_zero:CDCOnBoot=default,PSRAM=enabled,PartitionScheme=min_spiffs' \
  tools/hardware-test-v5/esp-zero-test

/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  tools/hardware-test-v5/esp-main-test

# ESP MAIN의 USB-UART 단자에서 SPI 전용 시험 로그를 읽는 빌드
/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  tools/hardware-test-v5/esp-main-spi-test
```

## 정상 판정

- TFT: 빨강, 초록, 파랑, 흰색 전체 화면이 순서대로 보이고 이후 글자가 위아래 바르게 표시된다.
- 버튼: 누른 버튼의 첫 글자 `P/N/O/B/M`이 초록색으로 바뀌고 시리얼에 `BUTTON PASS`가 출력된다.
- PSRAM: `PSRAM OK 8MB`가 표시된다. 단순 존재 확인뿐 아니라 256 KiB 패턴 기록/검증을 수행한다.
- SD: `SD OK`와 카드 용량이 표시되고 임시 파일 쓰기/읽기 비교가 통과한다.
- RTC: `RTC OK`와 시각이 1초씩 증가한다. `OSF=1`은 배터리/초기 시각 설정이 필요하다는 뜻이며 배선 불량과는 구분한다.
- 환경센서: 실제 조립체에서는 AHT20 주소 0x38의 온도·습도만 사용한다. 측정 데이터의 CRC-8과 유효 범위를 통과하면 `ENV OK`와 온도·습도가 표시된다. 같은 모듈에 표기된 BMP280 기압 기능은 사용하지 않는다.
- ESP 간 SPI: `SPI ZERO OK`, 양쪽 칩 온도와 증가하는 SEQ/RX가 표시된다. CRC가 틀리거나 READY/배선이 없으면 FAIL로 바뀐다.
- RGB LED: 각 보드는 자기 검사 상태만 표시한다. 상대 보드 LED를 원격으로 제어하지 않는다.

환경센서가 아직 납땜되지 않았다면 `ENV FAIL`만 발생하는 것이 정상이며 나머지 검사는 계속된다. SD, RTC, 센서 또는 ZERO가 빠져도 MAIN의 화면과 버튼 점검은 중단되지 않는다.
