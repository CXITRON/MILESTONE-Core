# MILESTONE Core — Agent Instructions

This file is the first operational reference for coding agents working on this repository.
Read `MILESTONE_PROJECT_CONTEXT.md` and the relevant sections of `README.md` before changing firmware behavior.
The source code is always the final authority when documentation and implementation disagree.

## 1. Project baseline

- Product: MILESTONE Core
- Current firmware baseline: `2.3.0`
- Persistent config schema: `10`
- Hardware: Waveshare ESP32-S3-Zero + SH1107 128×128 OLED
- Main branch: `main`
- Repository: `CXITRON/MILESTONE-Core`
- Release transport: GitHub Releases + CORE/MEDIA/NOW profile manifests and BIN assets
- Profile boundary: CORE owns all general/D-day views, MEDIA runs stored/live media only, and NOW runs iPhone AMS Bluetooth Now Playing only. MEDIA and NOW must not mutate persisted CORE view settings. This is a compile-time boundary: CORE and NOW contain no media/stream implementation, while CORE and MEDIA contain no Bluetooth runtime.
- Build/release source of truth: `tools/make-release.sh`
- Unified operator command: `milestone-release`

Do not introduce a second release workflow unless there is a concrete technical reason.
Do not recommend long manual `git push` / `git tag` / `gh release create` command sequences when `milestone-release` can perform the job.

## 2. Mandatory release workflow

After preparing a firmware version, actively use or recommend the unified release command.
Choose the mode from how the source reached the user's PC.

### Local work

Use this when the source was edited directly in the local repository.
The command MUST be run from the MILESTONE_Core project root:

```bash
cd /run/media/citron/T7/Documents/Dev/MILESTONE_Core
milestone-release local X.Y.Z "short release note"
```

If the agent has shell access to the user's machine, prefer invoking this command rather than manually reproducing its steps.
If the agent cannot execute it, tell the user the exact one-line command to run.

### Taildrop work

Use this when a completed archive named `MILESTONE_Core_X.Y.Z.zip` was sent to the PC with Tailscale Taildrop.
Run this mode from an existing directory outside the live project root so replacing the project cannot invalidate the caller's working directory:

```bash
cd /tmp && milestone-release taildrop X.Y.Z "short release note"
```

Do NOT directly `unzip -o` a received archive over the live repository as the normal workflow.
The release tool stages, validates, builds, and only then swaps the project directory.

### Install the command

If `milestone-release` is not installed, run from the project root:

```bash
./tools/install-milestone-release.sh
```

The installer copies the command to `~/.local/bin/milestone-release`.

## 3. What the release tool is responsible for

`milestone-release` is expected to enforce these invariants:

- requested version is strict `X.Y.Z` semantic version syntax
- `FIRMWARE_VERSION` exactly matches the requested version
- project is the expected `CXITRON/MILESTONE-Core` repository on `main`
- local Git identity is `CXITRON <cxitron@proton.me>`
- host regression tests pass, including documentation/version synchronization checks
- `AGENTS.md` and `MILESTONE_PROJECT_CONTEXT.md` baseline versions match `FIRMWARE_VERSION`
- `README.md` contains the current version entry and keeps version history in newest-to-oldest semantic-version order
- all three fixed Arduino profile builds pass (`CORE`, `MEDIA`, and `NOW`)
- all three generated BIN/manifest pairs exist and profile/asset/version/size/SHA-256 agree
- new commits covered by the current commit-hygiene policy use the expected Author and Committer identity
- those new commit messages contain no AI-tool attribution such as Codex/OpenAI/ChatGPT/etc.; historical commits are not rewritten solely for this check
- a new version tag must point to the current HEAD; repair of an existing Release keeps its tag fixed and requires that tag to be an ancestor of HEAD
- `main` and the version tag are pushed atomically
- the GitHub Release contains all six CORE/MEDIA/NOW BIN and JSON assets
- Taildrop hands publication to the incoming release command when it differs from the installed launcher, and every published asset is downloaded and SHA-256 checked after upload

Do not bypass these checks merely to make a release succeed.

## 4. Versioning

Use patch releases (`1.8.2` → `1.8.3`) for bug fixes, validation hardening, contained backwards-compatible observability/diagnostics additions, small internal changes, documentation/tooling that accompanies a firmware correction, and other narrowly scoped compatible changes.
Use minor releases (`1.8.x` → `1.9.0`) for broad compatible primary-product capabilities. Use a major release for a deliberately incompatible or product-wide architecture boundary such as the v2 multi-firmware profile split.
Do not increment `CONFIG_VERSION` unless the persistent NVS schema actually changes and a migration path is implemented.

A tooling/documentation-only commit that does not change the firmware binary does not require a firmware version bump by itself.

### Documentation synchronization

Documentation is part of every firmware release, not an optional follow-up. Before releasing a new firmware version:

- update the baseline version in `AGENTS.md` and `MILESTONE_PROJECT_CONTEXT.md`
- update user-facing current behavior and the implementation scope in `README.md`
- add exactly one `## vX.Y.Z 업데이트 안내` entry for the firmware version
- keep README version entries sorted newest-to-oldest by semantic version; intentional skipped versions do not require placeholder entries
- distinguish stored MSM1 media limitations from isolated live-streaming behavior so one mode is not documented as the other
- run `./tools/test-core.sh`; `tests/test_docs_contract.sh` must fail the release if these invariants drift

