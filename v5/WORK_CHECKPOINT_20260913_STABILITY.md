# 2026-09-13 MILESTONE D1 v5.2.3 작업 보고

> 후속 실사용에서 ZERO 링크 유실 및 Sync `Load Failed`가 보고되었다.
> 이 문서의 5.2.3 호스트 검사는 해당 회귀를 포착하지 못했다.
> 원인 재현·긴급 수정은 [5.2.4 보고](WORK_CHECKPOINT_20260913_HOTFIX.md)를 참조한다.

기준: 로컬 v5.2.2 소스에서 시작한 OTA·SPI·역할 분배·버튼·Sync·상태 표시 수정.
레거시 v3.3.4, schema 12, 핀맵, 파티션과 기존 릴리스 절차는 유지한다.
이 기록의 호스트 테스트/빌드 결과와 실기 검증은 구분한다.

## 1. 코드에서 확인한 원인과 변경

| 사용자 항목 | 확인한 소프트웨어 경로 | 적용 |
|---|---|---|
| OTA 확인 불안정 | ZERO가 MAIN의 프로필/AP 정책을 적용하지 않았고 BLE 연결 후에도 이미 시작한 연결/NTP가 진행될 수 있었음. BLE 객체를 유지한 상태에서 TLS heap이 부족하면 시작 실패 | 정책 heartbeat를 작업보다 먼저 ACK, ZERO의 live BLE 시 연결/NTP 중지·인터넷 작업 거부, heap/연속 블록 여유에 따른 한가한 MAIN 인계 |
| OTA 정체 | MAIN의 esp_ota_begin이 전체 이미지 공간을 지우며 loopTask를 점유. ZERO의 실패 수신증도 90초 대기 중 무시 | 서명/전체 SD 해시 검사 후 sequential erase/write. ZERO 재부팅 후 15초 이상 경과한 명시적 Failed는 오류로 표시, 무응답은 기존 90초 한도 |
| 일시적 HTTPS 실패 | 본문 전 연결/서버 오류가 즉시 전체 실패 | 연결 오류·408/429/500/502/503/504만 최대 3회. 취소 가능한 250/500 ms 간격. 본문 수신 이후 재시도·이어 붙이기 금지 |
| TFT/SD 부하 | 하나의 SPI에서 40 MHz TFT·10 MHz SD 사용. tile 출력은 픽셀마다 2회 transfer 호출. 별도 worker의 동시 SD/TFT 접근은 발견하지 못함 | loopTask 단일 소유 유지, 상대 CS 해제, TFT 20 MHz, tile 128바이트 일괄 쓰기, dirty flush 3 ms 예산, 영상 8행마다 CS 해제 |
| 버튼 입력 유실 | loopTask에서 30 ms 디바운스/입력을 함께 처리하여 긴 호출 사이에 끝난 누름은 관측 불가 | esp_timer 5 ms GPIO 샘플링+30 ms 디바운스, 버튼별 atomic 대기 입력. 동작·확인 조건·설치 중 제한은 기존 루프에서 처리 |
| Sync 전송 지연 | 현재 묶음은 64 KiB가 아니라 256 KiB. multipart boundary를 바이트별로 파싱하고 작은 단위로 파일 write | raw binary route, 8 KiB PSRAM write buffer, 요청 종료 전 checkpoint, 상태 조회 후 정확한 suffix 재개. 기존 multipart도 유지 |
| 프로필 분리 | ZERO가 profile/AP payload를 사용하지 않아 CORE에서도 BLE 초기화·광고 가능 | NOW에서만 활성화. CORE/MEDIA/AP/안전/설치에서는 중지. 오래된 MAIN 정책도 BLE 비활성 처리 |
| 아이콘·LED·정보 | 앨범아트와 NTP가 같은 아이콘 의미를 사용. 낮은 RGB에 LED brightness가 다시 곱해짐. ZERO 세부 런타임 정보 없음 | 14종 아이콘, 보드별 실제 로컬 LED와 1회 밝기 적용, CORE 12페이지 및 capability 기반 ZERO 상세 응답 |

