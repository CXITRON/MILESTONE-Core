# MILESTONE Core — Project Context

> Current baseline: MILESTONE Core v3.1.1
> Hardware: Waveshare ESP32-S3-Zero + ST7735-compatible 128×160 SPI TFT + three tactile switches
> Repository: `CXITRON/MILESTONE-Core`

This document gives coding agents and maintainers the architectural context needed before modifying the firmware. `AGENTS.md` contains operational rules, especially the release workflow. `README.md` contains user-facing behavior and detailed version history. When documentation conflicts with implementation, inspect the current source and treat the source as authoritative.

## 1. Product scope

MILESTONE Core is an ESP32-S3 desktop display firmware with:

- three OTA-switchable application profiles: CORE (general displays), MEDIA (stored/live media), and NOW (iPhone AMS Now Playing)

CORE owns D-day, message, clock, dashboard, device-information, and general screen-cycle behavior. MEDIA never enters those views: confirm and timed transitions select the next enabled media item. NOW renders only Bluetooth connection/AMS metadata state and does not enter general or media views. All three profiles share schema 11; MEDIA and NOW preserve the stored CORE general-view fields so switching back restores the previous CORE setup exactly.

The profile boundary is compile-time, not a runtime feature flag. CORE uses `CoreMediaDisabled.inc` only for schema-compatible no-op calls and raw shared-partition erase during an explicit factory reset; it must not link `CoreMedia.inc`, `CoreMedia.cpp`, LittleFS, media/stream HTTP routes, `StreamPage.h`, or media converter UI bytes. Release BIN inspection enforces this boundary.

- D-day, date, time, and message views
- selectable/automatic screen cycling
- previous/next/confirm tactile-button interaction plus the onboard BOOT fallback
- local captive configuration portal with optional fixed/open setup-AP security
- multiple remembered Wi-Fi networks
- WPA2-Enterprise PEAP support
- NTP time synchronization and offline fallback
- RGB status LED
- internal temperature display and thermal protection
- GitHub Release based HTTPS OTA updates
- post-OTA candidate validation and application-level rollback
- device diagnostics for memory, network, update, rollback, and thermal state
- persistent recent Diagnostics & Health event history with portal copy/clear tools
- browser-converted 128×128 RGB332 color or monochrome photos, GIFs, and short local videos
- isolated `/stream` live playback with browser-side full preconversion, RAW/XOR-RLE binary frame-record transport, PSRAM buffering, and stream-specific runtime isolation
- dedicated NOW profile for iPhone/iPad BLE Now Playing metadata via Apple Media Service (AMS)

It is an embedded application composed of several cooperative state machines. Reliability and recoverability are more important than cosmetic architectural purity.

## 2. Current source layout

```text
MILESTONE_Core/
├── AGENTS.md
├── MILESTONE_PROJECT_CONTEXT.md
├── MILESTONE_Core.ino
├── FirmwareProfile.h
├── CoreConfig.inc
├── CoreBluetooth.inc
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
├── CoreTftDisplay.h
├── CoreTftDisplay.cpp
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
│   ├── test_radio_bluetooth_contract.sh
│   ├── test_profile_ota_contract.sh
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
FIRMWARE_VERSION = "3.1.1"
CONFIG_VERSION = 11
```

Increment `CONFIG_VERSION` only when persistent NVS layout/meaning changes and implement a migration path. A firmware version change by itself must not force a schema reset.

Schema 9 appends `CUSTOM_MEDIA` as View 7 and TopMode 8 without renumbering the existing values. Schema 10 adds `ap_fixed`, `ap_pass`, and `ble_media`. Schema 11 adds six CORE semantic RGB colors and the MEDIA monochrome preference. The v10→v11 migration supplies the v3.1 defaults without changing Wi-Fi, AP security, CORE view order, stored media, or NOW layout.

## 4. Runtime architecture

### Configuration

`CoreConfig.inc` owns persistent settings and migration logic. Wi-Fi credentials use an A/B bank strategy so an interrupted write does not destroy the last known-good list. `loadConfig()` is effectively migration code and should be treated as high-risk.

### Display

`CoreDisplay.inc` renders views, colored status indicators, thermal status, and device information into the 128×128 U8g2 surface. `CoreTftDisplay.h/.cpp` tracks a PSRAM RGB565 layer alongside that surface and sends it to the centered 128×128 TFT region. Semantic CORE colors, RGB332 MEDIA frames, and RGB565 NOW artwork share this backend while preserving the existing fonts and layout.

### Network and time

`CoreNetwork.inc` manages Wi-Fi connection, Enterprise PEAP, setup AP behavior, DHCP stabilization, NTP synchronization, retries, and Wi-Fi sleep behavior. Cold-boot sequencing was hardened because setup-mode connectivity and autonomous reboot connectivity previously behaved differently. In schema 10, fixed AP security is opt-in: the default path still generates a fresh 8-character setup-AP password, while fixed mode uses a persisted 8–63 character password or an explicitly blank password for an open AP.


### Bluetooth Now Playing, NOW profile, and setup-AP security (v1.11.0, v2.1.0)

