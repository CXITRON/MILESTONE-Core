import decodeJpeg, { init as initJpeg } from "@jsquash/jpeg/decode.js";
import jpegDecoderWasm from "@jsquash/jpeg/codec/dec/mozjpeg_dec.wasm";
import worker from "./worker.js";

initJpeg(jpegDecoderWasm);

async function decodeArtwork(jpeg) {
  const buffer = jpeg.buffer.slice(jpeg.byteOffset, jpeg.byteOffset + jpeg.byteLength);
  return decodeJpeg(buffer);
}

export default {
  fetch(request, env, context) {
    return worker.fetch(request, { ...env, __decode: decodeArtwork }, context);
  },
};
