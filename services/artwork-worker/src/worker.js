const MAX_BODY_BYTES = 1400;
const MAX_FIELD_LENGTH = 192;
const MAX_LOOKUP_BYTES = 64 * 1024;
const MAX_SOURCE_IMAGE_BYTES = 220 * 1024;
const POSITIVE_CACHE_SECONDS = 30 * 24 * 60 * 60;
const NEGATIVE_CACHE_SECONDS = 6 * 60 * 60;
const ERROR_CACHE_SECONDS = 60;
const SMALL_WIDTH = 60;
const SMALL_HEIGHT = 60;
const LARGE_WIDTH = 88;
const LARGE_HEIGHT = 88;
const SMALL_BYTES = Math.ceil(SMALL_WIDTH / 8) * SMALL_HEIGHT;
const LARGE_BYTES = Math.ceil(LARGE_WIDTH / 8) * LARGE_HEIGHT;
const BITMAP_HEADER_BYTES = 16;
const BITMAP_PACKET_BYTES = BITMAP_HEADER_BYTES + SMALL_BYTES + LARGE_BYTES;
const BITMAP_CONTENT_TYPE = "application/vnd.milestone.artwork-bitmap";
const CRC32_TABLE = new Uint32Array(256);
for (let value = 0; value < CRC32_TABLE.length; value += 1) {
  let crc = value;
  for (let bit = 0; bit < 8; bit += 1) {
    crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  CRC32_TABLE[value] = crc >>> 0;
}
const USER_AGENT =
  "MILESTONE-Artwork/1 (https://github.com/CXITRON/MILESTONE-Core)";

function normalizeText(value) {
  return String(value || "")
    .normalize("NFKC")
    .replace(/\s+/gu, " ")
    .trim()
    .slice(0, MAX_FIELD_LENGTH);
}

function comparableText(value) {
  return normalizeText(value).toLocaleLowerCase("und");
}

function validUuid(value) {
  return /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(
    value,
  );
}

async function readLimited(response, maximum) {
  const announced = Number(response.headers.get("content-length") || 0);
  if (announced > maximum) throw new Error("response-too-large");
  const reader = response.body?.getReader();
  if (!reader) return new Uint8Array();
  const chunks = [];
  let total = 0;
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > maximum) {
      await reader.cancel("response-too-large");
      throw new Error("response-too-large");
    }
    chunks.push(value);
  }
  const joined = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    joined.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return joined;
}

function timeoutSignal(milliseconds) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort("timeout"), milliseconds);
  return { signal: controller.signal, cancel: () => clearTimeout(timer) };
}

async function boundedFetch(fetcher, url, options = {}, timeoutMs = 6500) {
  const timeout = timeoutSignal(timeoutMs);
  try {
    return await fetcher(url, { ...options, signal: timeout.signal });
  } finally {
    timeout.cancel();
  }
}

function chooseAppleArtwork(results, title, artist) {
  if (!Array.isArray(results)) return "";
  const wantedTitle = comparableText(title);
  const wantedArtist = comparableText(artist);
  const usable = results.filter(
    (item) => item && typeof item.artworkUrl100 === "string" &&
      item.artworkUrl100.startsWith("https://"),
  );
  const exact = usable.find(
    (item) => comparableText(item.trackName) === wantedTitle &&
      (!wantedArtist || comparableText(item.artistName) === wantedArtist),
  );
  return (exact || usable[0])?.artworkUrl100 || "";
}

function chooseDeezerArtwork(results, title, artist, album) {
  if (!Array.isArray(results)) return "";
  const wantedTitle = comparableText(title);
  const wantedArtist = comparableText(artist);
  const wantedAlbum = comparableText(album);
  const usable = results.filter((item) => {
    const url = item?.album?.cover_medium;
    return typeof url === "string" && url.startsWith("https://") &&
      comparableText(item.title_short || item.title) === wantedTitle;
  });
  const albumMatch = wantedAlbum && usable.find(
    (item) => comparableText(item.album?.title) === wantedAlbum,
  );
  const artistMatch = wantedArtist && usable.find(
    (item) => comparableText(item.artist?.name) === wantedArtist,
  );
  const selected = (albumMatch || artistMatch)?.album?.cover_medium || "";
  return selected.replace("/250x250-", "/96x96-");
}