`CoreBluetooth.inc` implements iPhone/iPad Now Playing metadata through Apple Media Service (AMS) when the Arduino-ESP32 build exposes NimBLE. Since v2.1.0, the BLE runtime is compiled only into the dedicated NOW image and is always enabled there; CORE and MEDIA contain no BLE runtime. MILESTONE advertises an AMS service solicitation plus a shortened primary-packet name, bonds with the iOS device, discovers the iPhone-hosted AMS service on the same connection, subscribes to Player/Track Entity Update attributes, and renders a large title, artist, playback state, and progress. Korean metadata uses the Korean U8g2 font and non-Korean metadata uses the Japanese font so kana/kanji titles render instead of falling back to empty glyphs. NOW does not add a ninth persistent `View`, so the existing 8-bit cycle mask and saved CORE screen-order contract remain unchanged. A NOW release must fail rather than silently compile the unsupported BLE stub when NimBLE is absent, and binary inspection must find `MILESTONE_BLE_AMS_RUNTIME_V8` only in the NOW image.

The current implementation parses complete AMS Entity Update notifications and safely displays the received prefix when iOS marks a value as truncated. A later revision may add asynchronous Entity Attribute reads for the full value; do not block the main loop waiting for a GATT read. Pairing and AMS interoperability must be validated on the physical ESP32-S3/iPhone combination before treating the feature as hardware-certified.

BLE and live streaming intentionally do not compete for the ESP32-S3 radio/runtime hot path. `enterMediaStreamPerformanceMode()` suspends BLE advertising/connections before STREAM_MODE work starts, and `leaveMediaStreamPerformanceMode()` resumes BLE only after normal runtime is restored. Preserve that isolation.

Setup-AP security is stored in schema 10 but preserves old behavior by default. `fixedApSecurity=false` uses the existing fresh random 8-character password. When explicitly enabled, `fixedApPassword` accepts either 8–63 characters or an empty string; empty means an open AP and the portal must display an explicit security warning/confirmation. The GET API exposes only whether a password exists, never the saved secret. AP security and Bluetooth have independent save actions so changing Bluetooth cannot accidentally overwrite a hidden AP password. Turning fixed mode off clears the dormant fixed password and returns the next AP start to the legacy random-password path.


### Network/AP stabilization (v1.11.1)

v1.11.1 treats the setup AP as a user-facing radio session that must take priority over autonomous STA discovery. Entering the portal cancels any in-flight saved-network scan or incomplete STA connection before SoftAP startup. Failed portal Wi-Fi tests leave STA idle instead of immediately launching a second saved-network connection underneath the AP; the normal saved-network sequence resumes only when setup closes or the user explicitly requests another network operation.

Portal scans remain asynchronous but use an 120ms active-scan dwell per channel, while non-portal saved-network scans use 120ms. `esp_wifi_scan_stop()` is used during cancellation/timeouts rather than relying only on deleting scan results. The preferred saved network gets one bounded direct attempt; after that, only saved SSIDs actually observed by the scan are tried. This avoids minutes of repeated direct attempts when no Wi-Fi exists nearby. Confirmed no-network states use the normal configured retry period instead of the 15-second quick retry, and park Wi-Fi in `WIFI_OFF` until that retry becomes due.

The portal UI tolerates transient HTTP loss while the single ESP32-S3 radio is off-channel for scanning, blocks scans while a connection test is active, and explicitly reports that the setup AP remains available when zero networks are found. Authenticated portal traffic refreshes the AP idle timeout so active configuration work is not terminated by the fixed 10-minute timer.

### Setup-AP session retention (v2.0.2)

A successful portal Wi-Fi/NTP test keeps the setup AP and browser session active instead of arming the former three-second shutdown. The connected STA may provide internet concurrently while the user completes the remaining settings; only the normal 10-minute idle timeout or a safety/runtime stop closes the portal. While the portal is active, the runtime also checks the AP radio mode and SoftAP IP once per second. If another radio transition unexpectedly removes the AP, recovery reasserts AP+STA mode, restarts the configured SoftAP, and rebinds captive DNS with a bounded retry cadence.

Manual time synchronization has an explicit portal-visible pending/result state and is polled until success or failure. Each bounded request restarts lwIP SNTP for a fresh callback, then stops it after completion so the configured manual/periodic cadence remains authoritative and stale callbacks cannot satisfy a later request. v2.0.7 configures one independent NTP provider at a time and advances after 7 seconds instead of inheriting lwIP's fixed 15-second receive delay for every unresponsive server; three providers therefore complete or fail within 21 seconds. Portal update/profile actions stop after that bounded failure instead of silently remaining queued through another quick-retry cycle. STREAM_MODE cannot begin during NTP, and automatic boot/weekly manifest checks are deferred while the setup portal is active so a completed time sync does not immediately block the captive portal with synchronous HTTPS work.

### Bluetooth advertising recovery and diagnostics (v1.11.2)

BLE stack initialization is not treated as proof that the device is advertising. Advertising payload and scan-response registration, advertising start, connection attempts, pairing/encryption, AMS discovery, CCCD discovery, and subscription writes have distinct runtime stages and error codes. A failed or unexpectedly stopped advertisement is retried on a bounded five-second cadence outside STREAM_MODE. Failed connection and security attempts are disconnected cleanly so advertising can resume instead of leaving the device in a permanently connected-but-unsecured state. `/api/status` exposes the active stage, actual advertising state, last BLE error, and numeric stack status for hardware diagnosis.

### OTA update

`CoreUpdate.inc` checks the GitHub Release manifest and performs the HTTPS OTA transaction. Important invariants include TLS verification, manifest validation, content length, SHA-256 verification, temperature/resource guards, connection-stall handling, `Update.end()` success, and reboot ordering.

