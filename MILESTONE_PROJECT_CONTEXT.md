# MILESTONE Core — Project Context

> Current baseline: MILESTONE Core v1.10.7
> Hardware: Waveshare ESP32-S3-Zero + SH1107 128×128 OLED
> Repository: `CXITRON/MILESTONE-Core`

This document gives coding agents and maintainers the architectural context needed before modifying the firmware. `AGENTS.md` contains operational rules, especially the release workflow. `README.md` contains user-facing behavior and detailed version history. When documentation conflicts with implementation, inspect the current source and treat the source as authoritative.

## 1. Product scope

MILESTONE Core is an ESP32-S3 desktop display firmware with:

- D-day, date, time, and message views
- selectable/automatic screen cycling
- BOOT-button interaction
- local captive configuration portal
- multiple remembered Wi-Fi networks
- WPA2-Enterprise PEAP support
- NTP time synchronization and offline fallback
- RGB status LED
- internal temperature display and thermal protection
- GitHub Release based HTTPS OTA updates
- post-OTA candidate validation and application-level rollback
- device diagnostics for memory, network, update, rollback, and thermal state
- persistent recent Diagnostics & Health event history with portal copy/clear tools
- browser-converted 128×128 monochrome photos, GIFs, and short local videos
- isolated `/stream` live playback with browser-side full preconversion, raw binary transport, PSRAM buffering, and stream-specific runtime isolation

It is an embedded application composed of several cooperative state machines. Reliability and recoverability are more important than cosmetic architectural purity.

## 2. Current source layout

```text
MILESTONE_Core/
├── AGENTS.md
├── MILESTONE_PROJECT_CONTEXT.md
├── MILESTONE_Core.ino
├── CoreConfig.inc
├── CoreDiagnostics.inc
├── CoreMedia.inc
├── CoreRollback.inc
├── CoreDisplay.inc
├── CoreNetwork.inc
├── CoreUpdate.inc
├── CorePortal.inc
├── CoreRuntime.inc
├── CoreLogic.h
├── CoreLogic.cpp
├── CoreDiagnostics.h
├── CoreDiagnostics.cpp
├── CoreMedia.h
├── CoreMedia.cpp
├── PortalPage.h
├── StreamPage.h
├── UpdateCertificates.h
├── README.md
├── tests/
│   ├── test_core_logic.cpp
│   ├── test_core_diagnostics.cpp
│   ├── test_core_media.cpp
│   ├── test_diagnostics_contract.sh
│   ├── test_media_contract.sh
│   ├── test_stream_page.js
│   ├── test_release_reconcile.sh
│   └── test_release_manifest.sh
└── tools/
    ├── make-release.sh
    ├── release-json.sh
    ├── test-core.sh
    ├── milestone-release
    └── install-milestone-release.sh
```

The `*.inc` runtime files are intentionally included into the sketch as one translation unit. Do not convert the whole firmware into independent `.cpp` modules only for style. `CoreLogic.cpp`, `CoreDiagnostics.cpp`, and `CoreMedia.cpp` are deliberate exceptions: they contain hardware-independent logic shared with host-side tests.

## 3. Version and configuration schema

Firmware and persistent schema versions are separate concepts.

```cpp
FIRMWARE_VERSION = "1.10.7"
CONFIG_VERSION = 9
```

Increment `CONFIG_VERSION` only when persistent NVS layout/meaning changes and implement a migration path. A firmware version change by itself must not force a schema reset.

Schema 9 appends `CUSTOM_MEDIA` as View 7 and TopMode 8 without renumbering the existing values. The v8→v9 migration appends View 7 to the saved order but leaves cycle-mask bit 7 off, preserving the user's previous visible cycle until explicitly enabled.

## 4. Runtime architecture

### Configuration

`CoreConfig.inc` owns persistent settings and migration logic. Wi-Fi credentials use an A/B bank strategy so an interrupted write does not destroy the last known-good list. `loadConfig()` is effectively migration code and should be treated as high-risk.

### Display

`CoreDisplay.inc` renders the OLED views, status indicators, thermal status, and device information. Display-only changes are lower risk than persistent-state or OTA changes, but timing and screen-cycle interactions still matter.

