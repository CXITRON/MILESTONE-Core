# 2026-09-12 OTA 수정 체크포인트

- 기준 릴리스 v5.2.0의 GitHub 게시·13개 자산 존재 확인. 작업 버전은 v5.2.1.
- 사용자 요청: 확인 중 전체 화면 금지, 결과만 전체 화면, Sync 간헐적 끊김,
  여유 공간이 충분한데도 앨범 이미지 캐시가 사라지는 증상. OTA만 사용.
- 실기 증상의 최종 원인은 아직 장치 로그/SD 파일과 대조하지 못했다.

## 코드·호스트에서 확인한 결함

1. 확인 중 다운로드 전용 전체 화면/버튼 차단 경로 진입.
2. Sync ACK 지연 시 제어 요청 3개가 동시 대기하고, 과거 재생 위치를 HTTP로 재전송.
3. 매 프레임 4바이트 인덱스 seek/read, 주기적 SD 캐시 탐색·공간 조회 및 동기 UART 송신.
4. 앨범아트는 SD 저장 실패여도 RAM 이미지를 표시. 저장 재시도 전에 곡이 바뀌면
   메모리가 교체되고, rename 실패로 남은 검증된 .tmp를 다음 부팅에서 사용하지 않음.
   이는 용량 부족 없이도 재현 가능한 결함이며 사용자 장치 원인으로 단정하지 않는다.

## 수정·검증

- 확인 아이콘/완료 본문 분리, 결과 화면을 미디어가 덮지 않도록 소유권 조건 추가.
- 단일 제어 요청·최신 상태 병합, HTTP 전환 시 현재 시각 재취득, seeking/waiting 반영.
- 고정 512바이트 인덱스 캐시, 연속 프레임 seek 제거, 재생 중 선택적 SD 작업 보류.
- 앨범 이미지 저장 상태·실패 원인 공개, 2초 저장 재시도 및 곡 전환 전 재시도,
  .tmp 오프라인 복구와 최종 파일 재검증, 일시적 읽기 오류의 파일 보존·재시도.
- 자동 캐시 삭제는 2GiB 예산 초과로 제한. SD 여유 1GiB는 신규 저장 허용 조건만 유지.
- 실제 생산 클래스를 파일 기반 SD mock에서 실행: 300프레임 연속 재생에서
  인덱스 읽기 3회·영상 seek 1회, pause/seek/지연 따라잡기, 캐시 재부팅 보존,
  여유 공간 충분한 기록 실패/rename 실패/임시 캐시 복구 테스트 통과.
- JPEG/TFT/SD 지연은 mock이므로 실제 FPS·무선·SD 문제의 최종 해결을 주장하지 않는다.
- 최종 공식 MAIN/ZERO/SAFE 빌드·서명 및 GitHub 게시 결과는 아래와 같다.
- 유선 업로드/안정 버전 지정은 수행하지 않는다.

## 최종 배포 확인

- `milestone-release --yes local 5.2.1` 공식 경로 완료.
- 릴리스 커밋/태그: `d0a0060`, `v5.2.1`. main/tag 원자적 push 완료.
- https://github.com/CXITRON/MILESTONE-Core/releases/tag/v5.2.1 정식 게시.
- MAIN/ZERO/SAFE 빌드, 기존 제품키 서명, 13개 자산의 서명/해시/보드/offset 검증,
  게시 후 필수 자산 재다운로드 검증 완료. 키·파티션·미디어 형식 변경 없음.
- `./tools/test-core.sh`, `./tools/test-v5.sh` 모두 통과. 새 SD 재생/캐시 runtime
  테스트는 ASan/UBSan도 통과 (환경 제약상 LeakSanitizer만 비활성화).

## 남은 실기 확인과 위험

- 사용자 OTA 설치 후 확인 중 본문 유지와 최신/실패/업데이트 결과 화면 확인.
- 실제 휴대폰·SD의 Sync pause/resume/seek와 간헐적 끊김을 확인.
  `/api/sync/status`의 read/decode/output/max_frame/max_render_gap/skipped 값을
  비교하면 저장 매체 지연과 제어 지연을 분리할 수 있다.
- `/api/now-config`의 artwork_cache_key/persisted/storage_status/save_failures로
  실제 삭제, 미저장, 다른 키 조회를 구분한다. UART0에도 저장 성공/실패·삭제 이유를 기록.
- 넉넉한 SD에서 쓰기/rename 실패는 호스트에 재현했지만 사용자의 실제 캐시 소실
  원인을 장치 로그·SD 파일과 대조한 것은 아니다. 실제 SD 자체가 계속 쓰기에 실패하면
  저장 성공을 보장하지 않으며 오류를 표시하고 기존 파일/검증된 임시 파일을 보존한다.
- 이전 버전에서 SD/임시 파일 모두에 남지 않은 RAM 이미지는 재부팅 후 복원할 수 없다.

## 변경 파일

- MAIN `.ino`, V5BundleDownload: 확인 화면/버튼/결과와 미디어 출력 충돌 방지.
- V5SyncPage, V5SyncMedia, V5Portal: 직렬화 제어·최신 위치·인덱스 캐시·진단.
- V5Artwork, V5ArtworkPortal: 캐시 보존·저장 재시도·임시 파일 복구·상태 API.
- tests/test_v5_sync_control.js, tests/test_v5_media_runtime.cpp 및 기존 테스트:
  실패 재현과 실제 생산 클래스의 회귀 확인. mocks는 실제 JPEG 처리 성능 검증이 아님.
