# v5 구현 진행표

기준: 2026-09-07.
계획한 v5 소프트웨어 경로는 모두 소스에 연결됐다. 현재 소스 기준은 v5.1.1이며,
아래의 호스트·컴파일 검증과 v5.1.0 기본 배선
실기 검증은 장시간 무선·전원 차단·업데이트 내구 시험을 대신하지 않는다.

## 반영 완료 범위

| 계획 | 연결 코드 | 구현 범위 |
|---|---|---|
| 듀얼 링크 | 두 `.ino`, Protocol/Transport | 512바이트 DMA mailbox, CRC, HELLO/버전 협상, ACK 제한 재시도, 중복 응답 재사용, heartbeat와 stale 복구 |
| 주변장치 | V5Hardware/V5Environment | TFT·5버튼·SD·DS3231·AHT20 온습도, 장치별 실패 격리, SD mount 실패 시 자동 포맷 금지 |
| 프로필·CORE | V5CoreViews, MAIN | 단일 CORE/MEDIA/NOW 런타임, quiesce 전환, 전면 MODE의 설정 AP 포함 7항목 메뉴, 기존 U8g2 구성의 7개 CORE 화면, 정보 화면 OK 넘김 |
| 화면 설정 | V5Portal/V5CoreViews/V5Tft | 날짜·문구·요소별 RGB565·톤·정렬·긴 문구 스크롤·자동 순환·번인 이동·화면 자동 끄기, PSRAM 이중 framebuffer와 8×8 dirty tile |
| 상태 띠·LED | V5Hardware, ZERO | 외부 환경 상단, 두 칩 내부 온도 하단, 무선 의미 아이콘, 보드별 로컬 상태 LED와 주야간 밝기 |
| 환경 설정·로그 | V5Environment/V5EnvironmentLog/V5Portal | AHT20 0x38·CRC-8, 섭씨/화씨·온습도 보정·주기·표시 mask·경고/위험 임계값·재검색, 일자별 CSV와 CRC write-ahead journal; BMP280 기압 제외 |
| 열 보호 | ThermalPolicy, 세 `.ino` | MAIN/ZERO/SAFE의 경고·80MHz 감속·중지 히스테리시스와 센서 오류 보호 |
| 로컬 MEDIA | V5Video/V5Hardware | BMP 사진, MVJ1 JPEG 영상, 목록·이전/다음·재생/일시정지·반복·정렬·흑백·PSRAM 선읽기·손상 항목 차단; 실시간 프레임 스트리밍 미포함 |
| 단계형 동기 MEDIA | V5SyncMedia/V5SyncSocket/V5SyncPage | 브라우저 전체 변환→SD 임시 업로드→CRC·JPEG 전수검증과 MVX1 인덱스→브라우저 오디오 기준 재생, 2.5초 제어 timeout, BACK/AP 종료 시 정리 |
| PC 변환 | convert-v5-media.py | ffmpeg 로컬 변환, 128×128 MVJ1 크기·CRC·프레임 제한과 기존 출력 덮어쓰기 거부 |
| BLE NOW | V5AmsRuntime/V5Ams | AMS 연결·보안·재요청·광고 복구, bounded UTF-8 메타데이터, 4개 NOW 배치와 진행률 |
| Wi-Fi·NTP | WifiStore/V5Network/V5Radio | 최대 8개 Personal/Open/PEAP A/B 자격 증명, 15초 연결 시험과 2초 안정 후 저장, 양 보드 복제, 제한 재시도·절전·NTP→RTC |
| v3 가져오기 | MilestoneV5Legacy | schema 12 이하 CORE/화면/시스템 설정과 legacy Wi-Fi A/B를 v5 namespace로 일회성 복사; v3 namespace는 변경하지 않음 |
| 동적 작업 | V5Radio/Runtime | 포털 접속자·ZERO BLE·STA·OTA 상태에 따른 MAIN/ZERO 작업 배정, 시간 제한 lease, 취소·AP 복구·BLE 일시 중지 |
| 앨범아트 | V5ArtworkWorker/V5Artwork | MAC1 다운로드, 곡별 transfer ID·청크·CRC 검증, SD 우선 사용과 임시 파일 readback 후 교체 |
| 아트 캐시·포털 | V5ArtworkIndex/V5ArtworkPortal | 2GiB/1GiB LRU, AUTO/CUSTOM/BLOCKED/MISSING, CUSTOM 보호, A/B 영구 인덱스 복구, 검색·미리보기·업로드·교체·삭제·고정·차단·재요청·완료 polling |
| 진단 | MilestoneV5Diagnostics/V5Portal | A/B CRC NVS 최근 16개 이벤트, 상태·오류·온도·센서·저장소 표시, 복사와 진단 이력 초기화 |
| HTTPS 묶음 | DownloadWorker/V5Download/V5BundleDownload | 고정 GitHub Release 자산, CA 검증·허용 host redirect·길이/시간/PSRAM ring 제한, MAIN 또는 ZERO 다운로드, SD 저장 후 서명·대상·버전·peer·크기·SHA 검증; 다운로드 task는 SD/SPI/Flash에 직접 접근하지 않음 |
| MAIN/ZERO OTA | V5SdUpdate/V5ZeroUpdate/V5OtaReceiver | 서명과 전체 해시 선검사, inactive slot 설치, IDF image validation, 실행 partition/ELF에 묶인 부팅 수신증과 지연 승인 |
| 동반 업데이트 | V5BundleUpdate/Bundle | immutable SD set과 NVS journal, MAIN 승인→선택적 ZERO 승인→10분 안정화→Stable/Backup A/B 승격, Recovery 보존 |
| 독립 SAFE | MilestoneV5Safe/SafetyRuntime | 2MiB factory 앱, 네트워크 없는 TFT·5버튼 메뉴, MAIN A/B 부팅, 서명된 Stable/Backup/Recovery 복원, SD/RTC 진단과 2회 확인 |
| 파티션·초기 이미지 | partitions.csv/release-v5.sh | MAIN/SAFE 공통 16MiB 표, factory SAFE + 6MiB MAIN A/B, ZERO 4MiB merged image, 고정 offset byte 검증 |
| 서명·릴리스 | prepare-v5-sd-restore.py/v5-release-assets.py/make-release/milestone-release | P-256 공개키 강제 포함, MAIN/ZERO manifest·bundle·catalog 서명, 역할/버전/스트림 제거/BLE 경계/이미지 offset 검증, 기존 단일 릴리스 명령에 통합 |