A documentation-only correction that does not change `FIRMWARE_VERSION` may be committed without creating a new firmware release.

## 5. High-risk areas

Treat the following as high-risk and avoid cosmetic refactors without a concrete benefit:

- `loadConfig()` and NVS schema migration
- Wi-Fi credential A/B bank handling
- `processNetwork()` state ordering
- NTP and cold-boot connectivity sequencing
- OTA download / SHA-256 / `Update.end()` transaction order
- `CoreRollback.inc` candidate validation and rollback state
- global OTA download buffer placement
- BOOT button timing and reset confirmation
- thermal protection
- diagnostics hooks placed on those state transitions
- `CoreMedia.inc` LittleFS mount/format policy and A/B media index commits

Function length or global-variable count alone is not sufficient justification to rewrite these paths.
Preserve working transaction/state-machine ordering unless the task specifically requires changing it.

Custom media is optional. A mount, allocation, validation, or playback failure must disable or skip media without blocking boot validation, time display, Wi-Fi, diagnostics, rollback, or OTA. Never change `LittleFS.begin(false)` into automatic format-on-failure; formatting is allowed only during first initialization or an explicit user-confirmed repair.

Live streaming is isolated in v1.10.5, the raw-request metadata contract is fixed in v1.10.6, v1.10.7 replaces the ACK-locked two-frame sender with PSRAM-watermark refill pacing, and v1.10.8 expands the jitter buffer, adds bounded RAW/XOR-RLE live frame records, and separates source timing from adaptive OLED service timing. v1.11.0 adds optional BLE Now Playing outside the stream hot path; v1.11.1 cancels background STA scan/connect work before setup AP entry, bounds scan dwell/connection attempts, and backs off when no saved network is visible; STREAM_MODE must suspend BLE advertising/connections on entry and resume the configured BLE service only after stream exit. Do not move the `/stream` sender back into the general portal hot path, reintroduce multipart/FormData for live frames, or put stream session/sequence/count/byte-length metadata back into URL query arguments; Arduino-ESP32 3.3.11 raw-body callbacks require those values to be explicitly collected as request headers. While `mediaStreamActive` is true, the dedicated stream loop must return before normal OTA, diagnostics, cycle, display, LED, NTP/reconnect scheduling, and stored-media work. Live frame storage and encoded-request scratch space belong in PSRAM; the binary frame-record transport must not write frames to LittleFS/NVS. STREAM_MODE must pin the setup AP, retain BOOT/thermal/resource safety, stop at the stream-specific 80°C limit, and restore normal runtime only on exit.

## 6. Testing policy

Prefer tests for hardware-independent logic before broad structural refactors.
Current host tests are run by:

```bash
./tools/test-core.sh
```

`tools/make-release.sh` already runs these tests before the Arduino build.
When adding new pure validation/calculation logic, prefer placing it in `CoreLogic.h/.cpp` so the firmware and host tests share the same implementation. Pure diagnostics storage/ring-buffer logic belongs in `CoreDiagnostics.h/.cpp`, and MSM1 parsing/CRC/frame reconstruction belongs in `CoreMedia.h/.cpp`; both must remain host-testable without Arduino dependencies.

Do not rewrite a working parser or state machine at the same time as first introducing its regression test unless the existing implementation itself is the bug being fixed. Diagnostics should observe existing transition result points; do not reorder Wi-Fi/NTP/OTA/rollback logic merely to make logging cleaner.

## 7. Commit hygiene

For commits created while assisting this project:

- Author: `CXITRON <cxitron@proton.me>`
- Committer: `CXITRON <cxitron@proton.me>`
- no `Co-authored-by` attribution to an AI tool
- no Codex/OpenAI/ChatGPT/Claude/Gemini/Copilot attribution in commit messages
- commit messages describe only the project change

Do not rewrite old history solely to remove historical metadata unless explicitly requested.

## 8. Handoff to the user

At the end of work that prepares a new firmware release, always state which command the user should run.
Use one of these exact patterns:

```bash
# Local repository work
milestone-release local X.Y.Z "release note"

# Archive delivered through Tailscale Taildrop
cd /tmp && milestone-release taildrop X.Y.Z "release note"
```

For local mode, remind the user that it must be run from the project root.
Do not make the user reconstruct the release sequence manually when this command is available.


### Taildrop 원격 이력 조정

`milestone-release taildrop`은 ZIP의 HEAD가 최신 `origin/main`을 포함하지 않으면 공통 릴리즈 태그를 확인한 뒤, 원격의 실제 펌웨어 버전과 일치하는 Taildrop 쪽 최신 커밋을 기준점으로 삼아 그 이후 변경만 최신 `origin/main` 위에 재적용합니다. 이미 원격에 반영된 같은 버전의 준비 커밋을 다시 재생하지 않습니다. 충돌하거나 안전한 버전 기준점을 찾지 못하면 기존 프로젝트를 교체하거나 원격에 push하지 않고 중단합니다. 강제 push는 사용하지 않습니다. 릴리즈 성공 후 설치된 `milestone-release` 명령도 새 프로젝트의 도구로 갱신합니다.
