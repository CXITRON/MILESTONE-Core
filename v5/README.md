# MILESTONE v5.1.0

This is the release source for the dual-ESP v5 hardware. It remains separate
from the legacy v3.3.4 firmware and uses its own signed MAIN/ZERO/SAFE release
catalog. Initial installation requires the merged USB images; subsequent
updates use the signed companion bundle.

The current public release is
[v5.1.0](https://github.com/CXITRON/MILESTONE-Core/releases/tag/v5.1.0).
Its 13 signed assets passed the release re-download contract, and the MAIN and
ZERO application images were written and hash-verified on the assembled boards.
The assembled boards previously booted v5.0.1 and negotiated companion
protocol v1 after upload. The v5.1.0 staged sync-media path is software-tested;
browser/device playback remains part of the release hardware check.

## Contents

- `libraries/MilestoneV5Core`: host-testable protocol, transport, settings,
  diagnostics, Wi-Fi, OTA, bundle, recovery, storage and safety contracts.
- `MilestoneV5Main`: CORE/MEDIA/NOW, TFT/5-button UI, SD/RTC/environment,
  portal, local media, artwork, radio broker and signed companion updates.
- `MilestoneV5Zero`: SPI slave, BLE/AMS, multi-network Wi-Fi/PEAP, NTP,
  artwork and HTTPS/binary OTA transfer.
- `MilestoneV5Safe`: independent factory-partition recovery UI with no network
  runtime and signed SD Stable/Backup/Recovery restoration.

The runtime implements the confirmed plan, including v3 configuration import,
eight A/B-protected Wi-Fi credentials, persisted system/display options,
PSRAM-backed dirty-tile rendering, A/B artwork and firmware indices, bounded
network leases, delayed boot-candidate acceptance and power-loss-aware bundle
journaling. See [the implementation status](IMPLEMENTATION_STATUS.md) for the
software/hardware validation boundary.

The 512-byte SPI slot holds a guarded frame length plus a frame with a fixed
32-byte little-endian header. Payloads carried in one SPI transaction are
bounded to 476 bytes. Larger artwork and OTA objects must be split into ordered
chunks at the application layer. The general frame codec remains bounded to
4096 bytes for non-SPI transports and host tooling.

## Host tests

```bash
./tools/test-v5.sh
```

## Arduino compile checks

```bash
arduino-cli compile \
  --libraries v5/libraries \
  --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  v5/MilestoneV5Main

arduino-cli compile \
  --libraries v5/libraries \
  --fqbn 'esp32:esp32:waveshare_esp32_s3_zero:CDCOnBoot=default,PSRAM=enabled,PartitionScheme=min_spiffs' \
  v5/MilestoneV5Zero

arduino-cli compile \
  --libraries v5/libraries \
  --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  v5/MilestoneV5Safe
```

These individual commands are compile checks only. The v5 runtime uses separate
`milestone_v5`, `v5_core`, `v5_wifi`, diagnostics, system and bundle namespaces.
On first boot it can copy supported v3 schema-12 CORE/system/Wi-Fi settings, but
it never mutates the released v3 namespaces. Do not use ordinary Arduino MAIN
upload: the factory partition belongs to SAFE and the verified initial image
places MAIN at OTA slot A.

## Local media

Put standard uncompressed 24-bit BMP images (up to 128×128) in `/media/photo/`.
Convert a local video on the PC, then copy the resulting MVJ1 file to
`/media/video/` on the SD card:

```bash
python3 tools/convert-v5-media.py input.mp4 output.mvj --fps 15 --seconds 60
```

MVJ1 is an experimental 128×128 JPEG-frame container, not native MP4 playback.
It contains no audio. The runtime validates each frame's length, CRC and JPEG
dimensions and prefetches encoded frames into PSRAM. FPS is a requested rate,
not a hardware-verified guarantee. No live video-frame streaming endpoint is
included.

The restored portal also accepts the legacy browser-generated MSM1 photo/GIF/
video container. Those items are stored on microSD as `/media/XXXXXXXX.msm`
with CRC-checked A/B catalogs and take precedence when enabled. MVJ1 remains the
direct-copy video format under `/media/video/`; original MP4 files are not
stored or decoded on the device.

### Staged browser-audio sync media

While the MEDIA profile is active, open the setup AP and select `오디오 동기화
MEDIA`. The browser converts the complete original video to MVJ1, uploads it to
`/media/sync/`, and waits while MAIN validates every frame and builds an MVX1
seek index. The original audio then plays in the browser while MAIN reads video
frames from microSD according to the browser audio position. Playback traffic
contains only bounded control/status packets over a one-client WebSocket; video
frames are never carried during playback.

The converted browser blob is limited to 256 MiB to avoid exhausting phone
memory, and MAIN also reserves 64 MiB of free SD capacity. Lower FPS or JPEG
quality when that browser limit is reached. Keep the browser in the foreground:
screen lock, background suspension, AP loss, or 2.5 seconds without control
updates pauses device video. BACK, MODE, the portal close button, thermal stop,
or reboot removes the temporary video/index. This path is intentionally
ephemeral and does not alter persistent `/media/video/` files.

## Local setup

Open the front-panel MODE menu and select `Wi-Fi 설정 AP` (MAIN BOOT remains a
shortcut) to open the password-protected setup AP; its new password is shown
on the TFT. Open `http://192.168.4.1/`. MODE, BACK, or BOOT closes the portal. The AP
also expires after ten idle minutes. CORE colors/date/message, environment
calibration/units/timing, logging, media options and artwork management are
available. Up to eight Personal/Open or WPA2-Enterprise PEAP credentials are
kept in CRC-protected A/B records. New credentials are committed only after a
bounded connection test remains usable for two seconds, then replicated to the
other board. SSIDs are visible in the portal; passwords and enterprise identity
fields are never returned to the browser.

The development artwork downloader uses the existing unencrypted HTTP worker
endpoint. Track title, artist and album are transmitted to that service only
for cache misses. JPG/PNG uploads are resized in the local browser and sent to
MAIN as validated MAC1 RGB565 data; they do not go through the artwork server.

## Signed MAIN/ZERO SD restore

This direct restore path is separate from the SD companion bundle below. The
independent factory SAFE app can select signed Stable/Backup/Recovery images;
MAIN also exposes the guarded recovery entry path. Each directory contains
`firmware.bin`, `manifest.txt`, and binary `manifest.sig`. The signed manifest
format is:

```text
MILESTONE-V5 MAIN X.Y.Z BYTE_COUNT LOWERCASE_SHA256_HEX MIN_PEER_PROTOCOL MAX_PEER_PROTOCOL
```

The file ends in a newline. Both peer protocol values are currently `1`.
ZERO SD recovery uses the same format with target `ZERO` and its corresponding
SD directory. The signature is a SHA-256 signature over the exact
manifest bytes, verified using the PEM public key compiled into
`MILESTONE_V5_RELEASE_PUBLIC_KEY`. The default key is empty and fails closed.
MAIN hashes the entire SD image before erasing an inactive OTA slot, hashes it
again during installation, and lets IDF validate the image before selecting it
for boot. The old active slot is not overwritten. ZERO's local SD restore is
sent through SPI only after MAIN's validation; ZERO independently checks the
manifest/signature, hashes incoming image bytes and writes its inactive slot.
The sender waits for ZERO's reboot and boot-candidate acceptance. A failed ZERO
restore never selects a different MAIN partition. No release private key is
stored on SD or embedded in firmware.

`tools/prepare-v5-sd-restore.py` prepares these three files using an existing
private/public key pair and verifies the resulting signature. It neither builds
nor publishes a release, and refuses to overwrite an existing output directory:

```bash
python3 tools/prepare-v5-sd-restore.py firmware.bin new-restore-directory \
  --target ZERO --version 5.1.0 \
  --private-key /path/to/private.pem --public-key /path/to/public.pem
```

Use only the matching trusted release public key in firmware. This offline
helper does not validate that an arbitrary input BIN implements the claimed
board role; `tools/make-release.sh` performs that check for release assets. Do
not sign an unverified binary. Test keys generated by host tests are temporary
and are not production release keys.

## Signed SD companion bundle

The runtime accepts a pre-staged `/firmware/incoming/` directory containing
`bundle.txt`, `bundle.sig`, `main/` and, when explicitly included, `zero/`.
The board subdirectories use the signed restore format above. The exact bundle
descriptor, including its final newline, is signed with the same trusted key:

```text
MILESTONE-V5 BUNDLE X.Y.Z MAIN_SHA256 ZERO_SHA256_OR_NONE
```

Prepare an offline bundle from verified board binaries with the same helper:

```bash
python3 tools/prepare-v5-sd-restore.py main.bin new-bundle-directory \
  --bundle --zero-source zero.bin --version 5.1.0 \
  --private-key /path/to/private.pem --public-key /path/to/public.pem
```

Omit `--zero-source` only for an intentional MAIN-only update; `NONE` is signed,
so removing ZERO from an already signed paired bundle does not authorize a
MAIN-only install. Copy the resulting directory to SD as `/firmware/incoming/`.
Request installation in the authenticated portal and confirm with the physical
OK button within 15 seconds.

The portal can also fetch `latest` or a canonical `X.Y.Z` release. A bounded
HTTPS worker on the board selected by the radio broker validates the CA chain
and only accepts GitHub release/CDN redirects. MAIN writes the fixed asset set
to a unique SD directory, validates the signed bundle and both manifests before
accepting binary sizes, and hashes every binary before installation. The worker
never accesses SD, SPI or Flash directly.

Before installing MAIN, the coordinator validates signatures and copies the
exact files into a new immutable `/firmware/sets/<id>/` directory, checking both
source and SD readback hashes. A CRC-protected NVS journal spans reboots. Only
after the new MAIN's running image hash and boot acceptance succeed can ZERO
installation start. ZERO's acceptance receipt binds the transfer to its running
partition and ELF image identity. Failure retains MAIN without promoting Stable.

After both acceptances, the development policy requires a ten-minute hold with
local safety and a healthy companion link for a paired update. Safety/link
failures restart the hold. An A/B SD index then advances Stable and preserves the
previous Stable as Backup. MAIN-only updates leave ZERO's index unchanged.
Recovery directories are never overwritten, and no firmware sets are deleted.
SAFE MODE resolves Stable through this index, falling back to the legacy SD
paths when no index exists. Host failure-injection tests are not hardware
power-loss or long-duration product qualification.

## Release build

The private key remains offline; firmware receives only its public key. A local
release build uses the existing release backend:

```bash
MILESTONE_V5_PRIVATE_KEY=/secure/private.pem \
MILESTONE_V5_PUBLIC_KEY=/secure/public.pem \
./tools/make-release.sh 5.1.0 "MILESTONE v5.1.0"
```

This creates and verifies 13 assets: board applications, signed manifests and
bundle, the independent SAFE app, MAIN/ZERO initial USB images and a signed
catalog. Actual publication remains exclusively under `milestone-release`; the
private and public key paths must be supplied through
`MILESTONE_V5_PRIVATE_KEY` and `MILESTONE_V5_PUBLIC_KEY`.
