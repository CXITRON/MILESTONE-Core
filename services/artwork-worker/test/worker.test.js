import assert from "node:assert/strict";
import test from "node:test";
import jpeg from "jpeg-js";
import worker, {
  chooseAppleArtwork,
  chooseDeezerArtwork,
  extractAppleMusicPageArtwork,
  extractReleaseGroups,
  crc32,
  makeAdaptiveToneLut,
  makeBitmapPacket,
  makeMonochromeBitmap,
  normalizeText,
} from "../src/worker.js";

class MemoryCache {
  constructor() {
    this.values = new Map();
  }

  async match(request) {
    const response = this.values.get(request.url);
    return response?.clone();
  }

  async put(request, response) {
    this.values.set(request.url, response.clone());
  }
}

function decodeForTest(bytes) {
  return jpeg.decode(bytes, {
    useTArray: true,
    formatAsRGBA: true,
    tolerantDecoding: true,
    maxResolutionInMP: 0.05,
    maxMemoryUsageInMB: 4,
  });
}

test("metadata normalization is stable and bounded", () => {
  assert.equal(normalizeText("  Ａ  song\nname "), "A song name");
  assert.equal(normalizeText("x".repeat(300)).length, 192);
});

test("Apple result selection prefers an exact localized track and artist", () => {
  const results = [
    { trackName: "Other", artistName: "Artist", artworkUrl100: "https://img/other.jpg" },
    { trackName: "Ｏｒｉｏｎ", artistName: "요네즈  켄시", artworkUrl100: "https://img/exact.jpg" },
  ];
  assert.equal(chooseAppleArtwork(results, "Orion", "요네즈 켄시"), "https://img/exact.jpg");
});

test("Apple Music page selection validates the top result title and artist", () => {
  const html = '<div class="top" data-testid="top-search-result" ' +
    'aria-label="マーシャル・マキシマイザー (feat. 可不) · 노래 · 柊マグネタイト">' +
    '<source srcset="https://is1-ssl.mzstatic.com/image/thumb/id/110x110bb-60.jpg 110w">';
  assert.equal(
    extractAppleMusicPageArtwork(
      html, "マーシャル・マキシマイザー (feat. 可不)", "柊マグネタイト",
    ),
    "https://is1-ssl.mzstatic.com/image/thumb/id/110x110bb-60.jpg",
  );
  assert.equal(extractAppleMusicPageArtwork(html, "Other", "柊マグネタイト"), "");
});

test("Deezer selection can match album when the artist name is localized", () => {
  const results = [{
    title: "Orion",
    title_short: "Orion",
    artist: { name: "Kenshi Yonezu" },
    album: { title: "BOOTLEG", cover_medium: "https://img/deezer.jpg" },
  }];
  assert.equal(
    chooseDeezerArtwork(results, "orion", "요네즈 켄시", "BOOTLEG"),
    "https://img/deezer.jpg",
  );
});

test("MusicBrainz parser accepts attribute order and removes duplicates", () => {
  const first = "12345678-1234-1234-9234-1234567890ab";
  const second = "abcdefab-cdef-4abc-8def-abcdefabcdef";
  const xml = `<release-group type="Album" id="${first}"></release-group>` +
    `<release-group id="${first}" type="Album"></release-group>` +
    `<release-group id="${second}"></release-group>`;
  assert.deepEqual(extractReleaseGroups(xml), [first, second]);
});

test("bitmap conversion uses the firmware LSB-first monochrome layout", () => {
  const rgb = new Uint8Array([
    255, 255, 255, 255, 0, 0, 0, 255,
    255, 255, 255, 255, 0, 0, 0, 255,
  ]);
  assert.deepEqual(
    makeMonochromeBitmap(rgb, 2, 2, 2, 2),
    new Uint8Array([0x01, 0x01]),
  );
});

