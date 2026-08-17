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
    this.dataset = {};
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
let mediaItems = [];
let deleteMode = 'success';
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
    createTextNode(text) { const e=new MockElement('text'); e.textContent=String(text); return e; },
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
  fetch: async url => {
    const textUrl=String(url);
    if(textUrl.includes('/api/media/delete')){
      const id=Number(new URLSearchParams(textUrl.split('?')[1]||'').get('id'));
      if(deleteMode==='failure')return {ok:false,status:500,statusText:'ERR',async text(){return JSON.stringify({error:'미디어를 삭제하지 못했습니다.',stage:'catalog-write-failed:file-remove-failed'})}};
      mediaItems=mediaItems.filter(item=>Number(item.id)!==id);
      return {ok:true,status:200,statusText:'OK',async text(){return JSON.stringify({ok:true,id,stage:'catalog-committed:file-removed'})}};
    }
    return {
      ok: true,
      status: 200,
      statusText: 'OK',
      async text() {
        if (textUrl.includes('/api/config')) return JSON.stringify({saved_networks: []});
        if (textUrl.includes('/api/media/status')) return JSON.stringify({ready: true, media_limit_bytes: 200000, media_used_bytes: mediaItems.reduce((n,x)=>n+x.size,0), item_count:mediaItems.length,max_items:64,psram:true});
        if (textUrl.includes('/api/media/list')) return JSON.stringify({items: mediaItems});
        if (textUrl.includes('/api/diagnostics')) return JSON.stringify({events: []});
        return '{}';
      }
    };
  },
  setInterval(fn, ms) { const id = nextTimer++; timers.set(id, {fn, ms}); return id; },
  clearInterval(id) { timers.delete(id); },
  setTimeout(fn, ms) {
    const id = nextTimer++;
    if (ms === 0) queueMicrotask(fn);
    else timers.set(id, {fn, ms});
    return id;
  },
  clearTimeout(id) { timers.delete(id); },
  confirm() { return true; },
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
  assert.strictEqual(run('mediaPick.active'), false);
  assert.strictEqual(run('mediaPick.phase'), '파일 전달 실패');
  assert.match(get('media_trace').textContent, /provider-no-file/);
  assert.match(get('media_trace').textContent, /현재 단계: 파일 전달 실패/);
  assert.match(get('media_info').textContent, /모바일 데이터 또는 인터넷/);
  assert.match(get('media_info').textContent, /재생·다운로드/);

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택')");
  video.files = [new File([new Uint8Array([4, 5])], 'eventless.mp4', {type: 'video/mp4'})];
  run('pollMediaPick()');
  await settle();
  assert.strictEqual(run('selectedMediaFile.name'), 'eventless.mp4');
  assert.match(get('media_trace').textContent, /poll-files/);

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택'); mediaPick.started=Date.now()-181000; pollMediaPick()");
  assert.strictEqual(run('mediaPick.active'), false);
  assert.match(get('media_info').textContent, /180초 동안/);
  assert.strictEqual(run('mediaPick.phase'), '파일 전달 실패');
  assert.match(get('media_trace').textContent, /timeout/);

  run("beginMediaPick($('media_video_file'),'사진 앱에서 동영상 선택'); convertMedia()");
  assert.match(get('media_info').textContent, /파일 선택 중/);


  mediaItems=[{id:1234567890,name:'삭제시험',size:4096,frames:1,duration_ms:1000,display_seconds:8,animated:false,loop:false,enabled:true}];
  await run("deleteMedia(1234567890,'삭제시험')");
  await settle();
  assert.strictEqual(mediaItems.length,0);
  assert.match(get('media_action_status').textContent,/삭제 완료/);
  assert.match(get('media_action_status').textContent,/catalog-committed:file-removed/);

  mediaItems=[{id:777,name:'실패시험',size:4096,frames:1,duration_ms:1000,display_seconds:8,animated:false,loop:false,enabled:true}];
  deleteMode='failure';
  await run("deleteMedia(777,'실패시험')");
  await settle();
  assert.strictEqual(mediaItems.length,1);
  assert.match(get('media_action_status').textContent,/삭제 실패/);
  assert.match(get('media_action_status').textContent,/HTTP 500/);
  assert.match(get('media_action_status').textContent,/catalog-write-failed:file-remove-failed/);
  deleteMode='success';

  console.log('Media picker and delete state-machine test passed');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
