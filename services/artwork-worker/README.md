# MILESTONE Artwork Worker

This Cloudflare Worker is the optional artwork gateway for the NOW firmware.
It races two bounded Deezer catalog searches against the first 48 KiB of Apple
Music's public search page. The Apple result is accepted only when its top
result contains both the normalized title and artist, and only its bounded JPEG
thumbnail is fetched. Positive and negative responses are stored in the
Cloudflare edge cache. `/v1/artwork` preserves the JPEG response for v2.2.21
devices.
`/v2/artwork` decodes with Wasm MozJPEG, applies bounded adaptive exposure and
a gamma-0.8 lookup table only to dark covers, then uses 4×4 Bayer ordered
dithering. It returns a fixed 1,464-byte `MAB1` packet containing 60×60 and
88×88 one-bit bitmaps plus CRC-32. True black is preserved and images with mean
luminance 110 or higher are not tone-adjusted. The v2.3.1 device makes one
bounded HTTP request and keeps AMS text/progress available when the gateway fails.

The deployment intentionally uses only Workers Free and the Cache API. It does
not bind Images, R2, Durable Objects, or a paid database. Free-plan quota
exhaustion therefore fails closed instead of creating usage charges.

## Test

```bash
node --test test/*.test.js
```

## Deploy

```bash
npx wrangler login
npx wrangler deploy
```

After deployment, the v2.3.1 firmware uses the port-80
`http://...workers.dev/v2/artwork` endpoint to avoid BLE/TLS watchdog contention.
Do not add Cloudflare credentials or `.dev.vars` to Git.
