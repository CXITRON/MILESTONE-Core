'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const source = fs.readFileSync(process.argv[2], 'utf8');
const match = source.match(/<\/main><script>([\s\S]*?)<\/script><\/body><\/html>/);
assert(match, 'portal script was not found');

class MockElement {
  constructor(id = '') {
    this.id = id;
    this.listeners = new Map();
    this.files = [];
    this.style = {};
    this.classList = {add() {}, remove() {}};
    this.className = '';
    this.textContent = '';
    this.hidden = false;
    this.disabled = false;
    this.checked = false;
    this.max = '255';
    this.min = '0';
    this.maxLength = 0;
    this.options = [];
    this.selectedIndex = 0;
    this._value = '';
    this.attributes = new Map();
  }
  get value() { return this._value; }
  set value(value) {
    this._value = String(value);
    if (value === '' && this.id.startsWith('media_') && this.id.endsWith('_file')) this.files = [];
  }
  addEventListener(type, handler) {
    const handlers = this.listeners.get(type) || [];
    handlers.push(handler);
    this.listeners.set(type, handlers);
  }
  removeEventListener() {}
  dispatch(type) {
    for (const handler of this.listeners.get(type) || []) handler({type, currentTarget: this});
  }
  getAttribute(name) { return this.attributes.get(name) || ''; }
  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  append() {}
  appendChild() {}
  replaceChildren() {}
  remove() {}
  select() {}
  querySelector() { return new MockElement(); }
  getContext() {
    return {
      fillRect() {}, save() {}, restore() {}, setTransform() {}, drawImage() {},
      putImageData() {}, createImageData() { return {data: new Uint8ClampedArray(65536)}; },
      getImageData() { return {data: new Uint8ClampedArray(65536)}; }
    };
  }
}

const elements = new Map();
for (const id of source.matchAll(/id="([^"]+)"/g)) elements.set(id[1], new MockElement(id[1]));
const get = id => {
  if (!elements.has(id)) elements.set(id, new MockElement(id));
  return elements.get(id);
};
get('media_image_file').setAttribute('aria-label', '사진 또는 GIF 선택');
get('media_video_file').setAttribute('aria-label', '사진 앱에서 동영상 선택');
get('media_any_file').setAttribute('aria-label', '파일 앱에서 동영상 선택');

let nextTimer = 1;
const timers = new Map();
const documentListeners = new Map();
const windowListeners = new Map();
const context = vm.createContext({
  console,
  Blob,
  File,
  TextEncoder,
  URLSearchParams,
  Uint8Array,
  Uint8ClampedArray,
  Float32Array,
  Date,
  Math,
  Promise,
  navigator: {userAgent: 'MILESTONE picker unit test', clipboard: null},
  document: {
    visibilityState: 'visible',
    getElementById: get,
    querySelectorAll() { return []; },
    createElement(tag) { return new MockElement(tag); },
    addEventListener(type, handler) { documentListeners.set(type, handler); },
    execCommand() { return true; },
    body: {appendChild() {}}
  },
  window: {
    isSecureContext: false,
    addEventListener(type, handler) { windowListeners.set(type, handler); }
  },
  Image: class extends MockElement { async decode() {} },
  ImageData: class {},
  URL: {createObjectURL() { return 'blob:test'; }, revokeObjectURL() {}},
  fetch: async url => ({
    ok: true,
    status: 200,
    statusText: 'OK',
    async text() {
      if (String(url).includes('/api/config')) return JSON.stringify({saved_networks: []});
      if (String(url).includes('/api/media/status')) return JSON.stringify({ready: false, media_limit_bytes: 0, media_used_bytes: 0});
      if (String(url).includes('/api/media/list')) return JSON.stringify({items: []});
      if (String(url).includes('/api/diagnostics')) return JSON.stringify({events: []});
      return '{}';
    }
  }),
  setInterval(fn, ms) { const id = nextTimer++; timers.set(id, {fn, ms}); return id; },
  clearInterval(id) { timers.delete(id); },
  setTimeout(fn, ms) {
    const id = nextTimer++;
    if (ms === 0) queueMicrotask(fn);
    else timers.set(id, {fn, ms});
    return id;
  },
  clearTimeout(id) { timers.delete(id); },
  confirm() { return false; },
  prompt() { return ''; }
});

vm.runInContext(match[1], context, {filename: 'PortalPage.inline.js'});
const run = expression => vm.runInContext(expression, context);
const settle = () => new Promise(resolve => setImmediate(resolve));

(async () => {
  const video = get('media_video_file');
  const clip = new File([new Uint8Array([0, 1, 2, 3])], 'camera.MOV', {type: ''});
  video.files = [clip];
  video.dispatch('input');
  await settle();
  assert.strictEqual(run('selectedMediaFile.name'), 'camera.MOV');
  assert.strictEqual(run('selectedMediaKind'), 'video');
  assert.match(get('media_info').textContent, /선택 완료/);
  assert.match(get('media_trace').textContent, /input/);
  assert.match(get('media_trace').textContent, /probe-ok/);

  video.dispatch('change');
  await settle();
  assert.match(get('media_trace').textContent, /file-event-duplicate/);

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택')");
  video.dispatch('cancel');
  assert.strictEqual(run('mediaPick.active'), true);
  assert.match(get('media_trace').textContent, /post-cancel-watch/);
  assert.match(get('media_info').textContent, /후속 파일 전달 감시 중/);
  video.files = [new File([new Uint8Array([9])], 'after-cancel.mp4', {type: 'video/mp4'})];
  run('pollMediaPick()');
  await settle();
  assert.strictEqual(run('selectedMediaFile.name'), 'after-cancel.mp4');

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택')");
  video.files = [new File([new Uint8Array([4, 5])], 'eventless.mp4', {type: 'video/mp4'})];
  run('pollMediaPick()');
  await settle();
  assert.strictEqual(run('selectedMediaFile.name'), 'eventless.mp4');
  assert.match(get('media_trace').textContent, /poll-files/);

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택'); mediaPick.started=Date.now()-181000; pollMediaPick()");
  assert.strictEqual(run('mediaPick.active'), false);
  assert.match(get('media_info').textContent, /180초 동안/);
  assert.match(get('media_trace').textContent, /timeout/);

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택'); convertMedia()");
  assert.match(get('media_info').textContent, /파일 전달 대기 중/);

  console.log('Media picker state-machine test passed');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
