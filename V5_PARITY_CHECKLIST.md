# MILESTONE v5.0.0 legacy parity checklist

This checklist is the release gate for restoring the proven ESP32-S3 Zero
single-board behavior inside the v5 MAIN/ZERO architecture.  Live streaming is
the only intentional omission.

## Checkpoint 1 — display and physical input

- [ ] CORE seven views use the legacy 128×128 U8g2 typography and coordinates.
- [x] Long UTF-8 text wraps/scrolls and configured centered text is measured by
      the active font before drawing.
- [ ] MEDIA browsing, still image, animation, pause and error screens retain the
      legacy presentation.
- [ ] NOW waiting and all four Now Playing layouts retain the legacy presentation.
- [x] Profile selection uses a legacy firmware-selection style screen with a
      visible selection, PREV/NEXT navigation, OK confirmation and BACK cancel.
- [x] The v5 top/bottom status bands remain outside the legacy 128×128 body.
- [ ] Device UI explanations are Korean; CORE/MEDIA/NOW, MAIN/ZERO, Wi-Fi, AP,
      OTA, SAFE and Stable/Backup/Recovery remain product/technical names.
- [x] MODE menu contains setup AP; AP can close with front-panel BACK.
- [x] The three-second legacy boot splash is shown before the active profile.

## Checkpoint 2 — setup AP and network parity

- [x] SSID is `MILESTONE-D1-SETUP`.
- [x] Default password is a fresh eight-character legacy-format password.
- [x] Fixed 8–63 character password and explicitly confirmed open AP are stored
      without returning the secret to the browser.
- [x] TFT setup screen matches the legacy layout and centers the address/password.
- [x] Responsive legacy portal styling and captive-portal routes are restored.
- [ ] Asynchronous 2.4 GHz scan lists RSSI, open/Personal, WPA2-Enterprise PEAP,
      and unsupported WPA3/CA-required networks.
- [x] Connection test keeps AP available and stores only after stable success.
- [x] Up to eight A/B-protected networks are ordered by success and can be reused
      or individually deleted.
- [x] Portal close, manual time sync, settings-default reset and factory reset are
      restored.

## Checkpoint 3 — non-streaming feature parity

- [ ] Display/time/cycle/color/scroll/burn-in/screen-off settings match legacy.
- [ ] Status, six device-information pages and diagnostics export/clear match legacy.
- [ ] LED day/night, Wi-Fi sleep/retry, NTP cadence and thermal controls remain.
- [ ] Browser photo/GIF/video conversion, preview, upload, list, rename, ordering,
      enable/disable, delete, clear and explicit repair work on SD-backed media.
- [ ] NOW layout selection and artwork search/preview/upload/pin/block/refresh work.
- [ ] Update check/download/install uses the signed MAIN/ZERO catalog and physical
      confirmation without reintroducing profile firmware swapping.
- [ ] Legacy schema-12 import is one-way and does not erase old data.
- [x] No `/stream`, WebSocket frame path or STREAM_MODE is present in v5.

## Checkpoint 4 — verification and delivery

- [x] Host v3 and v5 regression suites pass.
- [ ] MAIN, ZERO and SAFE product-key builds pass and the 13-asset catalog verifies.
- [ ] USB initial images are uploaded to both attached boards and esptool verifies
      every write.
- [ ] MAIN/ZERO link, TFT, five buttons, SD, RTC and AHT20 smoke checks pass.
- [ ] Git commit identity/hygiene checks pass.
- [ ] `milestone-release local 5.0.0` publishes and re-download-verifies the release.