### Network and time

`CoreNetwork.inc` manages Wi-Fi connection, Enterprise PEAP, setup AP behavior, DHCP stabilization, NTP synchronization, retries, and Wi-Fi sleep behavior. Cold-boot sequencing was hardened because setup-mode connectivity and autonomous reboot connectivity previously behaved differently.

### OTA update

`CoreUpdate.inc` checks the GitHub Release manifest and performs the HTTPS OTA transaction. Important invariants include TLS verification, manifest validation, content length, SHA-256 verification, temperature/resource guards, connection-stall handling, `Update.end()` success, and reboot ordering.

Do not split the OTA transaction merely because the function is long. Its sequential structure encodes safety assumptions.

### Rollback

`CoreRollback.inc` was added in v1.8.0. Before installing a candidate, the previous application slot/version is recorded. A candidate boot remains in validation state until approximately 10 seconds of normal runtime have completed. A reset before confirmation can cause the previous OTA slot to be selected again.

Application-level rollback cannot recover a candidate that fails before the rollback guard itself executes; full early-boot recovery depends on bootloader rollback support.

### Runtime/input/thermal

`CoreRuntime.inc` coordinates the main loop, BOOT-button behavior, screen cycling, temperature protection, and high-level state progression. Preserve cooperative/early-return semantics when modifying state processing.

### Live streaming (v1.10.5+, sender pacing revised in v1.10.7)

Live streaming is intentionally isolated from the normal cooperative runtime. `/stream` serves a dedicated lightweight browser page. The browser pre-converts the full source into 128×128 one-bit frames before playback, then sends paced `application/octet-stream` bodies. The ESP32 stores only a bounded PSRAM ring; live frames are never written to LittleFS.

While `mediaStreamActive` is true, `loopFirmware()` enters `processMediaStreamMode()` and returns before normal OTA, diagnostics, cycle, display, LED, NTP/reconnect scheduling, or stored-media work can run. The stream loop services only captive-portal networking, the raw frame transport, PSRAM queue/OLED timing, BOOT-stop input, resource guards, and low-rate thermal checks. A stream started inside `processNetwork()` is detected immediately and also returns before any normal background subsystem runs.

Important stream contracts:

- PSRAM is required; the ring capacity is 32 frames.
- Initial playback starts after 8 queued frames and recovers from underrun after 4 frames.
- Raw pushes carry at most 8 frames and receive a compact binary ACK; steady playback normally paces two frames per request, and transport-loss retries reuse the same idempotent sequence/body.
- On Arduino-ESP32 3.3.11, raw-body routes do not expose URL query metadata through `server.arg()` during `RAW_START`; live raw pushes therefore carry session, sequence, and frame-count metadata in explicitly collected `X-MILESTONE-*` request headers. Do not move these fields back into the push URL.
- Duplicate committed sequence numbers are consumed and ACKed without enqueueing frames twice.
- 20fps is the normal high-performance target; 24fps is experimental. The renderer preserves the media timebase by dropping stale frames rather than accumulating latency.
- The SH1107 path may update a changed tile span instead of flushing all 2048 bytes when that is cheaper.
- Stream thermal policy is 75°C warning, 80°C controlled stream stop, 85°C emergency thermal-safe entry.
- End-of-source is explicit: `/api/stream/finish` marks EOF and the device drains the real queue before leaving STREAM_MODE, including short sources below the normal prebuffer threshold.
- The OTA 2KiB transfer scratch buffer is allocated only during an OTA download instead of permanently occupying internal SRAM while streaming.
- A stream cannot start while the normal thermal throttle/safe state is active.
- Resource guards stop the stream on low internal heap, largest-block, or loopTask stack headroom.
- STREAM_MODE pins the setup portal and suspends normal background state machines; exit restores the previous CPU policy and normal runtime.

### Diagnostics & Health

v1.8.2 adds `CoreDiagnostics.inc` plus host-testable `CoreDiagnostics.h/.cpp`. The runtime stores only the latest 16 significant events in a fixed-size circular history under the separate `milestone_diag` Preferences namespace. Each record stores numeric event/detail/value fields, uptime, and an epoch only when system time is valid. The history has a version, checksum, and fixed capacity; invalid/corrupt storage is reset without touching normal settings. v1.8.3 makes diagnostic writes transactional in RAM/NVS, tracks duplicate suppression per event/detail pair even when other events are interleaved, and records boot validation only after the runtime reaches a stable operating state.