SPI 깨짐의 전기적 원인은 코드 분석만으로 확정할 수 없다. CS 경계 테스트와
클럭 완화는 적용했지만 배선 길이·GND·SD 모듈 레벨 시프터·전원·패널 상태를
포함하는 실측이 필요하다. 실제 업로드 속도나 버튼 최악 지연의 개선 배수도 아직 실측하지 않았다.

## 2. MAIN/ZERO 역할과 프로필

- MAIN: TFT, 전면 버튼/BOOT, DS3231, AHT20, microSD, 환경 로그, 설정 AP,
  CORE/MEDIA/NOW 표시, OTA/SAFE 조정.
- ZERO: NOW의 iPhone BLE/AMS, 기본 저장 Wi-Fi, NTP, HTTPS/앨범아트/업데이트 데이터.
- ZERO에 live BLE가 있거나 HTTPS 메모리가 부족하거나 ZERO가 오프라인이고
  MAIN에 AP·MEDIA 작업이 없으면 MAIN이 제한된 네트워크 작업을 맡는다.
- AP가 열려 있으면 접속자가 없어도 자동으로 닫아 네트워크를 인계하지 않는다.
  AP 수동 NTP는 새 optional task 36으로 ZERO에 요청하고 최대 45초 안에 종료한다.
  ZERO가 없는 AP에서 자격 증명을 시험하는 기존 명시적 MAIN AP+STA 경로는 유지한다.
- GPIO에 연결된 주변장치, 진행 중인 SD 파일 또는 Flash 설치를 ZERO로 옮기지 않는다.
  인계는 시작 전의 네트워크 작업 단위이며 진행 중인 TLS/Flash 세션 이동은 아니다.
- NOW 진입 시 BLE 초기화를 기존 첫 Wi-Fi/NTP 대기 뒤로 미루지 않는다. AP/프로필
  전환에서 기존 BLE 객체는 유지해 deinit 중 GAP/GATT callback 경합을 피한다.
  따라서 CORE로 돌아온 뒤 BLE가 쓰던 내부 RAM이 전부 반환된다고 보장하지 않는다.
- 정상 NOW의 active AMS를 인터넷 요청 때문에 끊지 않는다. AP/다른 프로필/실제 설치는
  명시적 BLE 중지 경계이다. 재연결·장시간 곡 정보 연속성은 iPhone 실기 확인 대상이다.

## 3. OTA 흐름과 유지한 무결성

1. 부팅 후 또는 사용자가 업데이트 확인 요청.
2. 사용 가능한 보드를 정하고 서명된 `bundle.txt`/`.sig`만 먼저 내려받음.
3. 최신/실패/새 버전 결과. 새 버전은 첫 OK 뒤 정확한 버전의 설치 자산 다운로드.
4. MAIN/ZERO manifest 서명·역할·버전·프로토콜·길이·SHA-256 검증.
5. 두 번째 물리 확인 뒤 immutable SD set 생성, 원본/복사본 해시 및 NVS journal.
6. MAIN 비활성 슬롯 기록·검증·부팅 선택 → MAIN 재부팅/실행 해시/부팅 승인.
7. ZERO 이미지가 포함되면 SPI 460바이트 청크 → ZERO 비활성 슬롯 검증/재부팅.
8. transfer ID·실행 파티션·ELF 해시에 결합된 ZERO 승인 수신증 → 10분 안정화.
9. 설치 완료와 Stable 지정은 별개. 관리자 서명 stable channel만 A/B Stable/Backup 갱신.

개인키는 기기나 소스에 포함하지 않는다. Flash 대상·검증 순서·SAFE factory 위치·
공개키 신뢰·프로토콜 v1·NVS schema는 유지한다. SPI의 빠른 정상 ACK 회수에는 5 ms를
사용하되 두 번 이상 응답이 늦으면 40 ms로 간격을 늘려 즉시 HELLO 재협상을 반복하지 않는다.
설치 본문 재그리기는 250 ms로 제한한다. 진행률은 실제 상태/전송량이며 실패를 성공으로 표시하지 않는다.

## 4. Sync 업로드와 버튼 보존

