#pragma once

// MILESTONE Setup portal. Kept in flash to preserve RAM.
static const char MILESTONE_PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MILESTONE Setup</title>
<style>
:root{color-scheme:dark;--bg:#0b0f10;--card:#141a1c;--line:#263236;--mint:#57efc4;--text:#edf7f4;--muted:#9aaba6;--bad:#ff6b78}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,"Noto Sans KR",sans-serif}
main{width:min(760px,100%);margin:auto;padding:20px 14px 60px}h1{font-size:24px;margin:4px 0}h2{font-size:17px;margin:0 0 14px}.brand{color:var(--mint);letter-spacing:.08em}.sub{color:var(--muted);margin:5px 0 18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin:12px 0}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.full{grid-column:1/-1}
label{display:block;color:var(--muted);font-size:13px;margin:0 0 5px}input,select,textarea,button{width:100%;border:1px solid var(--line);border-radius:9px;background:#0d1315;color:var(--text);padding:11px;font:inherit}textarea{min-height:74px;resize:vertical}
button{cursor:pointer;background:#1b2828;font-weight:700}button.primary{background:var(--mint);color:#062019;border-color:var(--mint)}button.danger{border-color:#80343d;color:#ff9da5}.row{display:flex;gap:9px;align-items:center}.row>*{flex:1}.check{display:flex;gap:8px;align-items:center;color:var(--text);margin:8px 0}.check input{width:auto}.status{white-space:pre-wrap;background:#0a0e0f;padding:11px;border-radius:9px;color:var(--muted);min-height:42px}.ok{color:var(--mint)}.bad{color:var(--bad)}small{color:var(--muted)}
.range-row{display:grid;grid-template-columns:minmax(0,1fr) 82px;gap:9px;align-items:center}.range-row input[type=number]{text-align:center}.saved-list{display:grid;gap:8px}.saved-entry{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:9px;align-items:center;background:#0d1315;border:1px solid var(--line);border-radius:9px;padding:9px 10px}.saved-entry button{width:auto;padding:7px 10px}.saved-name{overflow-wrap:anywhere}.badge{color:var(--mint);font-size:12px;margin-left:7px}
@media(max-width:560px){.grid{grid-template-columns:1fr}.row{flex-direction:column}.row>*{width:100%}}
</style></head><body><main>
<div class="brand">CYTRON//MILESTONE</div><h1>MILESTONE Setup</h1><p class="sub">MILESTONE D1 로컬 설정 포털</p>

<section class="card"><h2>상태</h2><div id="status" class="status">상태를 불러오는 중…</div><div class="row" style="margin-top:10px"><button onclick="syncTime()">지금 시간 동기화</button><button onclick="exportConfig()">설정 JSON 보기</button></div><textarea id="export" class="full" hidden readonly></textarea></section>

<section class="card"><h2>펌웨어 업데이트</h2><div id="update_status" class="status">업데이트 상태를 불러오는 중…</div><div class="row" style="margin-top:10px"><button onclick="checkUpdate()">지금 업데이트 확인</button><button id="install_update" class="primary" onclick="installUpdate()" disabled>업데이트 설치</button></div><p><small>GitHub의 CXITRON/MILESTONE-Core 최신 정식 Release를 확인합니다. 설치 중에는 전원을 분리하지 마세요.</small></p></section>

<section class="card"><h2>네트워크</h2><div class="grid">
<div class="full row"><select id="scan"><option value="">주변 Wi-Fi 검색 전</option></select><button onclick="scanWifi()">검색</button></div>
<div><label>Wi-Fi SSID</label><input id="ssid" maxlength="32" autocomplete="off"></div>
<div><label>Wi-Fi 비밀번호</label><input id="pass" type="password" maxlength="63" autocomplete="new-password" placeholder="저장된 비밀번호는 표시하지 않음"></div>
<div class="full"><button class="primary" onclick="testWifi()">Wi-Fi 시험 후 확정 저장</button></div>
<div class="full"><label>저장된 Wi-Fi (최근 성공 순, 최대 8개)</label><div id="saved_networks" class="saved-list"><small>저장된 네트워크를 불러오는 중…</small></div></div>
</div></section>

<section class="card"><h2>디데이와 문구</h2><div class="grid">
<div><label>목표 이름 (24자 이내)</label><input id="title" maxlength="24"></div>
<div><label>목표 날짜</label><input id="target" type="date"></div>
<div><label>디데이 계산 주기</label><select id="dday_period"><option value="0">매일 자정</option><option value="60">매분</option><option value="600">10분</option><option value="1800">30분</option><option value="3600">1시간</option></select></div>
<div class="full"><label>문구 (60자 이내)</label><textarea id="message" maxlength="60"></textarea></div>
<div><label>디데이 표기</label><select id="dday_style"><option value="0">D-N</option><option value="1">N일 남음</option></select></div>
<div><label>목표일 이후</label><select id="after_mode"><option value="0">D+N</option><option value="1">디데이 표시 종료</option></select></div>
<div><label>문구 정렬</label><select id="msg_align"><option value="0">가운데</option><option value="1">왼쪽</option></select></div>
<div><label>스크롤 속도(px/s)</label><input id="scroll_speed" type="number" min="5" max="80"></div>
<label class="check full"><input id="msg_scroll" type="checkbox"> 긴 문구 자동 스크롤</label>
</div></section>

<section class="card"><h2>표시 모드</h2><div class="grid">
<div class="full"><label>기본 모드</label><select id="mode"><option value="0">디데이(시간)</option><option value="1">디데이(문구)</option><option value="2">문구 단독</option><option value="3">시간 단독</option><option value="4">문구 + 시간</option><option value="5">종합 화면</option><option value="6">선택 순환</option></select></div>
<div class="full"><label>순환 포함 화면</label>
<label class="check"><input class="cycle" value="0" type="checkbox"> 디데이(시간)</label><label class="check"><input class="cycle" value="1" type="checkbox"> 디데이(문구)</label><label class="check"><input class="cycle" value="2" type="checkbox"> 문구 단독</label><label class="check"><input class="cycle" value="3" type="checkbox"> 시간 단독</label><label class="check"><input class="cycle" value="4" type="checkbox"> 문구 + 시간</label><label class="check"><input class="cycle" value="5" type="checkbox"> 종합 화면</label></div>
<div><label>순환 순서 (0~5, 예: 0,1,2,3,4,5)</label><input id="cycle_order" maxlength="11"></div>
<div><label>자동 전환(초, 0 또는 3~60)</label><input id="cycle_interval" type="number" min="0" max="60"></div>
</div></section>

<section class="card"><h2>시간과 화면</h2><div class="grid">
<div><label>시간 형식</label><select id="hour24"><option value="1">24시간제</option><option value="0">12시간제</option></select></div>
<div><label>초 표시</label><select id="show_seconds"><option value="0">끄기</option><option value="1">켜기</option></select></div>
<label class="check full"><input id="show_temp" type="checkbox"> 상단 상태 기호 옆에 ESP32 칩 온도 표시</label>
<div class="full"><small>칩 내부 온도이므로 주변 실내 온도보다 높게 표시될 수 있습니다. 이 토글은 평상시 표시만 제어하며 과열 경고와 보호 기능은 항상 작동합니다.</small></div>
<div><label>NTP 동기화 주기</label><select id="ntp_period"><option value="3600">1시간</option><option value="10800">3시간</option><option value="21600">6시간</option><option value="43200">12시간</option><option value="86400">24시간</option><option value="0">수동</option></select></div>
<div><label>Wi-Fi 재시도 주기</label><select id="retry_period"><option value="60">1분</option><option value="300">5분</option><option value="900">15분</option><option value="1800">30분</option></select></div>
<label class="check"><input id="boot_sync" type="checkbox"> 부팅 직후 시간 동기화</label><label class="check"><input id="wifi_sleep" type="checkbox"> 동기화 뒤 Wi-Fi 끄기</label><label class="check"><input id="burnin" type="checkbox"> 번인 방지 ±1px 이동</label>
<div><label>일반 밝기 (1~255)</label><div class="range-row"><input id="brightness" type="range" min="1" max="255"><input id="brightness_num" type="number" min="1" max="255" aria-label="일반 밝기 값"></div></div>
<div><label>야간 밝기 (1~255)</label><div class="range-row"><input id="night_level" type="range" min="1" max="255"><input id="night_level_num" type="number" min="1" max="255" aria-label="야간 밝기 값"></div></div>
<label class="check full"><input id="led_enabled" type="checkbox"> RGB 상태 LED 계속 표시</label>
<div><label>상태 LED 밝기 (1~64)</label><div class="range-row"><input id="led_brightness" type="range" min="1" max="64"><input id="led_brightness_num" type="number" min="1" max="64" aria-label="상태 LED 밝기 값"></div></div>
<div><label>야간 LED 밝기 (1~32)</label><div class="range-row"><input id="led_night_level" type="range" min="1" max="32"><input id="led_night_level_num" type="number" min="1" max="32" aria-label="야간 LED 밝기 값"></div></div>
<div><label>야간 시작</label><input id="night_start" type="time"></div><div><label>야간 종료</label><input id="night_end" type="time"></div>
<div><label>정지 화면 자동 끄기(분, 0=사용 안 함)</label><input id="screen_off" type="number" min="0" max="1440"></div>
</div></section>

<section class="card"><button class="primary" onclick="saveConfig()">화면·시간 설정 저장</button><p><small>Wi-Fi 비밀번호는 조회되지 않으며, Wi-Fi 시험 성공 시에만 확정 저장됩니다.</small></p></section>
<section class="card"><h2>시스템</h2><div class="grid"><div><button onclick="resetSettings()">표시 설정 기본값 복원</button><p><small>저장된 Wi-Fi는 유지하고 화면·시간·LED 설정만 초기화합니다.</small></p></div><div><button class="danger" onclick="factoryReset()">공장 초기화</button><p><small>저장된 Wi-Fi를 포함한 모든 데이터를 삭제하고 재부팅합니다.</small></p></div></div></section>
</main><script>
let wifiPolling=false;
const $=id=>document.getElementById(id); const val=id=>$(id).value;
function form(obj){return new URLSearchParams(obj)}
function setStatus(s,cls=''){const e=$('status');e.textContent=s;e.className='status '+cls}
async function api(url,opt={}){const r=await fetch(url,opt);const t=await r.text();let d;try{d=JSON.parse(t)}catch{d={ok:false,error:t||r.statusText}}if(!r.ok)throw new Error(d.error||('HTTP '+r.status));return d}
function minsToTime(n){n=Number(n)||0;return String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0')}
function timeToMins(s){const a=s.split(':').map(Number);return (a[0]||0)*60+(a[1]||0)}
const rangePairs=[['brightness','brightness_num'],['night_level','night_level_num'],['led_brightness','led_brightness_num'],['led_night_level','led_night_level_num']];
function bindRangePair(rangeId,numberId){const r=$(rangeId),n=$(numberId);const clamp=v=>Math.min(Number(r.max),Math.max(Number(r.min),Number(v)||Number(r.min)));r.addEventListener('input',()=>n.value=r.value);n.addEventListener('input',()=>{n.value=clamp(n.value);r.value=n.value});n.addEventListener('change',()=>{n.value=clamp(n.value);r.value=n.value})}
function syncRangePairs(){rangePairs.forEach(([r,n])=>$(n).value=$(r).value)}
function renderSavedNetworks(networks=[]){const box=$('saved_networks');box.replaceChildren();if(!networks.length){const s=document.createElement('small');s.textContent='저장된 Wi-Fi가 없습니다.';box.appendChild(s);return}networks.forEach(n=>{const row=document.createElement('div');row.className='saved-entry';const name=document.createElement('div');name.className='saved-name';name.textContent=n.ssid;if(n.preferred){const b=document.createElement('span');b.className='badge';b.textContent='최근 성공';name.appendChild(b)}const del=document.createElement('button');del.type='button';del.className='danger';del.textContent='삭제';del.onclick=()=>deleteSavedWifi(n.ssid);row.append(name,del);box.appendChild(row)})}
async function load(){try{const s=await api('/api/status');renderStatus(s);const c=await api('/api/config');for(const k of ['title','target','message','mode','cycle_order','cycle_interval','dday_style','after_mode','msg_align','scroll_speed','hour24','show_seconds','ntp_period','dday_period','retry_period','brightness','night_level','led_brightness','led_night_level','screen_off'])if($(k))$(k).value=c[k];syncRangePairs();$('ssid').value=c.wifi_ssid||'';$('msg_scroll').checked=!!c.msg_scroll;$('show_temp').checked=!!c.show_temp;$('boot_sync').checked=!!c.boot_sync;$('wifi_sleep').checked=!!c.wifi_sleep;$('burnin').checked=!!c.burnin;$('led_enabled').checked=!!c.led_enabled;$('night_start').value=minsToTime(c.night_start);$('night_end').value=minsToTime(c.night_end);document.querySelectorAll('.cycle').forEach(x=>x.checked=(c.cycle_mask&(1<<Number(x.value)))!==0);renderSavedNetworks(c.saved_networks||[])}catch(e){setStatus(e.message,'bad')}}
function renderUpdate(s){const latest=s.latest_firmware||'-';const lines=[`현재 버전: ${s.firmware}`,`최신 확인 버전: ${latest}`,`상태: ${s.update_state||'idle'}`,`마지막 확인: ${s.last_update_check||'-'}`];if(s.update_state==='downloading')lines.push(`다운로드: ${s.update_progress||0}%`);if(s.update_error)lines.push(`오류: ${s.update_error}`);$('update_status').textContent=lines.join('\n');$('update_status').className='status '+(s.update_available?'ok':s.update_error?'bad':'');$('install_update').disabled=!s.update_available}
function renderStatus(s){setStatus(`펌웨어: ${s.firmware}\n상태: ${s.state}\nWi-Fi: ${s.wifi}\nIP: ${s.ip||'-'}\n시간: ${s.time_valid?'동기화됨':'미확정'}\n마지막 동기화: ${s.last_sync||'-'}${s.wifi_test&&s.wifi_test!=='idle'?'\nWi-Fi 시험: '+s.wifi_test:''}`,s.time_valid?'ok':'');renderUpdate(s)}
async function refresh(){try{const s=await api('/api/status');renderStatus(s);if(wifiPolling){if(s.wifi_test==='success'){wifiPolling=false;setStatus('Wi-Fi 및 시간 동기화 성공. 설정을 확정 저장했습니다.','ok')}else if(s.wifi_test==='failed'){wifiPolling=false;setStatus('Wi-Fi 시험 실패: '+(s.wifi_error||'연결 또는 시간 동기화 실패'),'bad')}else setTimeout(refresh,1000)}}catch(e){setStatus(e.message,'bad')}}
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function scanWifi(){setStatus('주변 2.4GHz Wi-Fi 검색 중…');try{let d;for(let i=0;i<40;i++){d=await api('/api/wifi/scan');if(d.state!=='scanning')break;await wait(300)}if(!d||!Array.isArray(d.networks))throw new Error('Wi-Fi 검색 시간이 초과되었습니다.');const s=$('scan');s.innerHTML='<option value="">검색 결과 선택</option>';d.networks.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.ssid} (${n.rssi} dBm)${n.open?' [열림]':''}`;s.appendChild(o)});s.onchange=()=>{if(s.value)$('ssid').value=s.value};setStatus(`${d.networks.length}개 네트워크 검색 완료`,'ok')}catch(e){setStatus(e.message,'bad')}}
async function testWifi(){if(!val('ssid'))return setStatus('SSID를 입력하세요.','bad');setStatus('AP를 유지한 채 Wi-Fi와 NTP를 시험합니다…');try{await api('/api/wifi/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({ssid:val('ssid'),pass:val('pass')})});wifiPolling=true;setTimeout(refresh,700)}catch(e){setStatus(e.message,'bad')}}
async function deleteSavedWifi(ssid){if(!confirm(`저장된 Wi-Fi “${ssid}”를 삭제하시겠습니까?`))return;try{await api('/api/wifi/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({ssid})});$('pass').value='';await load();setStatus('저장된 Wi-Fi를 삭제했습니다.','ok')}catch(e){setStatus(e.message,'bad')}}
function configBody(){let mask=0;document.querySelectorAll('.cycle:checked').forEach(x=>mask|=1<<Number(x.value));return form({title:val('title'),target:val('target'),message:val('message'),mode:val('mode'),cycle_mask:mask,cycle_order:val('cycle_order'),cycle_interval:val('cycle_interval'),dday_style:val('dday_style'),after_mode:val('after_mode'),msg_align:val('msg_align'),msg_scroll:$('msg_scroll').checked?1:0,scroll_speed:val('scroll_speed'),hour24:val('hour24'),show_seconds:val('show_seconds'),show_temp:$('show_temp').checked?1:0,boot_sync:$('boot_sync').checked?1:0,ntp_period:val('ntp_period'),dday_period:val('dday_period'),retry_period:val('retry_period'),wifi_sleep:$('wifi_sleep').checked?1:0,brightness:val('brightness'),night_level:val('night_level'),led_enabled:$('led_enabled').checked?1:0,led_brightness:val('led_brightness'),led_night_level:val('led_night_level'),night_start:timeToMins(val('night_start')),night_end:timeToMins(val('night_end')),burnin:$('burnin').checked?1:0,screen_off:val('screen_off')})}
async function saveConfig(){setStatus('설정 검증 및 저장 중…');try{await api('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:configBody()});setStatus('설정을 저장했습니다.','ok');setTimeout(refresh,500)}catch(e){setStatus(e.message,'bad')}}
async function syncTime(){setStatus('NTP 동기화를 요청했습니다…');try{const d=await api('/api/time/sync',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({})});if(d.state==='connecting')setStatus('저장된 Wi-Fi에 다시 연결한 뒤 시간을 동기화합니다…');setTimeout(refresh,1500)}catch(e){setStatus(e.message,'bad')}}
async function checkUpdate(){setStatus('GitHub에서 최신 펌웨어를 확인합니다…');try{await api('/api/update/check',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({})});setTimeout(refresh,700)}catch(e){setStatus(e.message,'bad')}}
async function installUpdate(){if(!confirm('새 펌웨어를 설치하고 재부팅하시겠습니까? 설치 중에는 전원을 분리하면 안 됩니다.'))return;setStatus('펌웨어 업데이트를 시작합니다. 잠시 후 설정 Wi-Fi 연결이 종료됩니다…','ok');try{await api('/api/update/install',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({})})}catch(e){setStatus(e.message,'bad')}}
async function exportConfig(){try{const c=await api('/api/config');const e=$('export');e.hidden=false;e.value=JSON.stringify(c,null,2)}catch(e){setStatus(e.message,'bad')}}
async function resetSettings(){if(!confirm('저장된 Wi-Fi는 유지하고 화면·시간·LED 설정을 기본값으로 복원하시겠습니까?'))return;try{await api('/api/settings/reset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({confirm:'DEFAULTS'})});await load();setStatus('표시 설정을 기본값으로 복원했습니다. 저장된 Wi-Fi는 유지됩니다.','ok')}catch(e){setStatus(e.message,'bad')}}
async function factoryReset(){if(prompt('초기화하려면 RESET을 입력하세요.')!=='RESET')return;try{await api('/api/reset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form({confirm:'RESET'})});setStatus('초기화 중입니다. 기기가 재부팅됩니다.','ok')}catch(e){setStatus(e.message,'bad')}}
rangePairs.forEach(p=>bindRangePair(...p));load();setInterval(()=>{if(!wifiPolling)refresh()},10000);
</script></body></html>
)HTML";