async function deezerArtwork(fetcher, metadata, trace) {
  const queries = [];
  if (metadata.album) queries.push(`track:"${metadata.title}" album:"${metadata.album}"`);
  queries.push(`track:"${metadata.title}" artist:"${metadata.artist}"`);
  for (let index = 0; index < queries.length; index += 1) {
    const url = new URL("https://api.deezer.com/search");
    url.search = new URLSearchParams({ q: queries[index], limit: "5" });
    const response = await boundedFetch(fetcher, url, {
      headers: { "User-Agent": USER_AGENT, Accept: "application/json" },
    });
    trace.push(`deezer-${index + 1}:${response.status}`);
    if (!response.ok) continue;
    const bytes = await readLimited(response, MAX_LOOKUP_BYTES);
    const body = JSON.parse(new TextDecoder().decode(bytes));
    trace.push(`deezer-count-${index + 1}:${Array.isArray(body.data) ? body.data.length : -1}`);
    const artwork = chooseDeezerArtwork(
      body.data, metadata.title, metadata.artist, metadata.album,
    );
    trace.push(`deezer-match-${index + 1}:${artwork ? 1 : 0}`);
    if (artwork) return artwork;
  }
  return "";
}

function extractReleaseGroups(xml, maximum = 3) {
  const values = [];
  const seen = new Set();
  for (const match of xml.matchAll(/<release-group\b[^>]*>/giu)) {
    const id = /\bid\s*=\s*["']([^"']+)["']/iu.exec(match[0])?.[1] || "";
    if (!validUuid(id) || seen.has(id.toLowerCase())) continue;
    seen.add(id.toLowerCase());
    values.push(id.toLowerCase());
    if (values.length >= maximum) break;
  }
  return values;
}

async function appleArtwork(fetcher, metadata, trace) {
  const term = [metadata.title, metadata.artist, metadata.album].filter(Boolean).join(" ");
  const url = new URL("https://itunes.apple.com/search");
  url.search = new URLSearchParams({
    country: "KR",
    media: "music",
    entity: "song",
    limit: "3",
    term,
  });
  const response = await boundedFetch(fetcher, url, {
    headers: { "User-Agent": USER_AGENT, Accept: "application/json" },
  });
  trace.push(`apple-search:${response.status}`);
  if (!response.ok) return "";
  const bytes = await readLimited(response, MAX_LOOKUP_BYTES);
  const body = JSON.parse(new TextDecoder().decode(bytes));
  return chooseAppleArtwork(body.results, metadata.title, metadata.artist);
}

function musicBrainzQuery(kind, value, artist) {
  const url = new URL(`https://musicbrainz.org/ws/2/${kind}/`);
  const field = kind === "release-group" ? "release" : "recording";
  url.search = new URLSearchParams({
    fmt: "xml",
    limit: "3",
    query: `${field}:"${value.replaceAll('"', "\\\"")}" AND artist:"${artist.replaceAll('"', "\\\"")}"`,
  });
  return url;
}

async function musicBrainzCandidates(fetcher, metadata, trace) {
  const queries = [];
  if (metadata.album) queries.push(musicBrainzQuery("release-group", metadata.album, metadata.artist));
  queries.push(musicBrainzQuery("recording", metadata.title, metadata.artist));
  for (let index = 0; index < queries.length; index += 1) {
    if (index) await new Promise((resolve) => setTimeout(resolve, 1200));
    const response = await boundedFetch(fetcher, queries[index], {
      headers: { "User-Agent": USER_AGENT, Accept: "application/xml" },
    }, 7000);
    trace.push(`mb-${index + 1}:${response.status}`);
    if (!response.ok) return [];
    const bytes = await readLimited(response, MAX_LOOKUP_BYTES);
    const candidates = extractReleaseGroups(new TextDecoder().decode(bytes));
    if (candidates.length) return candidates;
  }
  return [];
}