v2.0.3 isolates optional CORE Bluetooth before manifest TLS and firmware download work. The NimBLE stack is deinitialized before the OTA memory guard so its internal RAM is available for TLS, advertising/connections cannot contend with Wi-Fi, and normal cooperative Bluetooth initialization resumes only after a completed check or failed/deferred install. A verified install reboots without rebuilding BLE. MEDIA uses the same calls through no-op profile stubs.

v2.0.4 fixes the portal-only manifest gate: the setup screen intentionally renders its AP indicator instead of the normal U icon, so manual update checks and CORE↔MEDIA profile checks must not wait for `updateCheckIndicatorRendered`. Bluetooth isolation now starts only immediately before the actual HTTPS attempt. Portal-originated manifest requests also use shorter connect/read/TLS limits so the synchronous request cannot leave captive-portal HTTP and DNS service unresponsive for the full autonomous-update timeout.

v2.0.5 fixes setup-AP idle-time evaluation after a portal request. `processNetwork()` captures its ordinary loop timestamp before calling `server.handleClient()`, but an authenticated handler refreshes `portalStartedMs` inside that call. The timeout check must re-sample `millis()` after request handling; subtracting the refreshed activity time from the older loop timestamp underflows and can falsely close the AP immediately. That false stop also changes AP+STA to STA immediately before the queued GitHub HTTPS request, explaining the coupled manifest/profile-check failures. Portal timeout ordering and bounded NTP provider rotation are covered by source contract tests.

v2.0.6 follows the v2.0.5 hardware verification result: the first four `github.com/releases/latest/download` TCP/TLS connections could be refused while an unchanged fifth attempt succeeded, and extending the NimBLE teardown pause did not change that pattern. The release check now uses one direct `api.github.com` response and validates its `tag_name` plus the selected CORE/MEDIA BIN asset's API URL, `size`, and GitHub-provided `sha256:` digest. Installation uses that verified asset API URL to redirect once to the CDN, retries only pre-Flash transport failures, and retains the existing streamed size/SHA-256/Update transaction. Legacy profile manifests continue to be published for older firmware.

v2.0.7 makes setup-portal firmware actions own their network prerequisites. A manual update or CORE↔MEDIA check queues the exact requested profile before reconnecting the preferred saved Wi-Fi, then continues through DHCP stabilization, bounded NTP, and Release API verification without a second browser action. Profile switching remains a physical-confirmation transaction: the OLED shows source and target identities and only a short BOOT press authorizes installation; the web install endpoint rejects cross-profile confirmation. The boot splash and ready-to-reboot screen show both semantic version and CORE/MEDIA identity. Same-profile web installation retains a target-qualified two-step confirmation, and an already confirmed install can reconnect/NTP automatically without asking the user to confirm again.

v2.0.8 hardens stored MEDIA playback. The catalog display interval is a minimum hold time rather than permission to reopen an animation mid-frame-cycle: one enabled item remains in its own MSM1 loop, while multiple items switch only after the active animation reaches a finished or loop boundary. Mid-playback LittleFS/frame-decode failures are recorded with the item ID and retried after a bounded five-second backoff instead of leaving an unexplained frozen frame or tight reopen loop. The main portal status also exposes the current hardware reset reason and numeric code so a real brownout, watchdog, panic, external reset, or ordinary power-on can be distinguished from a catalog restart.

v2.1.0 moves AMS Bluetooth out of CORE into the third NOW application profile. CORE contains only general views, MEDIA contains only stored/live media, and NOW contains only the always-on AMS connection and metadata screen. The common portal can check/install all three fixed assets, while a profile switch still requires a short physical BOOT confirmation. Release inspection rejects Bluetooth markers outside NOW and media/stream markers outside MEDIA. The configuration schema remains 10 and profile switches preserve Wi-Fi, CORE views, diagnostics, and stored media.

v2.1.1 hardens NOW firmware operations after a v2.0.7 CORE PANIC was observed during update checking. A connected iPhone is terminated first and the matching GAP disconnect event must arrive before `BLEDevice::deinit(false)` can delete the server and stop NimBLE tasks. The wait is bounded; timeout or termination failure refuses unsafe deinitialization and defers the firmware operation instead of risking a PANIC. The NOW screen removes unused album metadata and renders the title at 2× the artist size.

v2.2.0 adds five persistent NOW layouts and restores AMS album metadata only for layouts that need it. Artwork layouts query MusicBrainz directly, prefer album + artist matching, fetch a Cover Art Archive release-group thumbnail, decode JPEG inside the NOW image, and create 60×60/88×88 one-bit bitmaps. A 1.4-second latest-track debounce and six-entry PSRAM cache absorb rapid skipping. Artwork HTTPS runs in a bounded background task, is serialized against firmware HTTPS, and remains optional: lookup, allocation, download, or decode failure leaves AMS text and playback progress operational.

v2.2.1 hardens the NOW pairing/encryption transition. The runtime no longer depends on receiving one GAP encryption-change event: it checks the live NimBLE connection descriptor immediately after security starts and every 250ms, so an already encrypted bonded reconnect or a missed completion event can still advance to AMS discovery. A session that remains unencrypted for 15 seconds is terminated and returned to advertising for a clean retry instead of remaining on `SECURING` indefinitely. The portal status API exposes the current security wait duration for diagnosis.