변환기는 256 KiB를 넘는 순간 완성 JPEG 레코드까지 포함해 보낸다. 요청 한도는
320 KiB이며 offset/final/길이/Content-Type을 수집된 헤더로 검증하고 AP 로컬 인터페이스를 확인한다. multipart나 잘못된
Content-Type을 raw 포인터로 처리하지 않는다. 응답이 유실되면 저장 offset을 조회해
남은 데이터만 보내며, 모두 기록됐지만 final 처리가 빠졌으면 빈 final 요청을 보낸다.
최대 3회 시도와 세션 위치 불일치 거부를 유지한다. 이 재개는 같은 장치 런타임의
일시적 네트워크 중단용이며 전원 차단 뒤 업로드 복구 기능을 추가한 것은 아니다.

SD 쓰기 이후 callback은 companion 응답·취소 버튼·온도만 제한적으로 처리한다.
포털이나 버튼 동작을 재귀 실행하지 않는다. SD/Flash/드라이버 자체가 긴 시간
반환하지 않으면 동작 실행이 지연될 수 있다. 버튼별 대기값은 한 번의 누름으로
합쳐지며 빠른 여러 번 누름을 무한히 축적하지 않는다. 샘플러 생성 실패 시 기존
loop 수집으로 돌아가고 정보 화면에 `LOOP FALLBACK`을 표시한다.

파일 전체 저장 이후 MVJ1 헤더·각 프레임 CRC/JPEG·인덱스를 검사하는 조건,
오디오 시간을 따르는 재생·지연 프레임 건너뛰기·2.5초 제어 timeout은 유지한다.

## 5. 우상단 아이콘과 독립 LED

아이콘은 기존 우상단 12×12 범위에서 모양과 색을 함께 구분한다.
Wi-Fi 연결 아이콘은 최종 요청에 따라 두 개의 호와 점으로 단순화했다.
[실제 비트맵의 원본 크기·확대 미리보기](activity-icons.svg)를 함께 보관한다.
LED는 이 색을 해당 보드의 실제 로컬 작업에 적용하고 사용자 주야간 밝기를 한 번 적용한다.
MAIN은 ZERO의 BLE LED를 복제하지 않는다. 단독 SAFE는 MAIN LED로 안전 상태를 나타낸다.

| 상태 | 색/아이콘 의미 | LED 주기 |
|---|---|---|
| IDLE | 어두운 녹색 원 | 2초 |
| NTP | 흰색 시계 | 0.8초 |
| ONLINE | 녹색 확인 | 계속 켜짐 |
| AP | 청록색 AP | 계속 켜짐 |
| OTA 확인 | 주황색 돋보기 | 0.8초 |
| OTA 설치 | 보라색 위 화살표 | 0.8초 |
| 오류/열 보호 | 빨간 X | 0.2초 |
| Wi-Fi 연결 | 노란 Wi-Fi 파형 | 0.8초 |
| 앨범아트 | 파란 이미지 | 0.8초 |
| 다운로드 | 진한 주황색 아래 화살표 | 0.8초 |
| SD 작업 | 노란 SD 카드 | 0.8초 |
| BLE 광고 | 파란 BLE/광고 모양 | 0.8초 |
| BLE 연결 | 파란 BLE | 계속 켜짐 |
| SAFE | 붉은 방패 | 계속 켜짐 |

깜박임은 한 주기의 절반 켜짐/절반 꺼짐이다. 여러 작업이 겹치면 안전·설치 등
우선 상태 하나를 표시한다. 모든 센서/SD write 순간을 개별 LED 펄스로 기록하는 방식은 아니다.

## 6. 기기 정보

기존 SYSTEM/MEMORY/PSRAM/STORAGE/NETWORK/TIME/ENVIRONMENT/ZERO/FIRMWARE의
9페이지를 보존하고 다음 3페이지를 추가했다.

- ZERO SYSTEM: 실제 FW, uptime, CPU MHz, reset reason ID, Wi-Fi RSSI, boot state.
- ZERO RESOURCES: 최소 heap, 최대 연속 heap block, 남은 stack, 최장 loop 간격,
  수신 프레임 정상/오류 개수.
