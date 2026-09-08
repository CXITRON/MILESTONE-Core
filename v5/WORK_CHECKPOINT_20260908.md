# v5.1.6 이후 실기 수정 체크포인트

사용자 추가 지시에 따라 v5.1.7 릴리스·업로드 진행 중. 기존 UI 레이아웃과 서명키/파티션 유지.

## 확인한 원인과 수정

- Sync: loop 시작 `now`보다 Portal 제어 콜백의 `millis()`가 늦음.
  unsigned 뺄셈이 wrap되어 새 Playing 명령을 즉시 stale로 판정하고
  position을 영상 끝으로 clamp함. Pause는 stale 검사를 생략해 정상 표시.
  playback 호출 직전 새 시각 사용 및 순수 시간 함수의 음수 경과 방어.
  millis wrap/미래 anchor/6시간 frame 계산 회귀 테스트 추가.
- 앞선 화면 덮어쓰기 단독 원인 단정은 철회. Sync body 소유권 보호도 유지.
- OTA: 시작 실패가 busy 전이 감지에서 누락, upToDate를 실패 진단으로 기록,
  UART0 완료 로그 없음. 명시적 in-flight 완료 처리/3종 결과/TFT 유지/
  Portal 상태 및 MAIN·ZERO 준비/진행 timeout 오류를 추가.
- CORE: NEXT/PREV가 cycleMask를 무시. 마스크를 적용하고 저장/부팅 시
  현재 제외 화면을 유효 화면으로 이동. 모두 해제는 기존대로 저장 거절.
- Sync 장시간 영상: 기존 MVJ1을 유지하며 256KiB 묶음 순차 변환/업로드.
  6시간/432000프레임/2GiB 한도. 기존 고정 길이 업로드도 지원.
  기존 MSM1 짧은 영상 4096프레임/4MiB 한도는 아직 유지.
- v5 저장형 FPS 선택 15/20 추가, 재생 최저 간격 50ms. v3는 기존 10 유지.
  TFT 색상 보정/바이트 순서 유지하며 행 단위 SPI writeBytes로 비용 절감.
  Sync SD/JPEG/TFT 처리시간과 skipped frame 진단 추가. 20FPS 실기 미확정.

## 검증과 실기 상태

- test-core.sh 및 test-v5.sh 통과(신규 업로드 응답 유실 JS 실행 테스트 포함).
- MAIN/ZERO/SAFE 빌드 통과, 이후 수정 시 해당 대상 재빌드 필요.
- 빌드 위치 `/tmp/milestone-current-fixes-{main,zero,safe}`.
- 제품 공개키 `/home/citron/.config/milestone/keys/v5/public.pem` 사용.
- USB는 샌드박스 밖에서 확인 가능. MAIN ttyUSB0, ZERO ttyACM0.
- 수정 전 UART 실측: MAIN 5.1.6, ZERO는 **5.1.5** 실행 중.
  자동 업데이트 queued 및 ZERO BLE 중지/재개는 확인, 최종 결과 미표시 재현.
- 플래시 전 layout 백업 `/tmp/milestone-before-fixes-{main,zero}-layout.bin`.
  이 파일에는 NVS가 포함되어 있으므로 저장소에 추가하거나 출력하지 말 것.
- MAIN 수정 빌드 app0(0x210000) 유선 업로드/플래시 hash 검증 완료.
  MAIN 백업 `/tmp/milestone-before-fixes-main-image.bin`은 정식 5.1.6 BIN과 일치.
  NVS/SAFE/파티션/app1/OTA 선택 데이터는 기록하지 않음.
- 수정 MAIN 자동 OTA 실측 성공: ZERO에서 v5-bundle.txt와 서명을 받고
  `OTA signed catalog current: 5.1.6`, `OTA check complete: current (5.1.6)` 출력.
  이후 ZERO BLE 광고 복귀, 패닉/재부팅 없음.
- ZERO는 921600/460800 및 128KiB 분할 읽기에서도 0x4b000 부근에서
  `Packet content transfer stopped`로 백업 실패. ZERO에는 아직 쓰지 않았으며
  기존 5.1.5 유지. 실패한 partial 백업 파일을 복구용으로 사용하지 말 것.
- MAIN BIN SHA256: db2b665a87fde44d6d8fd4734ec301367b282e0780853535b1a101cfef1bf42d
- ZERO BIN SHA256: 40d23f548801ba82d7bfd9d8bb3fda6703f43b125ec968d69c81063d60f17a5c
- SAFE BIN SHA256: 088a5a9a0541183be8a7022ced009fee8dab1feba893e0c23dd37c6b64de1aac
- Sync 모바일 재생/높은 FPS 지속 측정/장시간 실측은 아직 미완료.
- 신규 JS 실행 테스트는 실제 변환 함수를 1200프레임으로 구동하여 전체
  출력 바이트 수/최대 전송 묶음 크기/응답 유실 중복 방지/6시간 초과 거절을 검증.
- 실기 acceptance 미완료: AP 수동 OTA 확인과 오류 화면, Sync 재생/pause/
  resume/seek, 15·20FPS 처리시간, CORE 제외 화면의 물리 버튼/전원 재인가.
- 이 작업은 전체 완료 상태가 아님. 특히 20FPS 안정 상한과 긴 영상 최종
  제품 한도는 실측 후 확정할 것. MSM1의 길이 한도 확장까지 구현됐다고 말하지 말 것.

## ZERO USB 읽기 해결

`esptool --no-stub` ROM 읽기로 문제 주소 0x4b000 및 전체 app0 1966080바이트
백업 성공. 기본 stub 경로 실패와 분리됨. ROM 방식으로 백업/검증을 수행한다.
백업: `/tmp/milestone-zero-rom-backup.bin`. 펌웨어 수정으로 해결한 문제가 아니며,
기본 stub 실패의 내부 원인까지 확정한 것은 아니다.

사용자가 수정·릴리스·전체 업로드를 추가 승인함. 테스트만으로 실기 완료 처리하지 말 것.