v2.2.2 hardens NOW artwork lookup after intermittent resets were reported near failed cover requests. MusicBrainz XML is scanned from a bounded 48KiB stream instead of being copied into an internal-heap `String`, the worker has an explicit 14KiB stack with high-water logging, and lookup/network/download/decode failures have distinct states. An RTC breadcrumb preserves whether an abnormal reset interrupted lookup, download, or decode and exposes that stage through the NOW portal API after reboot. NOW's internal-die thermal policy is raised to 75/85/95°C for warning/throttle/protection while CORE and MEDIA retain 70/80/90°C; thermal protection still does not intentionally reboot the device.

v2.2.3 limits `settling` to the 1.4-second metadata debounce. A setup AP without a stable STA reports `portal-paused`; a stable STA may run the background artwork lookup while the AP remains open, and an explicit portal-close API/button returns immediately to normal NOW operation after its HTTP response is delivered. The visible album-name layout is removed. Its persisted numeric value 2 remains reserved for rollback compatibility but normalizes to title + artist, and BOOT plus the portal expose only four layouts.

v2.2.4 removes `BLEDevice::deinit(false)` from NOW firmware checks and installs. Arduino-ESP32 3.3.x deletes advertising/server objects before stopping its NimBLE host task, leaving a callback race even after a matching disconnect event. Firmware operations now stop advertising, confirm disconnection, retain the initialized BLE objects, and resume advertising afterward; the existing internal-RAM guard refuses TLS safely if the retained stack leaves insufficient memory. RTC breadcrumbs identify resets during BLE quiesce, manifest TLS, firmware TLS, flash write, or verification through the portal status API.

v2.2.5 moves setup-portal Release checks to a bounded background worker so captive DNS and HTTP remain responsive during GitHub TLS, while loopTask applies the completed candidate atomically. Manifest collection now decodes chunked responses into a truly bounded 16KiB sink. NOW firmware isolation resolves live NimBLE connections from the host/server state, not only the cooperative app flag, and clears stale GAP/AMS events after disconnect. Artwork parsing accepts MusicBrainz XML attributes in any order, escapes Lucene metacharacters, cancels stale generations between network stages, pauses under thermal pressure, and avoids portal radio mutations during an active lookup. Portal browser requests have explicit timeouts, and MEDIA live preconversion has a 48MiB client-memory ceiling.

v2.2.6 prevents the update-check LED state from repeatedly interrupting an active NOW session. Automatic boot/weekly checks inspect the live NimBLE peer state before entering `CHECKING` and defer for five minutes while an iPhone remains connected. A connection racing that pre-check also exits `CHECKING` immediately if bounded BLE isolation is refused. Manual checks make one bounded disconnect attempt and terminate with a clear error instead of retrying every 500ms forever. The existing two-blue-flash LED remains the intentional indication only while an HTTPS release check is actually running.

v2.2.7 hardens direct MusicBrainz artwork lookup after live measurements showed valid 200 responses varying from under one second to more than thirteen seconds. The query requests only the first result actually consumed, extends the bounded lookup timeout to fifteen seconds, and retries one transient transport/429/5xx failure after the required 1.2-second spacing. Slow reads are labeled `lookup-timeout` rather than generic `network-failed`; the NOW portal reports the last HTTP/transport code and attempt count. Placeholder status text switches to a smaller font when necessary and is centered from the image frame's real origin on both axes.

v2.2.8 improves NOW artwork matching without introducing a relay service. An album + artist zero-result response falls back to title + artist, while transport and server failures retain their original diagnosis. Each query and Cover Art Archive pass is bounded to three distinct release-group candidates, allowing an alternate release to supply a cover when the top MusicBrainz result has none. The portal separates MusicBrainz response code/attempts from Cover Art Archive response code/candidate count, and all-candidate HTTP 404 is labeled `art-not-found` instead of a generic download failure.

v2.2.9 handles localized Apple Music metadata by querying Apple's public iTunes Search endpoint first with the exact AMS title, artist, and album, then streaming the first `artworkUrl100` from a bounded response directly into the existing JPEG path. This resolves names such as `요네즈 켄시`, which neither the MusicBrainz recording artist phrase nor its token query matches even though the artist record contains a differently ordered Korean alias. Apple lookup and image hosts chain to the already embedded DigiCert Global Root G2. MusicBrainz/CAA remains the no-result or transport fallback, and every MusicBrainz request, including album-to-recording fallback, is paced at least 1.2 seconds from the previous request start.

v2.2.10 fixes the internal-RAM lifetime overlap introduced by the Apple artwork path. The Apple search `NetworkClientSecure`, `HTTPClient`, query strings, and TLS buffers are destroyed before the separate image HTTPS connection is opened; the Apple URL buffer is then also destroyed before MusicBrainz fallback allocations. MusicBrainz query strings remain lazy and are built only when Apple lookup or image decode did not succeed. NOW portal diagnostics report the Apple Search HTTP/transport code separately from the MusicBrainz and image codes.

v2.2.11 fixes a NOW-only automatic update-check deadlock. The update state machine deliberately waits until the OLED renderer confirms that the visible `U` frame was sent before starting synchronous GitHub HTTPS, but both NOW playback and NOW waiting renderers bypassed the shared status-icon function. They now draw the same `T`/`U` status icon as general views; drawing `U` sets `updateCheckIndicatorRendered`, allowing the check to proceed and leave the repeated two-blue-flash state.