test("adaptive tone correction lifts dark covers without changing normal exposure", () => {
  const dark = new Uint8Array(16 * 16 * 4);
  for (let offset = 0; offset < dark.length; offset += 4) {
    dark[offset] = 64;
    dark[offset + 1] = 64;
    dark[offset + 2] = 64;
    dark[offset + 3] = 255;
  }
  const uncorrected = makeMonochromeBitmap(dark, 16, 16, 16, 16, 4, null);
  const corrected = makeMonochromeBitmap(dark, 16, 16, 16, 16);
  const whiteBits = (bitmap) => [...bitmap]
    .reduce((total, byte) => total + byte.toString(2).replaceAll("0", "").length, 0);
  assert.equal(whiteBits(uncorrected), 0);
  assert.ok(whiteBits(corrected) >= 32, "dark cover should recover visible ordered detail");

  const normal = new Uint8Array(16 * 16 * 4);
  for (let offset = 0; offset < normal.length; offset += 4) {
    normal[offset] = 128;
    normal[offset + 1] = 128;
    normal[offset + 2] = 128;
    normal[offset + 3] = 255;
  }
  assert.equal(makeAdaptiveToneLut(normal, 16, 16), null);
});

test("adaptive tone correction preserves deliberate true black", () => {
  const black = new Uint8Array(16 * 16 * 4);
  for (let offset = 3; offset < black.length; offset += 4) black[offset] = 255;
  assert.deepEqual(
    makeMonochromeBitmap(black, 16, 16, 16, 16),
    new Uint8Array(32),
  );
});

test("bitmap packet contains both fixed sizes and a payload CRC", async () => {
  const rgba = new Uint8Array(16 * 16 * 4);
  for (let offset = 0; offset < rgba.length; offset += 4) {
    rgba[offset] = 255;
    rgba[offset + 1] = 255;
    rgba[offset + 2] = 255;
    rgba[offset + 3] = 255;
  }
  const encoded = jpeg.encode({ data: rgba, width: 16, height: 16 }, 90).data;
  const packet = await makeBitmapPacket(encoded, decodeForTest);
  const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
  assert.equal(new TextDecoder().decode(packet.subarray(0, 4)), "MAB1");
  assert.deepEqual([...packet.subarray(4, 8)], [60, 60, 88, 88]);
  assert.equal(view.getUint16(8, false), 480);
  assert.equal(view.getUint16(10, false), 968);
  assert.equal(packet.byteLength, 1464);
  assert.equal(view.getUint32(12, false), crc32(packet.subarray(16)));
});

test("worker returns and reuses a converted bitmap cache entry", async () => {
  const cache = new MemoryCache();
  let deezerCalls = 0;
  let imageCalls = 0;
  const rgba = new Uint8Array(16 * 16 * 4).fill(255);
  const jpegBytes = jpeg.encode({ data: rgba, width: 16, height: 16 }, 90).data;
  const upstream = async (url) => {
    const value = String(url);
    if (value.startsWith("https://api.deezer.com/search")) {
      deezerCalls += 1;
      return Response.json({
        data: [{
          title: "Song",
          title_short: "Song",
          artist: { name: "Artist" },
          album: { title: "Album", cover_medium: "https://img.example/cover.jpg" },
        }],
      });
    }
    if (value === "https://img.example/cover.jpg") {
      imageCalls += 1;
      return new Response(jpegBytes, {
        headers: { "Content-Type": "image/jpeg", "Content-Length": String(jpegBytes.byteLength) },
      });
    }
    throw new Error(`unexpected URL: ${value}`);
  };
  const env = { __cache: cache, __fetch: upstream, __decode: decodeForTest };
  const pending = [];
  const context = { waitUntil(promise) { pending.push(promise); } };
  const makeRequest = () => new Request("https://worker.example/v2/artwork", {
    method: "POST",
    body: new URLSearchParams({ title: "Song", artist: "Artist", album: "Album" }),
  });

  const first = await worker.fetch(makeRequest(), env, context);
  await Promise.all(pending.splice(0));
  assert.equal(first.status, 200);
  assert.equal(first.headers.get("content-type"), "application/vnd.milestone.artwork-bitmap");
  assert.equal(first.headers.get("x-milestone-artwork"), "2");
  assert.equal(first.headers.get("x-milestone-tone"), "adaptive-v1");
  assert.equal((await first.arrayBuffer()).byteLength, 1464);

  const second = await worker.fetch(makeRequest(), env, context);
  assert.equal(second.status, 200);
  assert.equal(deezerCalls, 1);
  assert.equal(imageCalls, 1);
});

