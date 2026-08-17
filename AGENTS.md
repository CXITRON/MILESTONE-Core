# MILESTONE Core — Agent Instructions

This file is the first operational reference for coding agents working on this repository.
Read `MILESTONE_PROJECT_CONTEXT.md` and the relevant sections of `README.md` before changing firmware behavior.
The source code is always the final authority when documentation and implementation disagree.

## 1. Project baseline

- Product: MILESTONE Core
- Current firmware baseline: `1.9.1`
- Persistent config schema: `9`
- Hardware: Waveshare ESP32-S3-Zero + SH1107 128×128 OLED
- Main branch: `main`
- Repository: `CXITRON/MILESTONE-Core`
- Release transport: GitHub Releases + `MILESTONE_Core.json` manifest + `MILESTONE_Core.bin`
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
- host regression tests pass
- the fixed Arduino release build passes
- generated BIN/manifest exist and version/size/SHA-256 agree
- new commits covered by the current commit-hygiene policy use the expected Author and Committer identity
- those new commit messages contain no AI-tool attribution such as Codex/OpenAI/ChatGPT/etc.; historical commits are not rewritten solely for this check
- an existing version tag must point to the current HEAD or release stops
- `main` and the version tag are pushed atomically
- the GitHub Release contains `MILESTONE_Core.bin` and `MILESTONE_Core.json`

Do not bypass these checks merely to make a release succeed.

## 4. Versioning

Use patch releases (`1.8.2` → `1.8.3`) for bug fixes, validation hardening, contained backwards-compatible observability/diagnostics additions, small internal changes, documentation/tooling that accompanies a firmware correction, and other narrowly scoped compatible changes.
Use minor releases (`1.8.x` → `1.9.0`) for broad primary-product capabilities, incompatible behavior changes, or substantial firmware architecture changes.
Do not increment `CONFIG_VERSION` unless the persistent NVS schema actually changes and a migration path is implemented.

A tooling/documentation-only commit that does not change the firmware binary does not require a firmware version bump by itself.

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