v2.2.12 prioritizes the live AMS link while album-art Wi-Fi HTTPS is active. The requested BLE connection range changes from 100–200ms with latency 3 to 40–80ms with zero latency, and supervision timeout increases from six to twelve seconds, reducing radio-starvation disconnects during TLS handshakes. A live BLE peer also requires at least 80KiB free internal heap and a 32KiB largest block before artwork starts; otherwise optional artwork reports `low-memory` without risking the music session. GAP disconnect reason is retained in logs and the portal status API for hardware verification.

v2.2.13 fixes a NOW reconnect race confirmed on physical hardware. NimBLE can report the live bonded link as encrypted and then deliver a delayed `BLE_HS_ETIMEOUT` for the redundant security procedure. The runtime now ignores only that timeout after encryption has already been confirmed, preserving AMS discovery; a timeout before confirmation and every other nonzero encryption status retain the existing disconnect-and-retry behavior.

v2.2.14 hardens optional NOW artwork HTTPS after physical logs showed the local network refusing the first three fresh TLS transports while the fourth GitHub attempt succeeded. Apple Search and MusicBrainz now recreate their TLS/HTTP objects and retry transient negative transport results, HTTP 429/5xx, and incomplete HTTP 200 bodies up to five times with increasing bounded backoff. Definitive 4xx/no-match responses still stop immediately, Wi-Fi stays awake while the worker is active, and AMS playback remains independent of artwork success.

v2.2.15 bounds that recovery against normal two-to-four-minute track lengths. One artwork worker now has a shared 35-second wall-time budget across Apple Search, MusicBrainz, cover download, and decode, rather than allowing every provider to consume its independent maximum. Transport backoff starts at 500ms, fresh lookup TLS connect/handshake limits are five seconds, and completion logs include `elapsed_ms`; budget exhaustion is reported as `lookup-timeout` while track changes and thermal cancellation retain their existing meanings.

v2.2.16 applies the same bounded transient recovery to the actual Apple/CAA JPEG transfer. Hardware logs showed Apple Search reaching HTTP 200, followed by a first image connection failure and an unnecessary MusicBrainz fallback that consumed the 35-second budget. Negative transport results and incomplete HTTP 200 image bodies now recreate the TLS/HTTP objects and retry within the existing shared budget; a definitive 404 still advances immediately and decode failures are not retried as network failures.

v2.2.17 prioritizes cover usefulness within ordinary two-to-four-minute tracks. A new track starts Wi-Fi association during the shortened 800ms metadata debounce, and the shared lookup/download budget is reduced from 35 seconds to 15 seconds with tighter connect/read limits and retry backoff. Diagnostics now distinguish worker time from total track-change-to-result latency. NOW also defers initial BLE advertising until the bounded first Wi-Fi/NTP attempt settles, preventing an immediate bonded iPhone security/connection-parameter transition from overlapping the first STA WPA handshake.

v2.2.18 corrects the v2.2.17 hardware result where repeated Apple TLS transport failures consumed the entire 15-second worker budget before MusicBrainz could run. Apple remains the localized fast path but gets one bounded transport attempt; any failure immediately falls through to MusicBrainz/CAA. Artwork TLS handshakes are capped at three seconds, while the shared 15-second budget and download retry behavior remain intact.

v2.2.19 addresses the next hardware result: Apple and MusicBrainz both returned transport `-1` on the ESP32 while the same hosts returned HTTP 200 from the same LAN, and the embedded DigiCert Global Root G2 still matched both live certificate chains. The artwork worker now temporarily selects ESP-IDF's Wi-Fi-preferred coexistence policy for its bounded TLS work and always restores balanced coexistence before exit. The bonded AMS connection remains alive; the firmware does not disconnect Bluetooth merely to fetch a cover.

v2.2.20 removes the NOW OTA low-RAM retry trap. If a confirmed install still lacks the guarded internal heap or contiguous block after bounded BLE disconnection, the exact validated target metadata is committed to NVS and the device reboots into a bounded RAM-recovery install. That boot does not initialize BLE, reconnects Wi-Fi/NTP, and resumes the already confirmed install automatically. The marker is cleared after verified success, on any install failure, or after a 90-second network preparation timeout; failure therefore restores ordinary NOW/BLE operation instead of returning to an endlessly actionable AVAILABLE state.

v2.2.21 moves the optional NOW cover hot path to the free `milestone-artwork` Cloudflare Worker. The device sends bounded title, artist, and album form fields over one HTTPS connection and receives either one cached baseline JPEG thumbnail or a quick no-art response; gateway failure never fans out into Apple/MusicBrainz/CAA requests on the ESP32. The Worker runs near `aws:us-east-1`, where its bounded Deezer title+album/title+artist catalog queries return usable results, hashes normalized metadata for cache keys, keeps positive and negative responses in the edge cache, and uses no paid storage or image product. Live tests returned `orion / 요네즈 켄시 / BOOTLEG` as a 3.3KiB 96×96 baseline JPEG in about one second and a definite miss in under one second. AMS text/progress, six-entry PSRAM caching, thermal cancellation, OTA serialization, and schema 10 remain unchanged.