async function sourceArtwork(fetcher, metadata) {
  const trace = [];
  try {
    const deezerUrl = await deezerArtwork(fetcher, metadata, trace);
    if (deezerUrl) {
      const response = await boundedFetch(fetcher, deezerUrl, {
        headers: { "User-Agent": USER_AGENT, Accept: "image/jpeg,image/*" },
        redirect: "follow",
      });
      trace.push(`deezer-image:${response.status}`);
      if (response.ok) return { response, trace };
    }
  } catch (error) {
    trace.push(`deezer-error:${error?.message || "unknown"}`);
  }
  return { response: null, trace };
}

function makeMonochromeBitmap(rgb, sourceWidth, sourceHeight, targetWidth, targetHeight,
                              channels = 4) {
  const stride = Math.ceil(targetWidth / 8);
  const output = new Uint8Array(stride * targetHeight);
  const bayer = [-8, 0, -6, 2, 4, -4, 6, -2, -5, 3, -7, 1, 7, -1, 5, -3];
  const sourceXs = new Uint16Array(targetWidth);
  const sourceYs = new Uint16Array(targetHeight);
  for (let x = 0; x < targetWidth; x += 1) {
    sourceXs[x] = Math.floor(x * sourceWidth / targetWidth);
  }
  for (let y = 0; y < targetHeight; y += 1) {
    sourceYs[y] = Math.floor(y * sourceHeight / targetHeight);
  }
  for (let y = 0; y < targetHeight; y += 1) {
    const sourceRow = sourceYs[y] * sourceWidth;
    const targetRow = y * stride;
    const bayerRow = (y & 3) * 4;
    for (let x = 0; x < targetWidth; x += 1) {
      const offset = (sourceRow + sourceXs[x]) * channels;
      const luminance = (rgb[offset] * 54 + rgb[offset + 1] * 183 + rgb[offset + 2] * 19) >> 8;
      if (luminance >= 128 + bayer[bayerRow + (x & 3)] * 5) {
        output[targetRow + (x >> 3)] |= 1 << (x & 7);
      }
    }
  }
  return output;
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc = CRC32_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  }
  return (~crc) >>> 0;
}

async function makeBitmapPacket(jpeg, decoder) {
  if (typeof decoder !== "function") throw new Error("missing-image-decoder");
  const decoded = await decoder(jpeg);
  if (!decoded.width || !decoded.height ||
      decoded.data.byteLength !== decoded.width * decoded.height * 4) {
    throw new Error("invalid-decoded-image");
  }
  const small = makeMonochromeBitmap(
    decoded.data, decoded.width, decoded.height, SMALL_WIDTH, SMALL_HEIGHT,
  );
  const large = makeMonochromeBitmap(
    decoded.data, decoded.width, decoded.height, LARGE_WIDTH, LARGE_HEIGHT,
  );
  const packet = new Uint8Array(BITMAP_PACKET_BYTES);
  packet.set([0x4d, 0x41, 0x42, 0x31], 0); // "MAB1"
  packet.set([SMALL_WIDTH, SMALL_HEIGHT, LARGE_WIDTH, LARGE_HEIGHT], 4);
  const view = new DataView(packet.buffer);
  view.setUint16(8, SMALL_BYTES, false);
  view.setUint16(10, LARGE_BYTES, false);
  packet.set(small, BITMAP_HEADER_BYTES);
  packet.set(large, BITMAP_HEADER_BYTES + SMALL_BYTES);
  view.setUint32(12, crc32(packet.subarray(BITMAP_HEADER_BYTES)), false);
  return packet;
}

async function makeDeviceBitmap(source, decoder) {
  if (!source.body) throw new Error("empty-image");
  const jpeg = await readLimited(source, MAX_SOURCE_IMAGE_BYTES);
  return makeBitmapPacket(jpeg, decoder);
}

