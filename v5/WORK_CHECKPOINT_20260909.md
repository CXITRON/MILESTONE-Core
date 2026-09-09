# 2026-09-09 OTA directory / NOW progress investigation

Baseline: 5.1.8. Unreleased local changes. User subsequently approved MAIN-only
USB diagnostic installation because OTA was blocked.

- Confirmed NOW timing defect: MAIN default heartbeat is 1000 ms; ZERO alternates
  status and AMS metadata. Elapsed seconds therefore arrive about every 2 s.
  MAIN now polls at 250 ms in NOW only, retaining transfer/retry priorities and
  using the real ZERO playback position (no speculative playback interpolation).
- OTA directory failure root cause is NOT yet established on hardware. Original
  combined condition obscured which mkdir failed and could clean an existing
  directory on a name collision. Retry /firmware creation explicitly, report
  failing path/errno to portal and both serial outputs, and never clean a
  collided directory. No format, remount, signing or partition changes.
- Host core/v5 suites passed, including missing-parent and parent-file
  preservation tests. MAIN compile passed: 1,759,855 bytes, globals 64,884 bytes.
  ZERO/SAFE unchanged and not rebuilt for these unshipped diagnostic edits.
- USB inventory returned no /dev/serial/by-id. Need hardware access or the new
  `OTA directory failure: path=... errno=...` log to distinguish filesystem,
  write-protection, resource exhaustion, and media faults. Do not claim fixed.
- User requested OTA testing for this update; do not substitute USB flashing.

## Subsequent authorized MAIN diagnostic session

- MAIN CH340 /dev/ttyUSB0 detected, MAC 28:84:85:44:9a:04. Partitions unchanged:
  SAFE 0x10000, app0 0x210000/6MiB, app1 0x810000/6MiB. OTA sequence 1 valid,
  app0 selected. Existing image 1,758,736 bytes backed up in a 2MiB read at
  /tmp/milestone-main-before-directory-diag.bin. OTA data and partition copies
  are /tmp/milestone-main-diag-{otadata,partitions}.bin.
- First diagnostic MAIN uploaded only at 0x210000; esptool hash verification
  passed. Automatic OTA reproduced failure creating
  /firmware/download-6422e414d6f70478/main with errno 2 (ENOENT), after the parent
  mkdir reported success. Not a free-space rejection.
- Added mounted-VFS stat verification after Arduino mkdir. If reported success
  does not produce a real directory, try actual VFS mkdir and verify again.
  No data deletion or formatting. Added mocked false-success regression.
- v5 tests pass, revised MAIN compile 1,760,903 bytes / globals 64,884.
  Second MAIN-only upload hash verified. Runtime showed Arduino mkdir returned
  success for a long staging name but stat returned ENOENT. Direct VFS mkdir
  did not repair resolution of this long name.
- Third diagnostic uses FAT 8-character `dlXXXXXX` staging names; cleanup only
  accepts that exact hex pattern or the legacy download prefix. Existing files
  unchanged. MAIN build 1,761,047 bytes (BIN 1,761,200), upload hash verified.
  Real automatic check now passed folder creation, downloaded signed catalog,
  and completed with `OTA signed catalog current: 5.1.8` followed by
  `OTA check complete: current (5.1.8)`. Thus the download-folder blocker was
  avoided on this actual SD using short names; underlying long-name FAT/media
  fault remains unexplained. Host core/v5 suites passed.
- This is still an unreleased 5.1.8-based diagnostic MAIN, not a new release.
  SAFE/NVS/OTA selection/app1/ZERO were not flashed. Actual newer-image OTA
  installation is not tested. In particular V5BundleUpdate uses 16-character
  firmware set directory names; validate that path before claiming complete
  OTA installation support on this card. Do not weaken set-ID validation or
  signing contracts to hide filesystem problems.
- Manual AP checks still require powered, linked ZERO. Automatic MAIN check
  succeeded without changing that existing policy. NOW polling adjustment is
  installed but live BLE progress smoothness was not visually verified.

## ZERO reconnected / 5.1.9 preparation

- Both ports present. ZERO reports 5.1.7; MAIN diagnostic reports 5.1.8. SPI v1
  negotiated, BLE advertising and iPhone encryption/connection observed.
- Diagnostic probe created/read/removed a 16-character set directory within
  the owned short staging directory: `OTA set-directory probe: PASS`.
  Existing set-ID schema/paths need no change. Probe removed from release source.
- Preparing 5.1.9 with existing signer and unified release workflow. Full OTA
  installation still pending; do not equate catalog-current with installation.

## Release completed

- Official `milestone-release --yes local 5.1.9` completed: core/v5 tests,
  MAIN/ZERO/SAFE builds, signatures, atomic main/tag push and all 13 published
  asset checks passed. Release commit/tag c6eabf7 / v5.1.9.
  https://github.com/CXITRON/MILESTONE-Core/releases/tag/v5.1.9
- MAIN build 1,761,047 bytes; ZERO 1,345,283; SAFE 593,928.
- No post-release USB flashing. Device remains on 5.1.8 diagnostic MAIN and
  5.1.7 ZERO until OTA. User asked to trigger AP check/install and physical OK;
  passive MAIN monitor is /tmp/milestone-ota-final-monitor.py (55 s, no reset).
- Actual new-version OTA installation and live NOW cadence remain unverified.
  Never bypass the physical OK confirmation to automate installation.
