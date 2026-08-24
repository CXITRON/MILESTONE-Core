# MILESTONE Artwork Worker

This Cloudflare Worker is the optional artwork gateway for the NOW firmware.
It performs two bounded Deezer catalog searches, selects a 96-pixel provider
thumbnail, and stores positive and negative responses in the Cloudflare edge
cache. `/v1/artwork` preserves the JPEG response for v2.2.21 devices.
`/v2/artwork` decodes with Wasm MozJPEG and returns a fixed 1,464-byte `MAB1`
packet containing 60×60 and 88×88 one-bit bitmaps plus CRC-32. The device makes
one TLS request and keeps AMS text/progress available when the gateway fails.

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

After deployment, set `MILESTONE_ARTWORK_SERVICE_URL` in the firmware to the
reported `https://...workers.dev/v2/artwork` URL and run the normal MILESTONE
release workflow. Do not add Cloudflare credentials or `.dev.vars` to Git.