v2.2.22 moves JPEG decode, resizing, luminance conversion, and ordered dithering from the NOW device to the same free Worker. The backward-compatible `/v1/artwork` path continues to serve JPEGs to v2.2.21, while `/v2/artwork` uses a Wasm MozJPEG decoder and returns a fixed 1,464-byte `MAB1` packet containing 60×60 and 88×88 LSB-first one-bit bitmaps plus a CRC-32. NOW validates the exact length, magic, dimensions, component lengths, and payload CRC before copying either bitmap. Live cache-miss traces measured 6–9ms CPU after CRC/pixel-loop optimization, within the Workers Free 10ms request budget; no Images, R2, KV, or paid database is used. AMS text/progress, the six-entry PSRAM cache, thermal cancellation, OTA serialization, and schema 10 remain unchanged.

v2.2.23 fixes a physical-device radio regression exposed after the smaller bitmap gateway response shipped. Diagnostics showed repeated STA associations every 6–10 seconds during one live NOW session even with -60 to -63 dBm RSSI and a completed NTP sync. Artwork layouts now keep an already associated STA up while an iPhone remains connected instead of applying Wi-Fi-off after each result, the 1,464-byte TLS transfer stays on balanced Wi-Fi/Bluetooth coexistence instead of forcing Wi-Fi priority, and an active NTP exchange serializes ahead of artwork TLS. Metadata settling increases to 1.5 seconds and a failed gateway transport no longer starts an immediate second TLS attempt. Wi-Fi sleep resumes after the Bluetooth session ends; schema 10 and the Worker API remain unchanged.

v2.2.24 addresses the remaining physical NOW failure where a gateway transport error was still followed by AMS re-securing or advertising. Balanced coexistence alone did not guarantee enough BLE airtime while the fully awake Wi-Fi modem performed TLS. The bounded artwork worker now temporarily selects Bluetooth-priority coexistence and enables Wi-Fi minimum-modem power save only for the transfer, restoring balanced coexistence and fully awake STA immediately afterward. The requested iPhone connection interval narrows from 40–80ms to 30–40ms with zero latency and the existing 12-second supervision timeout. NTP, OTA, Wi-Fi association ordering, schema 10, and the Worker API remain unchanged.

v2.2.25 fixes the actual `SETTLING` to `SECURING` transition seen on hardware: the disconnect occurred before artwork TLS, when Wi-Fi-off mode had shut down STA after boot NTP and the first AMS metadata then started a fresh association. Artwork layouts now keep the boot-established station associated even before an iPhone connects, and a live AMS session never starts a new STA association. Text layouts retain the configured Wi-Fi-off behavior. The v2.2.24 runtime modem-power/coexistence transitions and shortened connection interval are removed after one candidate boot produced an interrupt watchdog reset; artwork TLS remains on the normal balanced policy. Schema 10 and the Worker API remain unchanged.

v2.3.0 closes the remaining common-runtime path after the v2.2.25 artwork-specific fix. While an iPhone is live in an artwork layout, the general network state machine now defers periodic NTP, offline retry, and saved-STA recovery instead of starting them independently of `CoreArtwork.inc`; automatic update reconnection observes the same deferral. The due work remains pending and resumes naturally after Bluetooth disconnects. Boot still establishes STA and completes its bounded NTP attempt before BLE advertising, artwork TLS stays on balanced coexistence, text-only layouts retain Wi-Fi-off behavior, and schema 10 is unchanged.

v2.3.1 responds to the confirmed post-validation `INT WDT` and persistent `network-failed` during the artwork transport. Live deployment testing proved that the owned Workers endpoint accepts port-80 POST directly and returns the same fixed 1,464-byte `MAB1` response. The NOW device therefore uses bounded plain HTTP for this one non-credential metadata/bitmap exchange, removing its TLS client and every runtime `esp_coex_preference_set` call; Cloudflare's upstream catalog/image work remains HTTPS. Track metadata is not encrypted on the device-to-Worker hop, which is disclosed in the portal and README. Exact length, magic, dimensions, component sizes, and CRC still protect parser/memory integrity but are not authentication. OTA and all other sensitive firmware traffic remain HTTPS; schema 10 is unchanged.

v2.3.2 folds common unsupported Latin diacritics to their ASCII equivalents only in the NOW OLED display copy, so metadata such as `ÁCIDO` renders as `ACIDO` without adding another font blob. Japanese, Korean, and other UTF-8 sequences remain byte-for-byte unchanged, while the original AMS title, artist, and album remain untouched for artwork lookup, cache identity, and diagnostics. The Worker also adds an exact title-and-artist override for the verified `ヤラララ - Yararara` Apple thumbnail because both bounded public catalog searches currently return no match despite the direct release page existing. Schema 10 and the `MAB1` packet remain unchanged.

v2.3.3 fixes the boot-update completion path bypassing the NOW artwork network requirement. Every update success, terminal failure, error hold, and postponed prompt now clears its deferred-sleep marker through one helper that enters `WIFI_OFF` only when `nowArtworkNeedsNetwork()` is false. Artwork layouts therefore retain the STA association established before BLE advertising, while text-only layouts preserve their configured Wi-Fi-off behavior. Live AMS sessions still prohibit starting a new association, avoiding the earlier radio-contention disconnect; schema 10 is unchanged.

v2.3.4 fixes three MEDIA runtime stalls and the low-rate live sender path. Stored-media playback now renders the pending update `U` marker so automatic checks cannot wait forever behind the MEDIA-owned framebuffer, and STREAM_MODE entry immediately replaces stale portal/NTP pixels with a buffering screen. Completed Wi-Fi test results no longer block a later stream start. Live push batches increase from 8 to 24 bounded frames to amortize Arduino WebServer's connection-close overhead, in-flight raw bodies cannot trip the stale-sender watchdog, fetches have a bounded abort timeout, and refill estimates follow measured OLED drain rate rather than requested source FPS. The sender reports a recent transfer rate; schema 10 and all profile boundaries are unchanged.