## 자동 검증 결과

- `./tools/test-core.sh`: 기존 v3.3.4 회귀, 문서, Taildrop와 GitHub 자산
  복구 계약 통과.
- `./tools/test-v5.sh`: wire CRC/순서/재시도/lease, 설정·날짜·RTC·환경·열,
  MVJ1/MAC1, OTA parser/수신기, bundle journal과 Stable index, Wi-Fi·시스템
  A/B, artwork A/B index, HTTPS 저장 흐름, 단계형 동기 MEDIA 계약,
  임시 P-256 키 서명·변조 거부 통과.
- `V5_SANITIZE=1`의 ASan/UBSan 조건도 통과. 환경 제약 때문에
  LeakSanitizer만 비활성화했다.
- Arduino-ESP32 3.3.11 제품 설정으로 MAIN, ZERO, SAFE를 제품 공개키와 함께
  빌드했다. v5.1.0 Release 앱 자산은 각각 1,745,632 / 1,345,344 /
  594,080바이트이며 지정 파티션 한도 안이다.
- 로컬 제품키로 MAIN/ZERO/SAFE 앱, MAIN/ZERO 초기 USB 이미지, 서명
  manifest/bundle/catalog 13개 자산을 생성하고 모든 SHA-256·서명·offset
  계약을 재검증했다.
- `milestone-release local 5.1.0`이 커밋·태그·`origin/main`을 같은 커밋으로
  맞추고 GitHub Release 게시와 13개 자산 재다운로드 검증을 완료했다.

## v5.1.0 배포 및 장치 확인

- 공개 릴리스: <https://github.com/CXITRON/MILESTONE-Core/releases/tag/v5.1.0>
- MAIN 제품 슬롯 `0x210000`과 ZERO 앱 슬롯 `0x10000`에 유선 업로드했으며,
  기존 NVS를 지우지 않고 esptool의 기록 직후 해시 검증을 통과했다.
- MAIN이 32KiB loopTask stack으로 setup과 main loop에 진입했고 기존
  LoadProhibited/TLSF assert/반복 재부팅은 재발하지 않았다.
- MAIN과 ZERO가 protocol v1/capability `0x0000001F`를 협상했고 ZERO에서
  v5.1.0 식별 문자열, BLE/AMS 광고와 연속 유효 링크 프레임을 확인했다.
- 단계형 동기 MEDIA의 실제 휴대폰 전체 변환·업로드·오디오 동기 재생은
  아래의 사용자 실기 검증 범위에 남긴다.
- 복구된 TFT 배치의 픽셀 단위 육안 확인과 실제 설정 AP 탐색·저장 반복 시험은
  아래의 장기 실기 검증 범위에 남긴다.

## 배포 후 추적 검증

아래 항목은 소프트웨어 누락이 아니라 실제 사용 중 계속 확인할 내구 검증이다.

- 제품 P-256 공개키가 포함된 MAIN/ZERO/SAFE와 서명 카탈로그의 OTA 왕복을
  실제 릴리스에서 반복 확인한다. 비공개키는 기기와 저장소에 넣지 않는다.
- MAIN-ZERO SPI를 목표 배선 길이와 전원 조건에서 장시간 돌려 오류율·복구 시간,
  ZERO 분리 시 MAIN 단독 동작을 확인한다.
- Personal/Open/PEAP, AP+STA, iPhone AMS, BLE 중 HTTPS 우선 전환과 복귀를
  실제 공유기·iPhone에서 반복한다.
- SD 제거·쓰기 실패·전원 차단을 각 journal/rename/OTA 단계에 주입하고
  Stable/Backup/Recovery 및 A/B rollback을 실제 Flash에서 확인한다.
- 사진과 MVJ1의 프레임률·발열·PSRAM 여유, 앨범아트 서버 가용성, 10분 안정화와
  장시간 열 보호를 실측한다.
- 복구된 TFT의 중앙정렬·상태 아이콘·MODE/AP 화면과 주변 Wi-Fi 검색·저장을
  사용자 실기 화면에서 최종 확인한다.

실기 결과로 임계값이나 핀/패널 보정이 달라지면 제품 계약을 바꾸는 것이 아니라
해당 하드웨어 상수와 회귀 테스트를 함께 조정한다.
