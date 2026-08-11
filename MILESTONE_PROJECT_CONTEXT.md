# MILESTONE Core — Project Context

> Current baseline: MILESTONE Core v1.8.2
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

It is an embedded application composed of several cooperative state machines. Reliability and recoverability are more important than cosmetic architectural purity.

## 2. Current source layout

```text
MILESTONE_Core/
├── AGENTS.md
├── MILESTONE_PROJECT_CONTEXT.md
├── MILESTONE_Core.ino
├── CoreConfig.inc
├── CoreDiagnostics.inc
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
├── PortalPage.h
├── UpdateCertificates.h
├── README.md
├── tests/
│   ├── test_core_logic.cpp
│   ├── test_core_diagnostics.cpp
│   ├── test_diagnostics_contract.sh
│   └── test_release_manifest.sh
└── tools/
    ├── make-release.sh
    ├── release-json.sh
    ├── test-core.sh
    ├── milestone-release
    └── install-milestone-release.sh
```

The `*.inc` runtime files are intentionally included into the sketch as one translation unit. Do not convert the whole firmware into independent `.cpp` modules only for style. `CoreLogic.cpp` and `CoreDiagnostics.cpp` are deliberate exceptions: they contain hardware-independent logic shared with host-side tests.

## 3. Version and configuration schema

Firmware and persistent schema versions are separate concepts.

```cpp
FIRMWARE_VERSION = "1.8.2"
CONFIG_VERSION = 8
```

Increment `CONFIG_VERSION` only when persistent NVS layout/meaning changes and implement a migration path. A firmware version change by itself must not force a schema reset.

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

### Diagnostics & Health

v1.8.2 adds `CoreDiagnostics.inc` plus host-testable `CoreDiagnostics.h/.cpp`. The runtime stores only the latest 16 significant events in a fixed-size circular history under the separate `milestone_diag` Preferences namespace. Each record stores numeric event/detail/value fields, uptime, and an epoch only when system time is valid. The history has a version, checksum, and fixed capacity; invalid/corrupt storage is reset without touching normal settings.

Important write-policy invariants:

- write only significant boot, Wi-Fi, NTP, OTA, rollback, or thermal events
- suppress the same event/detail for 60 seconds unless the event is explicitly forced
- never persist every loop, ordinary screen transitions, or RSSI fluctuations
- keep `CONFIG_VERSION` independent; diagnostics storage is not part of schema 8 migration
- diagnostics hooks observe existing state-transition outcomes and must not reorder the underlying state machine
- clearing diagnostics must not clear Wi-Fi/settings/rollback state; factory reset may clear the separate diagnostics namespace as part of a full wipe

The setup portal exposes `/api/diagnostics` and `/api/diagnostics/clear`, renders recent events newest-first, and provides a copyable support summary. Wi-Fi disconnect reason codes are diagnostics only and must not drive connection-state decisions.

## 5. Hardware-independent logic and tests

v1.8.1 introduced `CoreLogic.h/.cpp` so pure logic can be compiled on the host without the Arduino framework. v1.8.2 adds `CoreDiagnostics.h/.cpp` for persistent-history layout, validation, circular ordering, duplicate suppression, and event classification. It currently covers areas such as:

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

Then run from any directory:

```bash
milestone-release taildrop X.Y.Z "short release note"
```

The tool receives the archive into a staging directory, verifies it, runs tests and the firmware build before replacing the live project, and restores the previous project if a pre-publish failure occurs.

The tool also verifies Git identity, repository/branch, version consistency, generated manifest size/SHA-256, commit attribution, tag safety, and GitHub authentication. It pushes `main` and the release tag atomically and publishes both OTA assets with GitHub CLI.

Use `--dry-run` to validate/test/build without committing, replacing the live project, tagging, pushing, or publishing:

```bash
milestone-release --dry-run local X.Y.Z "short release note"
milestone-release --dry-run taildrop X.Y.Z "short release note"
```

Use `--yes` only when an unattended final publish is explicitly desired.

## 8. Areas intentionally not refactored through v1.8.2

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
milestone-release taildrop X.Y.Z "release note"
```

The agent should select the mode according to whether the source was edited directly on the PC or delivered as a Taildrop ZIP.


### Taildrop 원격 이력 조정

`milestone-release taildrop`은 ZIP의 HEAD가 최신 `origin/main`을 포함하지 않으면, 양쪽이 공유하는 가장 최신 릴리즈 태그 이후의 Taildrop 커밋만 최신 `origin/main` 위에 재적용합니다. 충돌하거나 안전한 공통 태그를 찾지 못하면 기존 프로젝트를 교체하거나 원격에 push하지 않고 중단합니다. 강제 push는 사용하지 않습니다.

### Already-received ZIP

For recovery or automation, `milestone-release --zip /path/to/MILESTONE_Core_X.Y.Z.zip taildrop X.Y.Z "notes"` runs the same staging/reconciliation/release pipeline without fetching another Taildrop file. Normal user-facing usage remains `milestone-release taildrop ...`.