- MAIN RESPONSE: 최장 loop 간격, 최장 TFT flush 시간, 유효/잘못된 응답·재시도,
  버튼 `5ms / 30ms` 또는 fallback 여부.

세부 정보는 optional capability/task 35로 5초마다 받으며 링크 단절 또는 15초
이상 된 값은 무효로 표시한다. 이전 ZERO와의 v1 통신은 유지하고 지원하지 않는
정보를 추측하지 않는다. 기존 페이지 저장 레코드와 호환되도록 새 페이지에서
저장할 때 기존 허용 페이지 0을 기록한다. `/api/status`에도 MAIN 지연·입력 방식·
실제 부팅 승인과 유효한 ZERO 상세 정보를 추가했다.

## 7. 검증 결과

- `./tools/test-core.sh`: v3.3.4 회귀·문서 동기화·릴리스 복구 계약 통과.
- `./tools/test-v5.sh`: 기존 프로토콜/서명/OTA/미디어 테스트와 신규 테스트 통과.
  실제 ZERO 네트워크 스케줄러의 BLE 중지·수동 NTP/timeout, 실제 HTTPS worker의
  최대 3회 시도·CA/redirect·부분 본문/취소, 버튼 debounce/대기 입력, 실제 TFT
  전송의 CS/클럭/일괄 쓰기, Sync SD checkpoint 실패·부분 재개·final 응답 유실을 검증.
- `ASAN_OPTIONS=detect_leaks=0 V5_SANITIZE=1 ./tools/test-v5.sh`: ASan/UBSan 통과.
  LeakSanitizer는 기존 호스트 테스트 환경 조건에 따라 제외.
- 실제 U8g2 글꼴을 사용하는 `test-v5-update-ui-fonts.sh`: OTA 화면 경계·UTF-8·겹침 검증 통과.
- 제품 공개키 MAIN/ZERO/SAFE 빌드와 고정 13개 자산의 서명·크기·SHA-256·역할·
  프로토콜·factory/OTA offset 검증 통과. MAIN 앱 1,791,776바이트,
  ZERO 앱 1,349,360바이트, SAFE 앱 594,160바이트로 각 제품 슬롯 한도 이내.
- 통합 `milestone-release --dry-run local 5.2.3` 검증 통과. 처음의 인증 오류는
  샌드박스 네트워크 제한이었으며 허용된 네트워크 환경에서 정상 인증/검증 확인.
- 최종 Wi-Fi 비트맵 변경은 정식 릴리스 명령에서 호스트·3종 빌드·서명을 다시 검증.
- 게시 절차: 사용자 요청에 따라 `milestone-release local 5.2.3` 경로 사용.
  도구가 커밋·태그·main atomic push·13개 GitHub 자산 재다운로드 검증을 담당.
- 실제 기기 Flash·전송 속도 실측·TFT/LED 실기 가독성·iPhone 내구 확인은 별도.

## 8. 실제 기기에서 확인할 순서

1. CORE 냉간 부팅 → ZERO BLE 광고 없음, 저장 Wi-Fi/NTP와 RTC 확인.
2. NOW → iPhone 재연결/AMS → 곡 변경·일시정지·진행률·앨범아트와 MAIN 인터넷 인계.
3. NOW↔CORE/MEDIA/AP 반복 → BLE 중지/복귀, AP 유지와 수동 NTP 완료/실패 시간.
4. OTA 확인·취소·다운로드·두 번 확인·MAIN 승인·ZERO 승인, 전원 차단 시 A/B/SAFE 복구.
5. Sync 큰 영상·느린 SD·네트워크 끊김/재개·BACK 취소와 기존 저장형/동기 재생.
6. SD와 TFT 동시 부하에서 깨짐·발열·버튼 누락 관찰, 새 정보/API의 loop/TFT/link 수치 기록.
7. 우상단 아이콘과 두 RGB LED를 대조하고 주야간 밝기/LED 끄기·12개 정보 페이지 가독성 확인.

정식 게시가 필요할 때 기존 서명 키 환경에서 프로젝트 루트에서 실행한다.

```bash
milestone-release local 5.2.3 "OTA·SPI·입력 안정성 및 보드 상태 표시 개선"
```