v2.3.5 corrects the remaining MEDIA live-stream starvation introduced by treating displayed FPS as queue-consumption FPS. The source timeline can discard stale frames while HTTP work temporarily suppresses OLED updates, so a low displayed measurement caused the browser to overestimate the queue, delay refill, hit the five-second stale limit, and receive `stream-inactive`. Queue estimation now follows the requested source clock, the browser POST timeout is 10 seconds, and device stale detection is a distinct 15 seconds. RAW body chunks service due OLED frames while WebServer remains inside one request, and render deadlines advance from their previous schedule instead of accumulating loop delay. Stream-stop counters and reason are emitted to serial; schema 10 is unchanged.

v2.3.6 lets a MEDIA live stream preempt an active NTP transaction without losing its higher-level intent. Stream start records a deferred-sync marker, stops lwIP SNTP, and moves the portal runtime out of `TIME_SYNCING` before entering the isolated hot path. On every normal stream exit it starts a fresh bounded NTP request and restores the existing Wi-Fi-test, manual-sync, or update-wait state; an allocation failure resumes NTP immediately. If STA network data is no longer ready, the original pending operation receives an explicit failure instead of remaining stuck. Active Wi-Fi association and firmware transfer remain hard stream-start exclusions; schema 10 is unchanged.

v2.3.7 fixes setup-AP startup after the saved 2.4GHz networks are unavailable. A cancelled STA connection or scan could leave the ESP-IDF radio reporting AP+STA mode and `192.168.4.1` even though no `MILESTONE-D1-SETUP` beacon was transmitted. Portal entry now preserves AP+STA only for a live station; otherwise it fully stops the radio, restarts in AP-only mode, verifies a visible configured SSID, and falls back to one more full-radio rebuild if startup fails. Portal scans still enable STA on demand, and schema 10 is unchanged.

v3.0.0 replaces the required SH1107 I2C display with the physically verified ST7735-compatible 128×160 SPI TFT. GPIO9 remains SCK and GPIO8 becomes MOSI; CS, reset, and DC use GPIO10, GPIO5, and GPIO6. The existing 128×128 one-bit U8g2/MSM1 surface is converted to RGB565 and centered, including dirty-tile stream updates; the top 16-pixel TFT-only band shows the active profile above a separator, while the bottom band remains empty below one separator. Three active-low `INPUT_PULLUP` switches add previous (GPIO1), next (GPIO2), and confirm (GPIO11); confirm mirrors the onboard BOOT action and the onboard BOOT button remains available. Previous/next navigation renders immediately on debounced press, before synchronous background network work. Because the TFT LED is hard-wired to 3V3, nonfunctional display brightness/night-brightness controls and runtime contrast mapping are removed; their old NVS keys remain reserved for schema-10 compatibility. USB CDC diagnostics use zero-timeout, capacity-checked writes so a closed serial monitor cannot stall the product loop. The incompatible hardware boundary requires a major version, while configuration schema 10 and all profile boundaries remain unchanged.

v3.1.0 adds the PSRAM RGB565 display layer, semantic CORE colors, colored status glyphs, a six-page confirm-driven device information view with PSRAM metrics, and the 1–3 second confirm-button profile selector. MEDIA extends MSM1 records with a reserved RGB332 pixel-format value while preserving old one-bit files; both stored and isolated live paths use bounded PSRAM buffers and the portal selects color or monochrome. NOW consumes the Worker `/v3/artwork` fixed `MAC1` RGB565 packet and retains `/v1` and `/v2` only for old firmware. Schema 11 persists the six CORE colors and MEDIA monochrome preference without allowing MEDIA/NOW to rewrite CORE view state.

v3.1.1 fixes an old-session GAP disconnect race in NOW by retaining separate connect, encryption, and disconnect handles and applying a disconnect only to the matching active connection. MEDIA color streaming increases each bounded PSRAM request batch from 4 to 12 frames and refills proactively before the 96-frame queue approaches underrun. CORE exposes the exact selected color values beside each picker. The 1–3 second button menu adds a device restart entry and exits immediately when the already-active profile is confirmed. Wi-Fi sleep and parked-retry waiting share an empty-circle glyph, and a fixed low-cost RGB LUT slightly raises contrast while reducing luminance for every CORE, MEDIA, NOW, artwork, status, and frame-chrome pixel. Schema 11 is unchanged.

The post-v2.3.1 Worker tone update retains the 4×4 Bayer packet contract but adds bounded adaptive correction at both luminance extremes. Mean luminance below 110 blends toward a gamma-0.8 lift capped at 32 while preserving 0–6 as true black; mean luminance above 160 blends toward gamma 3.5 highlight compression while preserving exact black and white. The curve is shared by both output sizes. Exact catalog exceptions use normalized title-and-artist equality, and cache generation `mab1-adaptive-tone-catalog-v2` bypasses prior positive and negative entries; packet format does not change.

Do not split the OTA transaction merely because the function is long. Its sequential structure encodes safety assumptions.

### Rollback

`CoreRollback.inc` was added in v1.8.0. Before installing a candidate, the previous application slot/version is recorded. A candidate boot remains in validation state until approximately 10 seconds of normal runtime have completed. A reset before confirmation can cause the previous OTA slot to be selected again.