Important write-policy invariants:

- write only significant boot, Wi-Fi, NTP, OTA, rollback, thermal, or isolated media-failure events
- suppress the same event/detail for 60 seconds unless the event is explicitly forced
- never persist every loop, ordinary screen transitions, or RSSI fluctuations
- keep `CONFIG_VERSION` independent; diagnostics storage is not part of schema 9 migration
- diagnostics hooks observe existing state-transition outcomes and must not reorder the underlying state machine
- clearing diagnostics must not clear Wi-Fi/settings/rollback state; factory reset may clear the separate diagnostics namespace as part of a full wipe

The setup portal exposes `/api/diagnostics` and `/api/diagnostics/clear`, renders recent events newest-first, and provides a copyable support summary. Wi-Fi disconnect reason codes are diagnostics only and must not drive connection-state decisions.

### Custom media

v1.9.0 adds `CoreMedia.inc` and host-testable `CoreMedia.h/.cpp`. JPEG/PNG/WebP, GIF, and browser-supported local video are decoded in the setup browser, converted to 128×128 one-bit page-major frames, and uploaded as checksummed `MSM1` files. The ESP32 never stores or decodes the original codec.

v1.9.1 hardens the browser-side video path without changing the MSM1 format or LittleFS transaction. Image/GIF and video choosers are separated, MP4/M4V/MOV extension fallback covers missing provider MIME types, and bounded metadata/frame waits prevent an iOS/WebKit load from hanging indefinitely.

v1.9.2 removes the native `accept` filter after Safari was observed returning no file for a selected video. One unrestricted chooser now accepts every provider result and validates it in JavaScript, while an iPad drag-and-drop target provides an input-independent fallback.

v1.9.3 instruments the complete browser import pipeline instead of treating an empty chooser as a codec failure. Dedicated photo, video, and unrestricted Files-provider inputs listen for both `input` and `change`, poll the input as an event-loss fallback, show elapsed waiting/metadata/frame-conversion stages, probe the selected File before conversion, and expose a copyable client-side trace. A 180-second chooser watchdog distinguishes an operating-system/provider handoff timeout from later video decoding or MSM1 conversion failures.

v1.9.4 handles an observed WebKit sequence in which the native picker returns `cancel` with `files=0` after the user selects a video. An empty cancel is now provisional: the 180-second `files` polling watchdog remains active so a delayed provider handoff can still complete. The active input is no longer cleared during its first click, avoiding mutation of an empty file control while WebKit begins native picker activation.

v1.9.5 reflects the confirmed iCloud-provider failure mode observed on iPhone/iPad: videos that cannot play in the Photos picker can return `cancel` with `files=0`, while the same items succeed when cellular data or internet access lets Photos fetch the original. An empty cancel now ends the chooser wait immediately with a clear `파일 전달 실패` state and guidance to enable mobile data/internet or pre-download/play the video. The 180-second watchdog remains only for cases where no terminal picker event is delivered. The client trace now carries an explicit phase across chooser, file-read, conversion, and device-upload stages.

v1.9.6 attempted to harden single-item deletion by closing playback before catalog/file mutation and by rolling the catalog back when physical file removal failed. Its feedback was rendered in the upper conversion-info area, however, so it was not visible where deletion is performed and the rollback strategy could make a successfully removed catalog entry reappear.

v1.9.7 hardened the server-side media delete transaction and added post-delete list verification, but its browser regression test invoked the delete function directly instead of exercising the rendered delete button. That left a client-side activation failure undetected on the real portal.

v1.9.8 fixes the remaining client-side delete activation path. Single-item deletion no longer depends on a modal confirm() call or a per-button onclick closure: the media list uses delegated click handling, the first tap immediately arms an 8-second confirmation state, and a second tap sends the delete request. Dynamic media buttons are explicit type=button controls wired through addEventListener. The v1.9.7 server-side catalog/file deletion recovery and post-delete list verification remain in place.

