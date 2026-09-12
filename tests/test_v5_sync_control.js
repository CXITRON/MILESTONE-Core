// Run the actual page sender against delayed/lost ACKs, not a reimplementation.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const page = fs.readFileSync('v5/MilestoneV5Main/V5SyncPage.h', 'utf8');
const sender = page.slice(page.indexOf('async function sendSync('), page.indexOf('function beginSyncLoop('));
const tick = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  const video = {currentTime: 1, paused: false, ended: false, seeking: false,
                 pause() { this.paused = true; }};
  let pending = [], http = [];
  const c = vm.createContext({
    $: () => video, Date, Math, Error, Promise,
    ws: {readyState: 1}, WebSocket: {OPEN: 1}, wsRetryAt: 0,
    controlSending: false, queuedSync: null, mediaWaiting: false,
    sessionReady: true, syncTimer: 0, clearInterval() {}, status() {},
    sendWsControl: (position, run) => new Promise((resolve, reject) => pending.push({position, run, resolve, reject})),
    api: async (path, body) => { http.push({...body}); },
  });
  vm.runInContext(sender, c);
  const first = vm.runInContext('sendSync(true)', c);
  video.currentTime = 1.25;
  vm.runInContext('sendSync(true)', c);
  video.currentTime = 1.5;
  vm.runInContext('sendSync(true)', c);
  assert.equal(pending.length, 1, 'periodic controls must not overlap a pending ACK');
  video.currentTime = 1.8;
  pending[0].reject(new Error('ACK lost'));
  await first;
  assert.equal(http.length, 1);
  assert.equal(http[0].position, 1800, 'fallback must sample the current position, not rewind 800ms');
  assert.equal(video.paused, false, 'a working HTTP fallback must keep playing');

  c.wsRetryAt = 0; pending = []; http = [];
  const second = vm.runInContext('sendSync(true)', c);
  video.currentTime = 4; video.paused = true;
  vm.runInContext('sendSync(false)', c);
  pending[0].reject(new Error('ACK lost'));
  await second;
  assert.equal(http[0].running, 0, 'late play fallback cannot override a newer pause');
  assert.equal(http[0].position, 4000);

  // One slow HTTP request must also be bounded to one in-flight request.
  c.ws = null; http = []; let finishHttp;
  c.api = (path, body) => { http.push({...body}); return new Promise(resolve => {finishHttp = resolve;}); };
  video.paused = false;
  const third = vm.runInContext('sendSync(true)', c);
  video.currentTime = 10;
  vm.runInContext('sendSync(true)', c);
  video.currentTime = 11;
  vm.runInContext('sendSync(true)', c);
  assert.equal(http.length, 1);
  finishHttp(); await tick();
  assert.equal(http.length, 2);
  assert.equal(http[1].position, 11000, 'only the newest queued timeline is sent');
  finishHttp(); await third;

  http = []; c.api = async (path, body) => {http.push({...body});};
  video.seeking = true;
  await vm.runInContext('sendSync(true)', c);
  assert.equal(http[0].running, 0, 'a timer during seek must not start playback');
  video.seeking = false; c.mediaWaiting = true;
  await vm.runInContext('sendSync(true)', c);
  assert.equal(http[1].running, 0, 'waiting for browser data must hold the TFT timeline');
  console.log('v5 sync delayed-control runtime tests passed');
})().catch(error => {console.error(error); process.exitCode = 1;});