test("worker falls back to a validated Apple Music page result when Deezer has none", async () => {
  const cache = new MemoryCache();
  let deezerCalls = 0;
  let appleCalls = 0;
  const rgba = new Uint8Array(16 * 16 * 4).fill(255);
  const jpegBytes = jpeg.encode({ data: rgba, width: 16, height: 16 }, 90).data;
  const upstream = async (url) => {
    const value = String(url);
    if (value.startsWith("https://api.deezer.com/search")) {
      deezerCalls += 1;
      return Response.json({ data: [] });
    }
    if (value.startsWith("https://music.apple.com/kr/search")) {
      appleCalls += 1;
      return new Response(
        '<div data-testid="top-search-result" ' +
        'aria-label="マーシャル・マキシマイザー (feat. 可不) · 노래 · 柊マグネタイト">' +
        '<source srcset="https://img.example/apple.jpg/110x110bb-60.jpg 110w">',
      );
    }
    if (value === "https://img.example/apple.jpg/110x110bb-60.jpg") {
      return new Response(jpegBytes, {
        headers: { "Content-Type": "image/jpeg", "Content-Length": String(jpegBytes.byteLength) },
      });
    }
    throw new Error(`unexpected URL: ${value}`);
  };
  const response = await worker.fetch(new Request("https://worker.example/v2/artwork", {
    method: "POST",
    body: new URLSearchParams({
      title: "マーシャル・マキシマイザー (feat. 可不)",
      artist: "柊マグネタイト",
      album: "KAF+YOU KAFU COMPILATION ALBUM シンメトリー",
    }),
  }), { __cache: cache, __fetch: upstream, __decode: decodeForTest }, { waitUntil() {} });
  assert.equal(response.status, 200);
  assert.equal(deezerCalls, 2);
  assert.equal(appleCalls, 1);
  assert.equal((await response.arrayBuffer()).byteLength, 1464);
});

test("v1 endpoint remains JPEG-compatible during firmware rollout", async () => {
  const rgba = new Uint8Array(8 * 8 * 4).fill(255);
  const jpegBytes = jpeg.encode({ data: rgba, width: 8, height: 8 }, 90).data;
  const upstream = async (url) => String(url).startsWith("https://api.deezer.com/search")
    ? Response.json({ data: [{
      title: "Song", title_short: "Song", artist: { name: "Artist" },
      album: { title: "Album", cover_medium: "https://img.example/cover.jpg" },
    }] })
    : new Response(jpegBytes, { headers: { "Content-Type": "image/jpeg" } });
  const response = await worker.fetch(new Request("https://worker.example/v1/artwork", {
    method: "POST",
    body: new URLSearchParams({ title: "Song", artist: "Artist", album: "Album" }),
  }), { __cache: new MemoryCache(), __fetch: upstream }, { waitUntil() {} });
  assert.equal(response.status, 200);
  assert.equal(response.headers.get("content-type"), "image/jpeg");
  assert.equal(response.headers.get("x-milestone-artwork"), "1");
});

test("invalid metadata is rejected without an upstream call", async () => {
  const request = new Request("https://worker.example/v1/artwork", {
    method: "POST",
    body: new URLSearchParams({ title: "Song" }),
  });
  const response = await worker.fetch(
    request,
    { __cache: new MemoryCache(), __fetch: () => assert.fail("must not fetch") },
    { waitUntil() {} },
  );
  assert.equal(response.status, 400);
});