v1.9.9 removes the browser-side 300-frame and 40-second conversion ceilings. The portal now uses the MSM1 format limit of 1024 frames directly; requested duration is bounded only by source length, 1024/fps, and the existing 160 KiB encoded-media limit. The UI states the theoretical frame-limited durations for 5/8/10 fps.

v1.10.0 adds live media streaming without changing the stored MSM1 format. While the setup portal remains open, a phone or PC browser can decode a local video or a browser-readable direct media URL, convert each frame to the same 128x128 one-bit U8g2 page layout, and push bounded batches to a 12-frame runtime queue. The stream is never written to LittleFS, so the 1024-frame and 160 KiB stored-media limits do not apply. YouTube page/iframe URLs are intentionally rejected because the browser cannot directly sample pixels from the cross-origin embedded player.

v1.10.1 hardens setup-AP coexistence during streaming. The original v1.10.0 portal defaulted to 20 fps even though a 2048-byte full-frame SH1107 transfer over the verified 400 kHz I2C bus consumes most of a 50 ms frame interval, which can starve WebServer/DNS servicing. v1.10.1 defaults the portal to 10 fps, accepts source timing up to 24 fps but caps physical OLED refresh at 15 fps, exposes the actual render rate in stream status, extends the stale window to 5 seconds, and treats an active stream as portal activity so AP timeout cannot close the setup portal during long playback.

v1.10.2 fixes setup-mode exit semantics at stream start. A live stream now explicitly pins the setup AP, cancels any pending post-Wi-Fi-test portal close, and suppresses both close-after-success and ordinary AP timeout paths until streaming ends. The OLED also remains on the setup screen until the first stream frame is actually received, rather than allowing the stream state to take display ownership before usable media exists.

Media uses LittleFS with a runtime limit of `min(160 KiB, totalBytes - 16 KiB)`, at most 64 items, generated internal filenames, a streamed temporary upload, full size/CRC/frame validation, and A/B generation indexes. `LittleFS.begin(false)` is mandatory after initialization: a later mount failure disables only media and must never silently format user data. Explicit `REPAIR` and factory reset may format/delete media. OTA and media upload are mutually exclusive flash writers.

The first MSM1 frame is a 2,048-byte raw U8g2 page buffer. Later frames use raw data or bounded XOR+RLE deltas. Runtime playback reads one frame at a time, prefers PSRAM, skips corrupt items, and yields to boot, portal, reset, OTA, and thermal safety screens. Media files survive OTA and rollback; v1.8.x ignores the filesystem.

## 5. Hardware-independent logic and tests

v1.8.1 introduced `CoreLogic.h/.cpp` so pure logic can be compiled on the host without the Arduino framework. v1.8.2 adds `CoreDiagnostics.h/.cpp`, and v1.9.0 adds `CoreMedia.h/.cpp` for the checksummed media container and bounded frame decoder. Tests cover areas such as:

- semantic version parsing/comparison
- ISO date validation
- civil-date D-day calculations
- cycle-order validation
- SHA-256 string validation
- JSON escape decoding shared by manifest parsing
- diagnostic history checksum/validation and corruption fallback
- 16-entry circular ordering and pre-NTP timestamp handling
- diagnostic duplicate-suppression behavior and event classification
- portal/API endpoint contract for diagnostics view/copy/clear
- MSM1 header/CRC/duration validation and raw/XOR-RLE reconstruction
- custom media and live-stream portal routes, schema migration, factory-reset, and OTA exclusion contracts

Run:

```bash
./tools/test-core.sh
```

The official firmware release build runs these tests automatically before invoking Arduino CLI.

## 6. Official build configuration

The release build source of truth is `tools/make-release.sh`. It compiles using the Waveshare ESP32-S3-Zero board definition with the project-approved PSRAM and OTA partition configuration. Do not manually substitute an arbitrary exported BIN for a release.

A successful build produces:

```text
release/MILESTONE_Core.bin
release/MILESTONE_Core.json
```

The manifest records the firmware version, exact binary size, SHA-256, and release note.

## 7. Unified release command

The preferred operator workflow is the installed command:

```bash
milestone-release
```