async function makeDeviceJpeg(source) {
  if (!source.body) throw new Error("empty-image");
  return readLimited(source, MAX_SOURCE_IMAGE_BYTES);
}

function cachedResponse(status, body, seconds, extraHeaders = {}) {
  return new Response(body, {
    status,
    headers: {
      "Cache-Control": `public, max-age=${seconds}`,
      "X-Content-Type-Options": "nosniff",
      ...extraHeaders,
    },
  });
}

async function cacheKey(requestUrl, metadata, format) {
  const canonical = [metadata.title, metadata.artist, metadata.album]
    .map(comparableText)
    .join("\u001f");
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(canonical));
  const hash = [...new Uint8Array(digest)]
    .map((byte) => byte.toString(16).padStart(2, "0"))
    .join("");
  const origin = new URL(requestUrl).origin;
  return new Request(`${origin}/__artwork_cache/${format}/${hash}`, { method: "GET" });
}

async function handleArtwork(request, env, context, format = "mab1") {
  const declared = Number(request.headers.get("content-length") || 0);
  if (declared > MAX_BODY_BYTES) return cachedResponse(413, "", ERROR_CACHE_SECONDS);
  const body = await readLimited(new Response(request.body), MAX_BODY_BYTES);
  const form = new URLSearchParams(new TextDecoder().decode(body));
  const metadata = {
    title: normalizeText(form.get("title")),
    artist: normalizeText(form.get("artist")),
    album: normalizeText(form.get("album")),
  };
  if (!metadata.title || !metadata.artist) {
    return cachedResponse(400, "", ERROR_CACHE_SECONDS);
  }

  const key = await cacheKey(request.url, metadata, format);
  const edgeCache = env.__cache || caches.default;
  const cached = await edgeCache.match(key);
  if (cached) return cached;

  const fetcher = env.__fetch || fetch;
  const source = await sourceArtwork(fetcher, metadata);
  const trace = source.trace.join(",").slice(0, 240);
  let response;
  if (!source.response) {
    response = cachedResponse(204, null, NEGATIVE_CACHE_SECONDS, {
      "X-Milestone-Upstream": trace,
    });
  } else {
    try {
      if (format === "mab1") {
        const bitmap = await makeDeviceBitmap(source.response, env.__decode);
        response = cachedResponse(200, bitmap, POSITIVE_CACHE_SECONDS, {
          "Content-Type": BITMAP_CONTENT_TYPE,
          "Content-Length": String(bitmap.byteLength),
          "X-Milestone-Artwork": "2",
          "X-Milestone-Upstream": trace,
        });
      } else {
        const jpeg = await makeDeviceJpeg(source.response);
        response = cachedResponse(200, jpeg, POSITIVE_CACHE_SECONDS, {
          "Content-Type": "image/jpeg",
          "Content-Length": String(jpeg.byteLength),
          "X-Milestone-Artwork": "1",
          "X-Milestone-Upstream": trace,
        });
      }
    } catch {
      response = cachedResponse(502, "", ERROR_CACHE_SECONDS, {
        "X-Milestone-Upstream": `${trace},transform-error`.slice(0, 240),
      });
    }
  }
  context.waitUntil(edgeCache.put(key, response.clone()));
  return response;
}

export default {
  async fetch(request, env, context) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/health") {
      return new Response("ok", { headers: { "Cache-Control": "no-store" } });
    }
    const format = url.pathname === "/v2/artwork" ? "mab1"
      : url.pathname === "/v1/artwork" ? "jpeg" : "";
    if (request.method !== "POST" || !format) {
      return new Response("Not found", { status: 404 });
    }
    try {
      return await handleArtwork(request, env, context, format);
    } catch {
      return cachedResponse(502, "", ERROR_CACHE_SECONDS);
    }
  },
};

export {
  chooseAppleArtwork,
  chooseDeezerArtwork,
  comparableText,
  crc32,
  extractReleaseGroups,
  handleArtwork,
  makeBitmapPacket,
  makeMonochromeBitmap,
  normalizeText,
};
