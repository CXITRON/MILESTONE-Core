#pragma once
#include "V5Artwork.h"
#include <WebServer.h>
#include <functional>

class V5ArtworkPortal {
public:
  void begin(WebServer &web, V5Artwork &cache, std::function<bool()> authorize,
             std::function<String()> csrf) {
    server = &web;
    art = &cache;
    auth = authorize;
    token = csrf;
    web.on("/artwork", HTTP_GET, [this] {
      String page = F(
          "<!doctype html><html lang='ko'><meta charset='utf-8'><meta "
          "name='viewport' content='width=device-width'><title>앨범아트 "
          "관리</title><style>body{font:16px "
          "sans-serif;max-width:720px;margin:auto;padding:20px}button,input{"
          "font:inherit;margin:6px;padding:8px}article{border-bottom:1px solid "
          "#aaa;padding:12px}canvas{image-rendering:pixelated}</"
          "style><h1>앨범아트 관리</h1><p><a "
          "href='/'>설정으로</a></p><p>AUTO는 LRU 정리 대상이며 CUSTOM은 "
          "보호됩니다. JPG/PNG는 이 브라우저에서 크기를 변환한 후 기기로 "
          "전송합니다.</p><input id='csrf' type='hidden' value='");
      page += token() +
              "'><input id='query' placeholder='앨범·아티스트·곡 검색'><button "
              "id='search'>검색</button><p id='status'></p><div "
              "id='items'></div><button id='more'>더 읽기</button>";
      page += R"HTML(<script>
const csrf=document.querySelector('#csrf').value,items=document.querySelector('#items'),status=document.querySelector('#status');let cursor=0;
const check=async r=>{if(!r.ok)throw Error(await r.text());return r;};
async function action(key,op){if(op==='delete'&&!confirm('이미지를 삭제하고 MISSING 상태로 유지할까요?'))return;try{await check(await fetch('/artwork/action',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:csrf,key,op})}));status.textContent=op==='refresh'?'다운로드 요청 접수':op==='delete'?'이미지 삭제 완료':'완료';if(op==='refresh'){for(let i=0;i<65;i++){await new Promise(resolve=>setTimeout(resolve,1000));const state=await(await check(await fetch('/artwork/request-status'))).json();if(state.key!==key)break;status.textContent=state.result;if(!state.pending)break;}}}catch(e){status.textContent=e.message;}}
function crc32(bytes){let c=0xffffffff;for(const b of bytes){c^=b;for(let n=0;n<8;n++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
async function upload(key,file){
 if(!file||!['image/jpeg','image/png'].includes(file.type))throw Error('JPG/PNG를 선택하세요');
 if(file.size>10*1024*1024)throw Error('입력 이미지는 10MB 이하만 지원합니다');
 const url=URL.createObjectURL(file),img=new Image();try{img.src=url;await img.decode();if(img.width*img.height>32000000)throw Error('이미지 해상도가 너무 큽니다');
 const packet=new Uint8Array(22704),view=new DataView(packet.buffer);packet.set([77,65,67,49,60,60,88,88]);view.setUint16(8,7200);view.setUint16(10,15488);let offset=16;
 for(const size of [60,88]){const c=document.createElement('canvas');c.width=c.height=size;const ctx=c.getContext('2d');ctx.fillStyle='#000';ctx.fillRect(0,0,size,size);const scale=Math.min(size/img.width,size/img.height),w=img.width*scale,h=img.height*scale;ctx.drawImage(img,(size-w)/2,(size-h)/2,w,h);const data=ctx.getImageData(0,0,size,size).data;for(let i=0;i<data.length;i+=4){view.setUint16(offset,((data[i]&248)<<8)|((data[i+1]&252)<<3)|(data[i+2]>>3));offset+=2;}}
 view.setUint32(12,crc32(packet.subarray(16)));const form=new FormData();form.append('image',new Blob([packet]),'artwork.mac');await check(await fetch('/artwork/upload',{method:'POST',headers:{'X-CSRF-Token':csrf,'X-Artwork-Key':key},body:form}));
 }finally{URL.revokeObjectURL(url);}
}
async function preview(key,canvas){const bytes=new Uint8Array(await (await check(await fetch('/artwork/file?key='+key))).arrayBuffer());if(bytes.length!==22704)throw Error('Invalid image');const ctx=canvas.getContext('2d'),image=ctx.createImageData(88,88),v=new DataView(bytes.buffer);for(let i=0;i<88*88;i++){let c=v.getUint16(7216+i*2);image.data[i*4]=((c>>11)&31)*255/31;image.data[i*4+1]=((c>>5)&63)*255/63;image.data[i*4+2]=(c&31)*255/31;image.data[i*4+3]=255;}ctx.putImageData(image,0,0);}
async function load(){try{if(cursor<0)return;const response=await check(await fetch('/artwork/list?cursor='+cursor+'&q='+encodeURIComponent(document.querySelector('#query').value))),data=await response.json();cursor=data.next;status.textContent=data.bytes+' bytes / '+data.count+' images';for(const item of data.items){const row=document.createElement('article'),title=document.createElement('p');title.textContent=item.text+' ['+item.state+']';row.append(title);const canvas=document.createElement('canvas');canvas.width=canvas.height=88;row.append(canvas);const show=document.createElement('button');show.textContent='미리보기';show.onclick=()=>preview(item.key,canvas).catch(e=>status.textContent=e.message);row.append(show);for(const [op,label] of [['pin','고정'],['unpin','고정 해제'],['block','자동 요청 차단'],['unblock','차단 해제'],['delete','이미지만 삭제'],['refresh','자동 이미지 삭제·재요청']]){const b=document.createElement('button');b.textContent=label;b.onclick=()=>action(item.key,op);row.append(b);}const input=document.createElement('input');input.type='file';input.accept='image/jpeg,image/png';input.onchange=()=>upload(item.key,input.files[0]).then(()=>status.textContent='사용자 이미지 저장 완료').catch(e=>status.textContent=e.message);row.append(input);items.append(row);}document.querySelector('#more').disabled=cursor<0;}catch(e){status.textContent=e.message;}}
document.querySelector('#search').onclick=()=>{cursor=0;items.replaceChildren();load();};document.querySelector('#more').onclick=load;load();
</script></html>)HTML";
      server->sendHeader("Cache-Control", "no-store");
      server->send(200, "text/html; charset=utf-8", page);
    });
    web.on("/artwork/list", HTTP_GET, [this] {
      token();
      list();
    });
    web.on("/artwork/request-status", HTTP_GET, [this] {
      token();
      server->send(200, "application/json",
                   "{\"pending\":" + String(art->manual ? "true" : "false") +
                       ",\"key\":" + json(art->lastRequestKey) +
                       ",\"result\":" + json(art->lastRequestResult) + "}");
    });
    web.on("/artwork/file", HTTP_GET, [this] {
      token();
      String key = server->arg("key");
      if (!V5Artwork::validKey(key)) {
        server->send(400);
        return;
      }
      File f = SD.open(base(key) + ".mac", FILE_READ);
      if (!f)
        f = SD.open(base(key) + ".bak", FILE_READ);
      if (!f || f.size() != MilestoneV5::kArtworkBytes) {
        server->send(404);
        return;
      }
      server->sendHeader("Cache-Control", "no-store");
      server->streamFile(f, "application/octet-stream");
      f.close();
    });
    web.on("/artwork/action", HTTP_POST, [this] { action(); });
    web.on(
        "/artwork/upload", HTTP_POST,
        [this] {
          server->send(uploadOk ? 200 : 400, "text/plain",
                       uploadOk ? "저장 완료" : uploadError);
        },
        [this] { upload(); });
  }
  void close() {
    uploadFile.close();
    directory.close();
  }