Install/update it from the repository with:

```bash
./tools/install-milestone-release.sh
```

### Source edited locally

Run from the project root:

```bash
milestone-release local X.Y.Z "short release note"
```

This path does not use Tailscale or ZIP files.

### Source delivered through Tailscale Taildrop

An archive must be named exactly:

```text
MILESTONE_Core_X.Y.Z.zip
```

Then run from an existing directory outside the live project root:

```bash
cd /tmp && milestone-release taildrop X.Y.Z "short release note"
```

The tool receives the archive into a staging directory, verifies it, runs tests and the firmware build before replacing the live project, and restores the previous project if a pre-publish failure occurs.

The tool also verifies Git identity, repository/branch, version consistency, generated manifest size/SHA-256, commit attribution, tag safety, and GitHub authentication. It pushes `main` and the release tag atomically and publishes both OTA assets with GitHub CLI.

Use `--dry-run` to validate/test/build without committing, replacing the live project, tagging, pushing, or publishing:

```bash
milestone-release --dry-run local X.Y.Z "short release note"
cd /tmp && milestone-release --dry-run taildrop X.Y.Z "short release note"
```

Use `--yes` only when an unattended final publish is explicitly desired.

## 8. Areas intentionally not refactored through v1.10.7

The following broad refactors were deliberately rejected because their regression risk exceeded their immediate value:

- mass conversion of all global state into structs
- wholesale replacement of Arduino `String` with fixed buffers
- PlatformIO migration
- splitting every long state-machine function for line-count reasons
- early refactoring of `loadConfig()` migration logic
- cosmetic decomposition of the critical OTA transaction

Future changes should be driven by a concrete defect, feature requirement, or measured resource problem rather than code-style metrics alone.

## 9. Release handoff rule for agents

When an agent prepares a version, it should not stop at “build the release manually.” It should either execute the unified command when it has shell access, or tell the user exactly which command to run:

```bash
milestone-release local X.Y.Z "release note"
```

or

```bash
cd /tmp && milestone-release taildrop X.Y.Z "release note"
```

The agent should select the mode according to whether the source was edited directly on the PC or delivered as a Taildrop ZIP.


### Taildrop 원격 이력 조정

`milestone-release taildrop`은 ZIP의 HEAD가 최신 `origin/main`을 포함하지 않으면 공통 릴리즈 태그를 확인한 뒤, 원격의 실제 펌웨어 버전과 일치하는 Taildrop 쪽 최신 커밋을 기준점으로 삼아 그 이후 변경만 최신 `origin/main` 위에 재적용합니다. 이미 원격에 반영된 같은 버전의 준비 커밋을 다시 재생하지 않습니다. 충돌하거나 안전한 버전 기준점을 찾지 못하면 기존 프로젝트를 교체하거나 원격에 push하지 않고 중단합니다. 강제 push는 사용하지 않습니다. 릴리즈 성공 후 설치된 `milestone-release` 명령도 새 프로젝트의 도구로 갱신합니다.

### Already-received ZIP

For recovery or automation, `milestone-release --zip /path/to/MILESTONE_Core_X.Y.Z.zip taildrop X.Y.Z "notes"` runs the same staging/reconciliation/release pipeline without fetching another Taildrop file. Normal user-facing usage remains `milestone-release taildrop ...`.


v1.10.3 was the last incremental live-stream implementation. v1.10.5 replaces that hot path: browser-side full preconversion, a dedicated `/stream` page, raw binary transport, a 32-frame PSRAM queue, isolated STREAM_MODE execution, partial SH1107 updates, stream-specific thermal policy, and explicit resource/sequence recovery. v1.10.6 fixes the Arduino-ESP32 3.3.11 raw `WebServer` metadata incompatibility by carrying session/sequence/frame-count in explicitly collected headers instead of URL query arguments. v1.10.7 then removes the two-frame ACK-locked sender cadence: the browser fills 20 frames before playback, refills when the queue falls to 12, and drives it back toward 22–24 frames with batches of up to 8 while the ESP32 remains the sole playback clock. Do not reintroduce the v1.10.3 multipart/burst sender, query-based raw metadata, or a tiny browser lead buffer into the live path.
