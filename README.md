# MILESTONE Core — MILESTONE D1

현재 제품 펌웨어는 **v5.1.2**입니다. GOOUUU ESP32-S3 N16R8 MAIN과
Waveshare ESP32-S3-Zero ZERO를 함께 사용하며, 기존 v3.3.4의 CORE·MEDIA·NOW
인터페이스를 듀얼 보드 구조 안에서 실행합니다.

- 최신 릴리스: [MILESTONE Core v5.1.2](https://github.com/CXITRON/MILESTONE-Core/releases/tag/v5.1.2)
- 설치·OTA·복구: [v5/README.md](v5/README.md)
- 구현·검증 범위: [v5/IMPLEMENTATION_STATUS.md](v5/IMPLEMENTATION_STATUS.md)
- v3.3.4 설치·사용법·변경 이력: [docs/LEGACY_V3.md](docs/LEGACY_V3.md)

## 제품 구조

| 구성 | 역할 |
|---|---|
| MAIN | CORE/MEDIA/NOW 전환, TFT, 5버튼, microSD, RTC, AHT20, 설정 AP, OTA·복구 |
| ZERO | iPhone BLE/AMS, 저장 Wi-Fi, NTP·HTTPS, 앨범아트·OTA 데이터 전달 |
| SAFE | 네트워크 없이 내부 A/B 또는 서명된 SD 이미지를 복원하는 독립 factory 앱 |

CORE, MEDIA, NOW는 MAIN 안에서 재설치나 재부팅 없이 전환됩니다. ZERO가
연결되지 않아도 MAIN의 CORE 화면, 버튼, SD와 SAFE 복구 기능은 계속 작동하며,
BLE Now Playing과 ZERO가 맡은 인터넷 작업만 제한됩니다.

## 조작과 설정 AP

| 버튼 | 기본 동작 |
|---|---|
| PREV | 이전 화면·미디어 |
| NEXT | 다음 화면·미디어 |
| OK | 선택, 정보 페이지 이동, 재생/일시정지 |
| BACK | 취소, 미디어 종료, 설정 AP 종료 |
| MODE | CORE/MEDIA/NOW, Wi-Fi 설정 AP, 재시작, SAFE MODE 메뉴 |

MODE 메뉴와 설정 AP는 기존 v3 펌웨어 선택·설정 화면의 글꼴, 배치, 색상과
중앙정렬 방식을 유지합니다. 설정 AP는 `MILESTONE-D1-SETUP`으로 광고되며,
TFT에 표시되는 8자리 임의 암호 또는 사용자가 저장한 고정/개방형 설정을
사용합니다. 포털에서 주변 2.4GHz Wi-Fi를 검색하고 연결 시험이 성공한
네트워크만 저장합니다.

## 로컬 MEDIA

- 사진: microSD의 `/media/photo/*.bmp`, 최대 128×128 24비트 BMP
- 영상: `/media/video/*.mvj`, 128×128 컬러 JPEG 프레임 기반 MVJ1,
  1~30fps, 오디오 없음
- 포털 미디어: 브라우저에서 변환한 v3 호환 MSM1을 microSD에 원자적으로 저장
- 동기화 MEDIA: 브라우저에서 전체 영상을 MVJ1으로 변환·임시 업로드한 뒤,
  브라우저 원본 오디오 시간을 기준으로 SD 영상 재생
- 기본 출력은 컬러이며 MEDIA 설정에서만 흑백 표시를 선택할 수 있음
- 재생 중에는 영상 프레임을 전송하지 않으며 제어 신호만 교환함

```bash
python3 tools/convert-v5-media.py input.mp4 output.mvj --fps 15 --seconds 60
```

## 업데이트와 설치

v5 릴리스는 MAIN/ZERO/SAFE와 서명·manifest·catalog를 포함한 고정 13개 자산으로
배포됩니다. 최초 설치는 `v5-main-initial.bin`과 `v5-zero-initial.bin`을
사용하고, 이후 업데이트는 서명된 MAIN/ZERO 묶음을 검증한 뒤 비활성 OTA
슬롯에 설치합니다. MAIN의 일반 Arduino 업로드는 factory SAFE 슬롯을
대상으로 하므로 v5 제품 설치 방식으로 사용하지 않습니다.

로컬 저장소의 정식 릴리스는 프로젝트 루트에서 단일 명령으로 실행합니다.

```bash
milestone-release local X.Y.Z "release note"
```

## 하드웨어 핀

| MAIN 기능 | GPIO |
|---|---:|
| PREV / BACK / NEXT / MODE / OK | 4 / 5 / 7 / 14 / 15 |
| TFT RESET / MOSI / SCK / DC / CS | 6 / 11 / 12 / 21 / 47 |
| microSD CS / MOSI / SCK / MISO | 10 / 11 / 12 / 13 |
| I2C SDA / SCL | 41 / 42 |
| RGB LED | 48 |

| MAIN–ZERO | MAIN GPIO | ZERO GPIO |
|---|---:|---:|
| SCK | 8 | 9 |
| MAIN → ZERO | 9 | 8 |
| ZERO → MAIN | 16 | 10 |
| CS | 17 | 7 |
| READY | 18 | 6 |

모든 버튼은 `INPUT_PULLUP` active-low이며 두 보드는 GND를 공통으로 연결합니다.
TFT와 microSD는 MAIN의 MOSI/SCK를 공유하고 각각 별도의 CS를 사용합니다.

## v5.1.2 업데이트 안내

v5.1.2는 저장된 Wi-Fi가 있는 상태에서 ZERO가 반복 재부팅되던 초기화 순서
오류를 수정한 긴급 복구 릴리스입니다.

- MAIN이 부팅할 때마다 저장된 Wi-Fi를 새 후보처럼 ZERO에 재시험하던 동작 제거
- ZERO와 MAIN 모두 SNTP가 실제 시작된 경우에만 종료하도록 네트워크 수명주기 보호
- ZERO의 `Invalid mbox` assert와 그에 따른 온도·BLE·Wi-Fi 기능 상실 방지

## v5.1.1 업데이트 안내

v5.1.1은 설정 AP와 Wi-Fi 저장 과정에서 확인된 표시·상태 복구 오류를 우선
수정한 긴급 안정화 릴리스입니다.

- Wi-Fi 시험 실패를 `idle`로 숨기지 않고 성공·실패 상태를 명확히 반환
- 저장 성공 직후 포털의 저장 네트워크 목록을 다시 불러오도록 수정
- ZERO가 Wi-Fi를 시험하는 동안에도 마지막 정상 온도를 링크 유효 시간 기준으로 유지
- 이전 동기 MEDIA 주소가 남은 브라우저를 설정 첫 화면으로 안전하게 복귀
- 한글 오류 응답에 UTF-8 charset을 명시해 깨진 문자 표시 방지
- 부팅 splash 위에 상태바를 번갈아 그리던 3초간의 깜빡임 제거
- 설정 본문과 상단 상태바에 중복 표시되던 AP 표식 제거

## v5.1.0 업데이트 안내

v5.1.0은 MEDIA에 브라우저 오디오와 microSD 영상을 결합한 단계형 동기 재생을
추가합니다. 실시간 프레임 스트리밍과 달리 변환·업로드를 먼저 완료한 뒤
재생하므로 무선 지연이 화면 전송량에 직접 누적되지 않습니다.

- 브라우저에서 원본 전체를 128×128 컬러 MVJ1, 1~30fps로 변환
- microSD 임시 영역에 업로드 후 프레임 길이·CRC·JPEG 크기 전수 검증
- 프레임 위치 인덱스를 별도로 생성해 오디오 시간 변경과 탐색에 즉시 대응
- 재생 중에는 브라우저 오디오를 기준으로 작은 WebSocket 제어 패킷만 교환
- 연결이 2.5초 끊기면 MAIN 영상을 자동 일시정지하고, 기기 OK로 재생 토글 요청
- BACK/MODE/AP 종료 또는 재부팅 시 임시 영상·인덱스와 동기 세션 삭제
- 휴대폰 브라우저 메모리 보호를 위한 변환본 256 MiB 제한

## v5.0.1 업데이트 안내

v5.0.1은 v5 통합 과정에서 달라지거나 빠졌던 기존 화면·설정 동작을 복구하고,
듀얼 보드 상태와 새 센서를 기존 UI 문법 안에서 확인할 수 있도록 보완했습니다.

- MODE 메뉴와 설정 AP 화면을 기존 프로필 선택·설정 화면의 배치로 복구
- 우상단 상태 표시를 대기 원·동기화 시계·온라인 체크·AP·업데이트·오류 의미로 복구
- CORE 기기 세부정보를 9페이지로 확장해 MAIN/ZERO·SD·RTC·AHT20·OTA 상태 표시
- 기존 8자리 AP 암호, 고정/개방형 암호와 주변 Wi-Fi 검색·시험·저장 복구
- ZERO capability 및 SPI 요청 임대 ID 검증, 앨범아트 이미지 삭제 기능 추가
- 기기와 포털 설명을 한글화하고 NOW의 BLE/AMS 연결 단계를 구분
- MAIN/ZERO 실기 유선 업로드, 부팅과 protocol v1 협상 확인

## v5.0.0 업데이트 안내

v5.0.0은 듀얼 ESP 제품 구조를 도입한 첫 정식 펌웨어입니다.

- MAIN의 CORE/MEDIA/NOW 런타임과 TFT·SD·RTC·AHT20·5버튼 통합
- ZERO의 BLE/AMS, 다중 Wi-Fi/PEAP와 동적 인터넷 작업
- SD 사진·MVJ1 영상, 컬러 앨범아트, 환경 로그와 진단
- 서명된 MAIN/ZERO 묶음 OTA, 독립 SAFE와 SD 복구
- 기존 v3.3.4 설정의 일회성 가져오기와 레거시 소스 보존
