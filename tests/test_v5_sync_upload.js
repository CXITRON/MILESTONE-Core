// Execute the actual browser chunk sender, including response-loss recovery.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('v5/MilestoneV5Main/V5SyncPage.h', 'utf8');
const sender = source.slice(source.indexOf('async function sendStreamingChunk'),
                            source.indexOf('async function convertAndStore'));
let calls = [], saved = 0, loseResponse = false, fail = false, finalState = false;
let largestBatch = 0;
class Form { append(name, blob) { this.blob = blob; } }
const context = vm.createContext({
  Blob, FormData: Form, Error, Promise,
  setTimeout: callback => { callback(); },
  boundedFetch: async (path, options) => {
    calls.push(path);
    largestBatch = Math.max(largestBatch, options.body.blob.size);
    if (fail) throw new Error('disconnected');
    saved += options.body.blob.size;
    finalState = path.includes('final=1');
    if (loseResponse) { loseResponse = false; throw new Error('response lost'); }
    return { ok: true, json: async () => ({ ok: true }) };
  },
  api: async () => ({ state: finalState ? 'indexing' : 'uploading', written_bytes: saved }),
});
vm.runInContext(sender, context);
(async () => {
  context.parts = [new Uint8Array(16), new Uint8Array(262144)];
  const end = await vm.runInContext('sendStreamingChunk(parts,0,false)', context);
  assert.equal(end, 262160);
  assert.equal(calls.length, 1);
  assert.match(calls[0], /total=0&offset=0&final=0&stream=1/);
  loseResponse = true;
  context.offset = end;
  const final = await vm.runInContext('sendStreamingChunk(parts,offset,true)', context);
  assert.equal(final, 524320);
  assert.equal(calls.length, 2, 'lost response must not append the batch twice');
  fail = true; saved = 0; finalState = false; calls = [];
  await assert.rejects(vm.runInContext('sendStreamingChunk(parts,0,false)', context), /disconnected/);
  assert.equal(calls.length, 3, 'retry budget is bounded');
  // Execute the real converter with 1,200 frames: only a bounded batch may
  // survive between uploads, rather than one Blob containing the whole film.
  const elements = new Map();
  context.$ = id => {
    if (!elements.has(id)) elements.set(id, { value: id === 'fps' ? '20' : '0.75', firstElementChild: { style: {} } });
    return elements.get(id);
  };
  context.file = {}; context.busy = false;
  const video = { duration: 60, videoWidth: 128, videoHeight: 128 };
  context.prepareSource = async () => video;
  context.seek = async () => {};
  context.jpeg = async () => new Uint8Array(4096);
  context.crc32 = () => 0;
  context.document = { createElement: () => ({ getContext: () => ({fillRect(){},drawImage(){}}) }) };
  const messages = [];
  context.status = (id, text, bad) => messages.push({id, text, bad});
  context.startPoll = () => {};
  fail = false; calls = []; saved = 0; largestBatch = 0;
  vm.runInContext(source.slice(source.indexOf('async function convertAndStore'), source.indexOf('async function api(')), context);
  await vm.runInContext('convertAndStore()', context);
  assert.equal(saved, 16 + 1200 * (4096 + 8));
  assert(calls.length > 10);
  assert(largestBatch <= 256 * 1024 + 32776);
  assert(!messages.some(m => m.bad));
  video.duration = 21601; calls = []; messages.length = 0;
  await vm.runInContext('convertAndStore()', context);
  assert.equal(calls.length, 0, 'overlong input must not be silently truncated');
  assert(messages.some(m => m.bad));
  console.log('v5 sync upload response-loss runtime tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