Application-level rollback cannot recover a candidate that fails before the rollback guard itself executes; full early-boot recovery depends on bootloader rollback support.

### Runtime/input/thermal

`CoreRuntime.inc` coordinates the main loop, previous/next/confirm and onboard BOOT behavior, screen cycling, temperature protection, and high-level state progression. Previous/next navigate CORE views, MEDIA items, or NOW layouts. Confirm and onboard BOOT share the existing short/hold confirmation path. Preserve cooperative/early-return semantics when modifying state processing.

### Live streaming (v1.10.5+, buffering/render pacing revised in v1.10.8)

Live streaming is intentionally isolated from the normal cooperative runtime. `/stream` serves a dedicated lightweight browser page. The browser pre-converts the full source into 128×128 one-bit frames before playback, then sends paced `application/octet-stream` frame-record bodies. The first source frame is RAW and later frames use bounded XOR-RLE deltas only when smaller; the ESP32 reconstructs every accepted record into a raw PSRAM ring before playback. Live frames are never written to LittleFS.

While `mediaStreamActive` is true, `loopFirmware()` enters `processMediaStreamMode()` and returns before normal OTA, diagnostics, cycle, display, LED, NTP/reconnect scheduling, or stored-media work can run. The stream loop services only captive-portal networking, the binary frame-record transport, PSRAM queue/TFT timing, confirm-or-BOOT stop input, resource guards, and low-rate thermal checks. A stream started inside `processNetwork()` is detected immediately and also returns before any normal background subsystem runs.

Important stream contracts:

- PSRAM is required; the live ring capacity is 240 raw frames (480 KiB).
- Initial playback starts after 96 queued frames and recovers from underrun after 48 frames, giving the sender several seconds of jitter margin.
- Pushes carry at most 8 RAW/XOR-RLE frame records and receive a compact binary ACK; the browser refills by queue watermarks, and transport-loss retries reuse the same idempotent sequence/header/body.
- On Arduino-ESP32 3.3.11, raw-body routes do not expose URL query metadata through `server.arg()` during `RAW_START`; live pushes therefore carry session, sequence, frame-count, and encoded-byte length in explicitly collected `X-MILESTONE-*` request headers. Do not move these fields back into the push URL.
- Duplicate committed sequence numbers are consumed and ACKed without enqueueing frames twice.
- 20fps is the normal source-timebase target; 24fps is experimental. The TFT service clock adapts to measured flush cost and reserves a network-service slice; stale source frames are dropped rather than accumulating latency.
- The TFT path detects changed 8×8 tiles on both axes and converts row spans/bounding rectangles to RGB565 before falling back to a full centered 128×128 update.
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
- suppress the same event/detail for 60 seconds unless the event is explicitly forced; repeated Wi-Fi connect timeouts, NTP timeouts, and update-check failures use a one-hour window
- never persist every loop, ordinary screen transitions, or RSSI fluctuations
- keep `CONFIG_VERSION` independent; diagnostics storage is not part of schema 10 migration
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
release/MILESTONE_Media.bin
release/MILESTONE_Media.json
release/MILESTONE_Now.bin
release/MILESTONE_Now.json
```

Each manifest records the firmware version, profile, matching asset name, exact binary size, SHA-256, and release note.

In Taildrop mode, publication must hand off to the incoming project's release command before any remote mutation when that command differs from the installed launcher. Existing same-tag releases are repairable only after every already-published asset matches the current build byte-for-byte. A completed publish must download and verify all six CORE/MEDIA/NOW assets against the local build.

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

The tool also verifies Git identity, repository/branch, version consistency, both generated manifest/BIN pairs, commit attribution, tag safety, and GitHub authentication. It pushes `main` and the release tag atomically and publishes all four OTA assets with GitHub CLI.

Use `--dry-run` to validate/test/build without committing, replacing the live project, tagging, pushing, or publishing:

```bash
milestone-release --dry-run local X.Y.Z "short release note"
cd /tmp && milestone-release --dry-run taildrop X.Y.Z "short release note"
```

Use `--yes` only when an unattended final publish is explicitly desired.

## 8. Areas intentionally not refactored through v2.0.8

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


v1.10.3 was the last incremental live-stream implementation. v1.10.5 replaces that hot path: browser-side full preconversion, a dedicated `/stream` page, raw binary transport, a PSRAM stream queue, isolated STREAM_MODE execution, partial SH1107 updates, stream-specific thermal policy, and explicit resource/sequence recovery. v1.10.6 fixes the Arduino-ESP32 3.3.11 raw `WebServer` metadata incompatibility by carrying session/sequence/frame-count in explicitly collected headers instead of URL query arguments. v1.10.7 then removes the two-frame ACK-locked sender cadence. v1.10.8 expands the PSRAM ring to 240 frames, starts after a 96-frame jitter buffer, uses bounded RAW/XOR-RLE live frame records, separates the source timeline from an adaptive OLED service clock that reserves network time after each flush, and adds X/Y dirty-tile partial refresh. v1.11.0 leaves that transport/render contract unchanged and only suspends optional BLE around STREAM_MODE. v1.11.1 changes only normal Wi-Fi/AP discovery and retry behavior; STREAM_MODE transport/render contracts remain unchanged. Do not reintroduce the v1.10.3 multipart/burst sender, query-based raw metadata, or a tiny browser lead buffer into the live path.
