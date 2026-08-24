import assert from "node:assert/strict";
import test from "node:test";
import worker, {
  chooseAppleArtwork,
  chooseDeezerArtwork,
  extractReleaseGroups,
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

test("worker returns and reuses a provider JPEG cache entry", async () => {
  const cache = new MemoryCache();
  let deezerCalls = 0;
  let imageCalls = 0;
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
      return new Response(new Uint8Array([0xff, 0xd8, 0xff, 0xd9]), {
        headers: { "Content-Type": "image/jpeg", "Content-Length": "4" },
      });
    }
    throw new Error(`unexpected URL: ${value}`);
  };
  const env = { __cache: cache, __fetch: upstream };
  const pending = [];
  const context = { waitUntil(promise) { pending.push(promise); } };
  const makeRequest = () => new Request("https://worker.example/v1/artwork", {
    method: "POST",
    body: new URLSearchParams({ title: "Song", artist: "Artist", album: "Album" }),
  });

  const first = await worker.fetch(makeRequest(), env, context);
  await Promise.all(pending.splice(0));
  assert.equal(first.status, 200);
  assert.equal(first.headers.get("x-milestone-artwork"), "1");

  const second = await worker.fetch(makeRequest(), env, context);
  assert.equal(second.status, 200);
  assert.equal(deezerCalls, 1);
  assert.equal(imageCalls, 1);
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