private:
  WebServer *server = nullptr;
  V5Artwork *art = nullptr;
  std::function<bool()> auth;
  std::function<String()> token;
  File directory, uploadFile;
  uint32_t cursor = 0, uploadBytes = 0;
  String uploadKey, uploadError = "업로드 없음";
  bool uploadOk = false, uploadActive = false;
  static String base(const String &key) {
    return String("/now/art-cache/") + key;
  }
  static String json(const String &text) {
    String result = "\"";
    for (unsigned i = 0; i < text.length(); ++i) {
      unsigned char c = text[i];
      if (c == '"' || c == '\\') {
        result += '\\';
        result += char(c);
      } else if (c < 32) {
        char escaped[7];
        snprintf(escaped, sizeof(escaped), "\\u%04x", c);
        result += escaped;
      } else
        result += char(c);
    }
    return result + '"';
  }
  void list() {
    String requested = server->arg("cursor");
    bool valid = requested.length() > 0 && requested.length() <= 7;
    for (unsigned i = 0; i < requested.length(); ++i)
      valid = valid && isDigit(requested[i]);
    if (!valid) {
      server->send(400);
      return;
    }
    uint32_t position = requested.toInt();
    if (position == 0) {
      directory.close();
      directory = SD.open("/now/art-cache");
      cursor = 0;
    }
    if (position != cursor) {
      server->send(409, "text/plain", "목록을 처음부터 다시 검색하세요");
      return;
    }
    String query = server->arg("q");
    query.toLowerCase();
    String entries = "";
    bool ended = false;
    for (unsigned i = 0; i < 64; ++i) {
      File f = directory ? directory.openNextFile() : File();
      if (!f) {
        ended = true;
        directory.close();
        break;
      }
      ++cursor;
      String name = f.name();
      bool isFile = !f.isDirectory(), metadata = name.endsWith(".meta");
      f.close();
      if (!isFile || (!name.endsWith(".mac") && !metadata))
        continue;
      String key = name.substring(0, name.length() - (metadata ? 5 : 4));
      if (!V5Artwork::validKey(key) ||
          (metadata && SD.exists(base(key) + ".mac")))
        continue;
      String text = key;
      File meta = SD.open(base(key) + ".meta", FILE_READ);
      uint8_t bytes[444];
      MilestoneV5::NowMetadata m{};
      if (meta && meta.size() <= sizeof(bytes)) {
        size_t n = meta.size();
        if (meta.read(bytes, n) == n && MilestoneV5::decodeNow(bytes, n, m))
          text = String(m.title) + " / " + m.artist + " / " + m.album;
      }
      meta.close();
      String lower = text;
      lower.toLowerCase();
      if (query.length() && lower.indexOf(query) < 0)
        continue;
      String state = SD.exists(base(key) + ".blocked")  ? "BLOCKED"
                     : SD.exists(base(key) + ".custom") ? "CUSTOM"
                     : metadata                         ? "MISSING"
                                                        : "AUTO";
      if (entries.length())
        entries += ',';
      entries += "{\"key\":" + json(key) + ",\"text\":" + json(text) +
                 ",\"state\":" + json(state) + "}";
    }
    server->send(200, "application/json",
                 "{\"next\":" + (ended ? String(-1) : String(cursor)) +
                     ",\"bytes\":" + String(art->cacheBytes) +
                     ",\"limit\":2147483648,\"count\":" + art->cacheCount +
                     ",\"storage_status\":" + json(art->storageStatus) +
                     ",\"save_failures\":" + String(art->saveFailures) +
                     ",\"items\":[" + entries + "]}");
  }
  bool marker(const String &path) {
    File f = SD.open(path, FILE_WRITE);
    bool ok = f && f.write(uint8_t(1)) == 1;
    f.flush();
    f.close();
    return ok;
  }
  void action() {
    if (!auth())
      return;
    String key = server->arg("key"), op = server->arg("op");
    if (!V5Artwork::validKey(key)) {
      server->send(400);
      return;
    }
    String path = base(key);
    bool ok = false;
    if (op == "pin")
      ok = SD.exists(path + ".mac") && marker(path + ".custom");
    else if (op == "unpin")
      ok = !SD.exists(path + ".custom") || SD.remove(path + ".custom");
    else if (op == "block")
      ok = marker(path + ".blocked");
    else if (op == "unblock")
      ok = !SD.exists(path + ".blocked") || SD.remove(path + ".blocked");
    else if (op == "delete") {
      if (SD.exists(path + ".bak")) {
        server->send(409, "text/plain",
                     "복구용 백업을 먼저 복원하거나 명시적으로 정리해야 합니다");
        return;
      }
      ok = (!SD.exists(path + ".mac") || SD.remove(path + ".mac")) &&
           (!SD.exists(path + ".custom") || SD.remove(path + ".custom")) &&
           (!SD.exists(path + ".blocked") || SD.remove(path + ".blocked"));
    } else if (op == "refresh") {
      if (SD.exists(path + ".custom") || SD.exists(path + ".bak")) {
        server->send(
            409, "text/plain",
            "고정을 먼저 해제하세요. 복구용 백업은 자동 삭제하지 않습니다");
        return;
      }
      if (!art->queueRefresh(key)) {
        server->send(409, "text/plain",
                     "앨범아트 작업 중이거나 메타데이터가 없습니다");
        return;
      }
      ok = (!SD.exists(path + ".blocked") || SD.remove(path + ".blocked")) &&
           (!SD.exists(path + ".mac") || SD.remove(path + ".mac"));
      if (!ok)
        art->invalidate();
    }
    if (ok) {
      art->requestRecount();
      if (art->key == key && op != "refresh")
        art->invalidate();
    }
    server->send(ok ? 200 : 400, "text/plain",
                 ok ? "완료" : "작업에 실패했습니다");
  }
  void upload() {
    HTTPUpload &u = server->upload();
    if (u.status == UPLOAD_FILE_START) {
      uploadFile.close();
      uploadOk = false;
      uploadActive = false;
      uploadBytes = 0;
      uploadError = "올바르지 않은 업로드입니다";
      uploadKey = server->header("X-Artwork-Key");
      if (server->header("X-CSRF-Token") != token() || token().isEmpty() ||
          !V5Artwork::validKey(uploadKey))
        return;
      if (!art->cacheKnown ||
          art->cacheBytes + MilestoneV5::kArtworkBytes >
              2ULL * 1024 * 1024 * 1024 ||
          SD.totalBytes() < SD.usedBytes() + 1024ULL * 1024 * 1024) {
        uploadError = "Cache capacity unavailable";
        return;
      }
      String temp = base(uploadKey) + ".upload.tmp";
      if (SD.exists(temp) && !SD.remove(temp))
        return;
      uploadFile = SD.open(temp, FILE_WRITE);
      uploadActive = bool(uploadFile);
    } else if (u.status == UPLOAD_FILE_WRITE && uploadActive) {
      if (u.currentSize > MilestoneV5::kArtworkBytes - uploadBytes ||
          uploadFile.write(u.buf, u.currentSize) != u.currentSize) {
        uploadFile.close();
        uploadActive = false;
        return;
      }
      uploadBytes += u.currentSize;
    } else if (u.status == UPLOAD_FILE_END && uploadActive) {
      uploadActive = false;
      uploadFile.flush();
      uploadFile.close();
      if (uploadBytes != MilestoneV5::kArtworkBytes ||
          u.totalSize != uploadBytes)
        return;
      String path = base(uploadKey), temp = path + ".upload.tmp";
      uint8_t *packet = static_cast<uint8_t *>(heap_caps_malloc(
          MilestoneV5::kArtworkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!packet)
        return;
      File f = SD.open(temp, FILE_READ);
      bool valid = f && f.size() == uploadBytes &&
                   f.read(packet, uploadBytes) == uploadBytes &&
                   MilestoneV5::validArtwork(packet, uploadBytes);
      f.close();
      free(packet);
      if (!valid || SD.exists(path + ".bak") || !marker(path + ".custom"))
        return;
      bool hadImage = SD.exists(path + ".mac");
      if (hadImage && !SD.rename(path + ".mac", path + ".bak"))
        return;
      if (!SD.rename(temp, path + ".mac")) {
        if (hadImage)
          SD.rename(path + ".bak", path + ".mac");
        return;
      }
      uploadOk = true;
      if (hadImage)
        SD.remove(path + ".bak");
      art->requestRecount();
      if (art->key == uploadKey)
        art->invalidate();
    } else if (u.status == UPLOAD_FILE_ABORTED) {
      uploadFile.close();
      uploadActive = false;
    }
  }
};
