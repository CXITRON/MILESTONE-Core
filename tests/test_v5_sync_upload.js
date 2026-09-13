// Execute the actual browser chunk sender, including response-loss recovery.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('v5/MilestoneV5Main/V5SyncPage.h', 'utf8');
const sender = source.slice(source.indexOf('async function sendStreamingChunk'),
                            source.indexOf('async function convertAndStore'));
let calls = [], saved = 0, loseResponse = false, fail = false, finalState = false;
let largestBatch = 0;
let partialBytes = 0, skipFinalize = false, statusOverride = null;
const context = vm.createContext({
  Blob, Error, Promise,
  setTimeout: callback => { callback(); },
  boundedFetch: async (path, options) => {
    calls.push({path,offset:Number(options.headers['X-Sync-Offset']),bytes:options.body.size});
    if(!fail) assert.equal(Number(options.headers['X-Sync-Offset']), saved);
    largestBatch = Math.max(largestBatch, options.body.size);
    if (fail) throw new Error('disconnected');
    if (partialBytes) { saved += partialBytes; partialBytes = 0; throw new Error('partial request'); }
    saved += options.body.size;
    if (skipFinalize) { skipFinalize = false; throw new Error('finalize not reached'); }
    finalState = options.headers['X-Sync-Final']==='1';
    if (loseResponse) { loseResponse = false; throw new Error('response lost'); }
    return { ok: true, json: async () => ({ ok: true, written_bytes: saved }) };
  },
  api: async () => statusOverride || ({ state: finalState ? 'indexing' : 'uploading', written_bytes: saved }),
});
vm.runInContext(sender, context);
(async () => {
  context.parts = [new Uint8Array(16), new Uint8Array(262144)];
  const end = await vm.runInContext('sendStreamingChunk(parts,0,false)', context);
  assert.equal(end, 262160);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].path, '/api/sync/data');
  assert.equal(calls[0].offset,0);
  loseResponse = true;
  context.offset = end;
  const final = await vm.runInContext('sendStreamingChunk(parts,offset,true)', context);
  assert.equal(final, 524320);
  assert.equal(calls.length, 2, 'lost response must not append the batch twice');
  fail = true; saved = 0; finalState = false; calls = [];
  await assert.rejects(vm.runInContext('sendStreamingChunk(parts,0,false)', context), /disconnected/);
  assert.equal(calls.length, 3, 'retry budget is bounded');
  fail = false; saved = 0; calls = []; partialBytes = 12345;
  assert.equal(await vm.runInContext('sendStreamingChunk(parts,0,false)', context), 262160);
  assert.equal(calls.length, 2);
  assert.equal(calls[1].offset, 12345);
  assert.equal(calls[1].bytes, 262160 - 12345, 'resume sends only the missing suffix');
  saved = 0; calls = []; skipFinalize = true; finalState = false;
  assert.equal(await vm.runInContext('sendStreamingChunk(parts,0,true)', context), 262160);
  assert.equal(calls.length, 2);
  assert.equal(calls[1].bytes, 0, 'all bytes saved but finalization missing: finish without duplicate data');
  fail = true; calls = [];
  statusOverride = { state: 'uploading', written_bytes: 900000 };
  await assert.rejects(vm.runInContext('sendStreamingChunk(parts,0,false)', context), /저장 위치가 변경/);
  assert.equal(calls.length, 1, 'do not overwrite an unrelated upload session');
  statusOverride = null;
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
  calls = []; messages.length = 0;
  let prepared = false;
  context.prepareSource = async () => { prepared = true; return video; };
  statusOverride = { upload_allowed: false, upload_error: '펌웨어 설치 중' };
  await vm.runInContext('convertAndStore()', context);
  assert(!prepared && calls.length === 0, 'reject before decoding/converting the first batch');
  assert(messages.some(m => m.bad && m.text === '펌웨어 설치 중'));
  statusOverride = null;
  video.duration = 21601; calls = []; messages.length = 0;
  await vm.runInContext('convertAndStore()', context);
  assert.equal(calls.length, 0, 'overlong input must not be silently truncated');
  assert(messages.some(m => m.bad));
  console.log('v5 sync upload response-loss runtime tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
