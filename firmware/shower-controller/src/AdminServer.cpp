#include "AdminServer.h"

#include <WiFi.h>
#include <SD.h>
#include <mbedtls/base64.h>

#include "Config.h"

namespace {
const char ADMIN_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Camp Water Admin</title><style>
:root{color-scheme:dark;--bg:#071817;--card:#102b28;--ink:#fff9ea;--muted:#a8c2bc;--a:#53e0a6;--warn:#ffc85b;--danger:#ff756c}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#16423b 0,var(--bg) 38%);color:var(--ink);font:16px system-ui,sans-serif}main{max-width:900px;margin:auto;padding:24px}
.eyebrow{color:var(--a);font-size:13px;font-weight:800;text-transform:uppercase;letter-spacing:.13em}h1{font-size:clamp(34px,8vw,58px);line-height:1;margin:.15em 0}.lede,p,small{color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}.card{background:var(--card);border:1px solid #245047;border-radius:20px;padding:20px;margin:16px 0}
.stat{font-size:32px;font-weight:800;color:var(--a)}label{display:block;font-weight:700;margin:12px 0 6px}input,select{width:100%;font:inherit;padding:12px;border:1px solid #52766f;border-radius:10px;background:#071b19;color:var(--ink)}
button{font:inherit;font-weight:800;border:0;border-radius:11px;padding:12px 15px;background:var(--a);color:#052019;cursor:pointer}.secondary,.tab{background:#31524c;color:var(--ink)}.danger{background:transparent;color:var(--danger);border:1px solid var(--danger)}button:disabled{opacity:.45}
.actions{display:flex;gap:9px;margin-top:12px;flex-wrap:wrap}.status{padding:13px;border-left:4px solid var(--a);background:#0d2522;border-radius:7px}.member{padding:15px 0;border-top:1px solid #31534d}.member:first-child{border-top:0}.head{display:flex;justify-content:space-between;gap:10px}.uid{font:12px ui-monospace,monospace;color:var(--muted)}.usage{color:var(--a);font-weight:700}.disabled{color:var(--warn)}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{text-align:left;padding:9px 5px;border-bottom:1px solid #31534d}th{color:var(--muted)}
.tabs{display:flex;gap:8px;flex-wrap:wrap;margin:24px 0 0}.tab.on{background:var(--a);color:#052019}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--danger);margin-right:7px}.dot.on{background:#0a5}.off .card{opacity:.45;pointer-events:none}#msg{min-height:1.4em}
</style></head><body><main><div class="eyebrow">Camp network · secured</div><h1>Camp Water Admin</h1><p class="lede">One login, every station. Signed in at <span id="here">…</span></p>
<div id="status" class="status">Connecting…</div><div class="grid"><section class="card"><div class="eyebrow">Camp-wide</div><div class="stat"><span id="total">0.00</span> gal</div><small>Completed usage across every station</small><p id="peers" style="margin:10px 0 0">Network: —</p></section>
<section class="card"><div class="eyebrow">Enroll wristband</div><label for="name">Member name</label><input id="name" maxlength="32" placeholder="e.g. Dusty River"><label for="enrollAt">Enroll on (tap the wristband on that station's reader)</label><select id="enrollAt"></select><div class="actions"><button id="arm">Enroll next tag</button><button id="cancel" class="secondary">Cancel</button></div></section></div>
<section class="card"><h2>Members</h2><small>Usage is camp-wide. Edits here reach every station within seconds.</small><div id="members"><small>Loading…</small></div></section>
<section class="card"><h2>Station limits</h2><p>Per-session limits by station kind; synced to every controller.</p><div style="overflow:auto"><table><thead><tr><th>Kind</th><th>Gallons</th><th>Minutes</th></tr></thead><tbody id="limits"></tbody></table></div><div class="actions"><button id="saveLimits">Save limits</button></div><small id="limitsInfo"></small></section>
<section class="card" id="pwCard" style="display:none"><h2>Change password</h2><label for="password">New admin password</label><input id="password" type="password" minlength="8" maxlength="64"><div class="actions"><button id="changePassword">Update password</button></div><small>One password for every station — it syncs over the camp network. You will be asked to sign in again.</small></section>
<div id="tabs" class="tabs"></div><p id="msg"></p><div id="st">
<div class="grid"><section class="card"><h2 id="stName">Station</h2><p id="sess">—</p><div class="actions"><button id="endSession" class="danger">End session</button></div></section>
<section class="card"><h2>Flow calibration</h2><p>Dispense into a known-volume container, then stop at the measured volume.</p><label for="known">Known volume (gallons)</label><input id="known" type="number" min="0.01" max="100" step="0.01" value="1"><div class="actions"><button id="calStart">Start dispensing</button><button id="calStop" class="secondary">Stop &amp; save</button></div><p id="calStatus"></p></section></div>
<div id="musicCards" class="grid"><section class="card"><h2>Shower speaker</h2><p id="audioStatus">Connecting…</p><label for="speakerVolume">Volume: <span id="speakerVolumeValue">—</span>%</label><input id="speakerVolume" type="range" min="0" max="100" step="1"><div class="actions"><button id="saveVolume">Set volume</button><button id="tone">Test tone</button><button id="play">Play channel 1</button><button id="stopAudio" class="secondary">Stop</button><button id="findSpeaker" class="secondary">Find speaker</button></div><label for="audioFile" id="uploadLabel">Replace channel 1</label><input id="audioFile" type="file"><div class="actions"><button id="uploadAudio" class="secondary">Upload audio</button></div><small>The Tough reconnects to a speaker named “Select 4 Go”.</small></section>
<section class="card"><h2>Music vibe selector</h2><p id="knobStatus">Connecting…</p><div class="stat"><span id="knobRaw">—</span><small> / 4095 raw</small></div><p id="knobPoints"></p><div class="actions"><button id="knobCalStart">Start calibration</button><button id="knobCapture">Capture position 0</button><button id="knobCalCancel" class="secondary">Cancel</button></div><small>Notch 0 is quiet; 1–9 are channels. Capture each notch in order.</small></section></div>
<section class="card"><h2>Recent sessions</h2><div style="overflow:auto"><table><thead><tr><th>Member</th><th>Gallons</th><th>Duration</th><th>Ended</th></tr></thead><tbody id="sessions"></tbody></table></div></section>
<section class="card"><h2>Controller</h2><p id="health">Connecting…</p><div class="actions"><button id="reboot" class="danger">Reboot controller</button></div><small>Reboot is safe: the pump is shut off first.</small></section>
</div></main><script>
const $=s=>document.querySelector(s),T=(i,v)=>$('#'+i).textContent=v,D=(i,v)=>$('#'+i).disabled=v,F=(v,d)=>Number(v).toFixed(d),esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])),DS=['OPEN','IN USE','UNAVAILABLE'];
let S=[],sel=+localStorage.tab||0,busy=false;const nm=id=>(S.find(x=>x.id==id)||{}).name||'Station '+id,LIM=[['Shower','shower'],['Water fill','water'],['RV fill','rv']];
$('#limits').innerHTML=LIM.map(([n,k])=>`<tr><td>${n}</td><td><input id="${k}Gal" type="number" min="0.5" max="500" step="0.5"></td><td><input id="${k}Min" type="number" min="1" max="180"></td></tr>`).join('');
async function post(path,data={}){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const j=await r.json();if(!r.ok)throw Error(j.message||'Request failed');return j}
async function cmd(action,extra={},station=sel){try{const j=await post('/api/command',{station,action,...extra});let m=j.message;if(j.pending){m='';for(let i=0;i<13&&!m;i++){await new Promise(x=>setTimeout(x,300));const q=await(await fetch('/api/command?nonce='+j.nonce)).json();if(q.state=='done')m=(q.ok?'':'Rejected: ')+(q.message||'');else if(q.state!='pending')break}m=m||'No answer from '+nm(station)}T('msg',nm(station)+': '+m);refresh()}catch(e){alert(e.message)}}
function pick(id){sel=id;localStorage.tab=id;render()}
async function refresh(){if(busy)return;busy=true;const ctl=new AbortController();const tm=setTimeout(()=>ctl.abort(),5000);try{const r=await fetch('/api/overview',{signal:ctl.signal});const o=await r.json();const s=o.status,m=o.members;S=o.stations;T('here',`${s.station} · ${s.ssid}`);$('#pwCard').style.display=s.pagePassword?'':'none';
T('peers',`Network: ${S.filter(x=>!x.local).map(p=>`${p.name} (${p.online?'online':'last seen '+p.lastSeenS+'s ago'})`).join(' · ')||'no other stations heard yet'} · rx ${s.net.rx} tx ${s.net.tx}${s.net.txFail?' fail '+s.net.txFail:''}`);
const L=s.limits,lim=[];LIM.forEach(([n,k])=>lim.push([k+'Gal',L[k].gal],[k+'Min',L[k].min]));if(!lim.some(([id])=>document.activeElement===$('#'+id)))lim.forEach(([id,v])=>{$('#'+id).value=v});T('limitsInfo',`Version ${L.version} · members version ${s.membersVersion}`);
const ea=$('#enrollAt'),ev=ea.value;ea.innerHTML=S.map(x=>`<option value="${x.id}">${esc(x.name)}${x.online?'':' (offline)'}</option>`).join('');ea.value=ev;if(!ea.value)ea.value=s.stationId;
let total=0;$('#members').innerHTML=m.length?m.map(x=>{total+=x.networkGallons;return `<div class="member"><div class="head"><div><strong>${esc(x.name)}</strong> ${x.enabled?'':'<span class="disabled">disabled</span>'}<div class="uid">${esc(x.uid)}</div></div><div class="usage">${F(x.networkGallons,2)} gal total<br><small>showers ${F(x.showerGallons,1)} · water ${F(x.waterGallons,1)} · RV ${F(x.rvGallons,1)}</small></div></div><div class="actions"><button class="secondary" onclick="editMember('${x.uid}','${encodeURIComponent(x.name)}',${x.allowance},${x.enabled})">Edit · ${x.allowance>0?F(x.allowance,1)+' gal shower limit':'station limits'}</button><button class="danger" onclick="removeMember('${x.uid}')">Delete</button></div></div>`}).join(''):'<small>No members enrolled.</small>';T('total',total.toFixed(2));
render()}catch(e){T('status','Controller not responding — retrying…')}finally{clearTimeout(tm);busy=false}}
function render(){const c=S.find(x=>x.id==sel)||S.find(x=>x.local);if(!c)return;sel=c.id;const t=c.telemetry,se=t.session,h=t.health,on=t.speaker==='connected';
$('#tabs').innerHTML=S.map(x=>`<button class="tab${x.id==sel?' on':''}" onclick="pick(${x.id})"><span class="dot${x.online?' on':''}"></span>${esc(x.name)} · ${x.online?DS[x.telemetry.session.doorState]||'?':'OFFLINE'}</button>`).join('');
const w=S.find(x=>x.telemetry.enrollmentPending);T('status',w?`Waiting for ${w.telemetry.pendingName}'s wristband at ${w.name} — tap it now.`:`${c.name}: ${t.message}`);
$('#st').classList.toggle('off',!c.online);T('stName',`${c.name} · ${c.roleName}${c.local?' · this station':''}${c.online?'':' · OFFLINE'}`);
T('sess',se.active?`${se.name} · ${F(se.gallons,2)} of ${F(se.limit,1)} gal · pump ${se.pumpOn?'ON':'off'}`:`No active session · ${DS[se.doorState]||'?'}`);D('endSession',!se.active);
T('calStatus',`${t.calibrationMessage} · ${t.calibrationPulses} pulses · ${F(t.pulsesPerGallon,2)} pulses/gal`);D('calStart',t.calibrationActive);D('calStop',!t.calibrationActive);$('#musicCards').style.display=t.features.music?'':'none';
T('audioStatus',`Speaker: ${t.speaker} · ${t.audioPlayback} · channel tracks ${t.audioFile?'ready':'missing'}`);if(document.activeElement!==$('#speakerVolume')){$('#speakerVolume').value=t.speakerVolume;T('speakerVolumeValue',t.speakerVolume)}D('tone',!on);D('play',!on||!t.audioFile);
D('uploadAudio',!c.local);D('audioFile',!c.local);T('uploadLabel',c.local?'Replace channel 1 (44.1 kHz stereo signed 16-bit PCM)':`Upload is local only — join ${c.name}'s Wi-Fi`);
T('knobRaw',t.musicKnobRaw);T('knobStatus',t.musicCalibrationActive?`Calibrating · move the knob to notch ${t.musicCalibrationNext} and capture`:`Position ${t.musicChannel} · ${t.musicChannelName} · ${t.musicKnobCalibrated?'calibrated':'using test thresholds'}`);T('knobPoints',t.musicPositions.map((x,i)=>`${i}: ${x===null?'—':x}`).join(' · '));D('knobCalStart',t.musicCalibrationActive);D('knobCapture',!t.musicCalibrationActive);T('knobCapture',`Capture position ${t.musicCalibrationNext}`);D('knobCalCancel',!t.musicCalibrationActive);
$('#sessions').innerHTML=c.recent.length?c.recent.map(x=>`<tr><td>${esc(x.name)}</td><td>${F(x.gallons,2)}</td><td>${x.durationS}s</td><td>${esc(x.reason)}</td></tr>`).join(''):'<tr><td colspan="4">No completed sessions</td></tr>';
T('health',`Uptime ${Math.floor(h.uptimeS/60)} min · heap ${Math.round(h.freeHeap/1024)}k free (low ${Math.round(h.minFreeHeap/1024)}k) · WiFi clients ${h.wifiClients} · hub ${h.hub?'ok':'DOWN'} · relay ${h.relay?'ok':'DOWN'} · RFID ${h.rfid?'ok':'DOWN'} · SD ${h.sd?'ok':'DOWN'} · audio underruns ${h.audioUnderruns}`)}
$('#arm').onclick=()=>cmd('enroll',{name:$('#name').value.trim()},+$('#enrollAt').value);$('#cancel').onclick=()=>cmd('cancel',{},+$('#enrollAt').value);
async function editMember(uid,name,allowance,enabled){name=decodeURIComponent(name);const nextName=prompt('Member name',name);if(!nextName)return;const nextAllowance=prompt('Custom shower limit in gallons (0 = station limit)',allowance);if(nextAllowance===null||nextAllowance==='')return;const nextEnabled=confirm('Allow this wristband to start sessions?');try{await post('/api/member',{uid,name:nextName,allowance:nextAllowance,enabled:nextEnabled?'1':'0'});refresh()}catch(e){alert(e.message)}}
async function removeMember(uid){if(!confirm('Delete this wristband registration? Historical usage remains.'))return;await post('/api/delete',{uid});refresh()}
for(const[i,a]of[['calStart','calStart'],['tone','tone'],['play','play'],['stopAudio','stop'],['findSpeaker','findSpeaker'],['knobCalStart','musicCalStart'],['knobCapture','musicCalCapture'],['knobCalCancel','musicCalCancel']])$('#'+i).onclick=()=>cmd(a);
$('#calStop').onclick=()=>cmd('calStop',{gallons:$('#known').value});
$('#changePassword').onclick=async()=>{try{await post('/api/password',{password:$('#password').value});alert('Password changed on every station. Sign in again.');location.reload()}catch(e){alert(e.message)}};
$('#speakerVolume').oninput=()=>T('speakerVolumeValue',$('#speakerVolume').value);$('#saveVolume').onclick=()=>cmd('volume',{volume:$('#speakerVolume').value});
$('#uploadAudio').onclick=async()=>{const f=$('#audioFile').files[0];if(!f)return alert('Choose a PCM file first');const body=new FormData();body.append('audio',f);T('audioStatus',`Uploading ${f.name}…`);const r=await fetch('/api/audio/upload',{method:'POST',body});const j=await r.json();if(!r.ok)return alert(j.message||'Upload failed');refresh()};
$('#saveLimits').onclick=async()=>{const d={};LIM.forEach(([n,k])=>['Gal','Min'].forEach(u=>d[k+u]=$('#'+k+u).value));try{await post('/api/limits',d);refresh()}catch(e){alert(e.message)}};
$('#endSession').onclick=()=>{if(confirm(`End the active session at ${nm(sel)}?`))cmd('endSession')};$('#reboot').onclick=()=>{if(confirm(`Reboot ${nm(sel)}?`))cmd('reboot')};
refresh();setInterval(refresh,2000);
</script></body></html>)HTML";

constexpr uint32_t TELEMETRY_REBUILD_MS = 250;
// Give the ACK a chance to leave the radio before a remotely requested reboot.
constexpr uint32_t REMOTE_REBOOT_DELAY_MS = 500;
constexpr uint8_t COMMAND_DRAIN_PER_LOOP = 4;

struct ActionName {
  const char* name;
  uint8_t action;
};
const ActionName ACTION_NAMES[] = {
    {"enroll", CampNet::CMD_ENROLL},
    {"cancel", CampNet::CMD_CANCEL_ENROLL},
    {"calStart", CampNet::CMD_CALIBRATION_START},
    {"calStop", CampNet::CMD_CALIBRATION_STOP},
    {"musicCalStart", CampNet::CMD_MUSIC_CAL_START},
    {"musicCalCapture", CampNet::CMD_MUSIC_CAL_CAPTURE},
    {"musicCalCancel", CampNet::CMD_MUSIC_CAL_CANCEL},
    {"tone", CampNet::CMD_AUDIO_TONE},
    {"play", CampNet::CMD_AUDIO_PLAY},
    {"stop", CampNet::CMD_AUDIO_STOP},
    {"volume", CampNet::CMD_AUDIO_VOLUME},
    {"findSpeaker", CampNet::CMD_SPEAKER_SEARCH},
    {"reboot", CampNet::CMD_REBOOT},
    {"endSession", CampNet::CMD_END_SESSION},
};

uint8_t actionFromName(const String& name) {
  for (const ActionName& entry : ACTION_NAMES) {
    if (name == entry.name) return entry.action;
  }
  return 0;
}

// Actions that need the speaker / music knob, absent on fill stations.
bool needsMusic(uint8_t action) {
  switch (action) {
    case CampNet::CMD_MUSIC_CAL_START:
    case CampNet::CMD_MUSIC_CAL_CAPTURE:
    case CampNet::CMD_MUSIC_CAL_CANCEL:
    case CampNet::CMD_AUDIO_TONE:
    case CampNet::CMD_AUDIO_PLAY:
    case CampNet::CMD_AUDIO_STOP:
    case CampNet::CMD_AUDIO_VOLUME:
    case CampNet::CMD_SPEAKER_SEARCH:
      return true;
    default:
      return false;
  }
}

bool parseVolume(const String& value, long& percent) {
  if (value.isEmpty() || value.length() > 3) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) return false;
  }
  percent = value.toInt();
  return percent >= 0 && percent <= 100;
}

// Bounded copy of a packet char array that may lack a terminator.
template <size_t N>
String field(const char (&text)[N]) {
  String out;
  out.reserve(N);
  for (size_t i = 0; i < N && text[i] != '\0'; ++i) out += text[i];
  return out;
}

const char* boolJson(bool value) { return value ? "true" : "false"; }
}  // namespace

AdminServer::AdminServer(MemberRegistry& registry, const PulseStorage& pulseStorage,
                         const SessionStorage& sessions, SettingsStore& settings,
                         SpeakerAudio& speakerAudio, const UsageLedger& ledger,
                         CampNetLink& net)
    : registry_(registry), pulseStorage_(pulseStorage), sessions_(sessions),
      settings_(settings), speakerAudio_(speakerAudio), ledger_(ledger), net_(net) {}

bool AdminServer::begin() {
  if (started_) return true;
  // CampNet owns the radio and brings up the soft-AP; this only serves HTTP.
  if (!net_.ready()) return false;
  // begin() is retried from the main loop if the AP fails at boot, so the
  // route table must only be registered once.
  if (!routesConfigured_) {
    const char* headers[] = {"Authorization"};
    server_.collectHeaders(headers, 1);
    configureRoutes();
    routesConfigured_ = true;
  }
  server_.begin();
  started_ = true;
  return true;
}

void AdminServer::handle() {
  if (started_) server_.handleClient();
  drainRemoteCommands();
  if (millis() - lastTelemetryMs_ >= TELEMETRY_REBUILD_MS) {
    lastTelemetryMs_ = millis();
    publishTelemetry();
  }
}

bool AdminServer::onTagScanned(const String& uid) {
  lastUid_ = uid;
  if (!enrollmentPending_) return false;
  const String name = pendingName_;
  const bool saved = registry_.upsert(uid.c_str(), name);
  if (saved) net_.markMembersDirty();
  enrollmentPending_ = false;
  pendingName_ = "";
  lastMessage_ = saved ? name + " enrolled" : "Enrollment save failed";
  return true;
}

String AdminServer::address() const { return started_ ? WiFi.softAPIP().toString() : String("offline"); }

bool AdminServer::takeCalibrationStartRequest() {
  const bool requested = calibrationStartRequested_;
  calibrationStartRequested_ = false;
  return requested;
}

bool AdminServer::takeCalibrationStopRequest(float& knownGallons) {
  if (!calibrationStopRequested_) return false;
  calibrationStopRequested_ = false;
  knownGallons = calibrationKnownGallons_;
  return true;
}

void AdminServer::reportCalibration(bool active, uint32_t pulses, const String& message) {
  calibrationActive_ = active;
  calibrationPulses_ = pulses;
  calibrationMessage_ = message;
}

bool AdminServer::takeMusicCalibrationStartRequest() {
  const bool requested = musicCalibrationStartRequested_;
  musicCalibrationStartRequested_ = false;
  return requested;
}

bool AdminServer::takeMusicCalibrationCaptureRequest() {
  const bool requested = musicCalibrationCaptureRequested_;
  musicCalibrationCaptureRequested_ = false;
  return requested;
}

bool AdminServer::takeMusicCalibrationCancelRequest() {
  const bool requested = musicCalibrationCancelRequested_;
  musicCalibrationCancelRequested_ = false;
  return requested;
}

void AdminServer::reportMusicKnob(uint16_t raw, int8_t channel,
                                  bool calibrationActive, uint8_t nextPosition,
                                  const String& message) {
  musicKnobRaw_ = raw;
  musicChannel_ = channel;
  musicCalibrationActive_ = calibrationActive;
  musicCalibrationNextPosition_ = nextPosition;
  musicCalibrationMessage_ = message;
}

void AdminServer::reportHardware(bool hubReady, bool relayReady, bool rfidReady) {
  hubReady_ = hubReady;
  relayReady_ = relayReady;
  rfidReady_ = rfidReady;
}

void AdminServer::reportSession(const char* activeName, float sessionGallons,
                                float sessionLimit, bool pumpOn, uint8_t doorState) {
  strlcpy(activeName_, activeName ? activeName : "", sizeof(activeName_));
  sessionGallons_ = sessionGallons;
  sessionLimit_ = sessionLimit;
  pumpOn_ = pumpOn;
  doorState_ = doorState;
}

bool AdminServer::takeRebootRequest() {
  if (!rebootRequested_ || static_cast<int32_t>(millis() - rebootReadyMs_) < 0) return false;
  rebootRequested_ = false;
  return true;
}

bool AdminServer::takeSpeakerSearchRequest() {
  const bool requested = speakerSearchRequested_;
  speakerSearchRequested_ = false;
  return requested;
}

bool AdminServer::takeEndSessionRequest() {
  const bool requested = endSessionRequested_;
  endSessionRequested_ = false;
  return requested;
}

bool AdminServer::authorize() {
  // The Wi-Fi password is the gate; the page itself is open unless enabled.
  if (!Config::ADMIN_PAGE_PASSWORD) return true;
  String header = server_.header("Authorization");
  if (header.startsWith("Basic ")) {
    header.remove(0, 6);
    unsigned char decoded[128] = {0};
    size_t decodedLength = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decodedLength,
                              reinterpret_cast<const unsigned char*>(header.c_str()),
                              header.length()) == 0) {
      decoded[decodedLength] = 0;
      const String credentials(reinterpret_cast<char*>(decoded));
      const int colon = credentials.indexOf(':');
      if (colon > 0 && credentials.substring(0, colon) == Config::ADMIN_USERNAME &&
          settings_.verifyPassword(credentials.substring(colon + 1))) return true;
    }
  }
  server_.sendHeader("WWW-Authenticate", "Basic realm=\"Camp Shower Admin\"");
  server_.send(401, "application/json", "{\"ok\":false,\"message\":\"Authentication required\"}");
  return false;
}

void AdminServer::configureRoutes() {
  server_.on("/", HTTP_GET, [this]() { if (authorize()) server_.send_P(200, "text/html", ADMIN_PAGE); });
  server_.on("/api/status", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", statusJson()); });
  server_.on("/api/members", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", "{\"members\":" + membersJson() + "}"); });
  server_.on("/api/sessions", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", "{\"sessions\":" + sessionsJson() + "}"); });
  server_.on("/api/health", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", healthJson()); });
  server_.on("/api/stations", HTTP_GET, [this]() { if (authorize()) server_.send(200, "application/json", stationsJson()); });
  server_.on("/api/overview", HTTP_GET, [this]() {
    if (!authorize()) return;
    // One request per poll instead of several keeps the single-threaded
    // WebServer responsive and the admin page resilient.
    const String stations = stationsJson();
    String body;
    body.reserve(2048 + registry_.count() * 192 + sessions_.recentCount() * 128 + stations.length());
    body += "{\"status\":";
    body += statusJson();
    body += ",\"health\":";
    body += healthJson();
    body += ",\"members\":";
    body += membersJson();
    body += ",\"sessions\":";
    body += sessionsJson();
    body += ",\"stations\":";
    body += stations;
    body += '}';
    server_.send(200, "application/json", body);
  });
  server_.on("/api/command", HTTP_POST, [this]() { if (authorize()) handleCommandPost(); });
  server_.on("/api/command", HTTP_GET, [this]() { if (authorize()) handleCommandPoll(); });
  // Legacy per-action routes share the command implementations.
  server_.on("/api/reboot", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_REBOOT, "", 0.0F); });
  server_.on("/api/speaker/search", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_SPEAKER_SEARCH, "", 0.0F); });
  server_.on("/api/enroll", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_ENROLL, server_.arg("name"), 0.0F); });
  server_.on("/api/cancel", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_CANCEL_ENROLL, "", 0.0F); });
  server_.on("/api/member", HTTP_POST, [this]() { if (authorize()) updateMember(); });
  server_.on("/api/rename", HTTP_POST, [this]() { if (authorize()) renameMember(); });
  server_.on("/api/delete", HTTP_POST, [this]() { if (authorize()) deleteMember(); });
  if (Config::ADMIN_PAGE_PASSWORD) {
    server_.on("/api/password", HTTP_POST, [this]() { if (authorize()) changePassword(); });
  }
  server_.on("/api/calibration/start", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_CALIBRATION_START, "", 0.0F); });
  server_.on("/api/calibration/stop", HTTP_POST, [this]() {
    if (authorize()) sendAction(CampNet::CMD_CALIBRATION_STOP, "", server_.arg("gallons").toFloat());
  });
  server_.on("/api/music/calibration/start", HTTP_POST,
             [this]() { if (authorize()) sendAction(CampNet::CMD_MUSIC_CAL_START, "", 0.0F); });
  server_.on("/api/music/calibration/capture", HTTP_POST,
             [this]() { if (authorize()) sendAction(CampNet::CMD_MUSIC_CAL_CAPTURE, "", 0.0F); });
  server_.on("/api/music/calibration/cancel", HTTP_POST,
             [this]() { if (authorize()) sendAction(CampNet::CMD_MUSIC_CAL_CANCEL, "", 0.0F); });
  server_.on("/api/audio/tone", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_AUDIO_TONE, "", 0.0F); });
  server_.on("/api/audio/play", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_AUDIO_PLAY, "", 0.0F); });
  server_.on("/api/audio/stop", HTTP_POST, [this]() { if (authorize()) sendAction(CampNet::CMD_AUDIO_STOP, "", 0.0F); });
  server_.on("/api/audio/volume", HTTP_POST, [this]() {
    if (!authorize()) return;
    long percent = 0;
    if (!parseVolume(server_.arg("volume"), percent)) return sendJsonMessage(400, false, "Volume must be 0-100");
    sendAction(CampNet::CMD_AUDIO_VOLUME, "", static_cast<float>(percent));
  });
  server_.on("/api/limits", HTTP_POST, [this]() { if (authorize()) setRoleLimits(); });
  server_.on("/api/audio/upload", HTTP_POST,
             [this]() {
               if (!audioUploadAuthorized_) return;
               const bool ok = !audioUploadFailed_ && audioUploadBytes_ > 0;
               sendJsonMessage(ok ? 200 : 500, ok,
                               ok ? String("Uploaded ") + audioUploadBytes_ + " bytes" : "Audio upload failed");
               audioUploadAuthorized_ = false;
             },
             [this]() { handleAudioUpload(); });
  server_.onNotFound([this]() { if (authorize()) sendJsonMessage(404, false, "Not found"); });
}

// ---- Telemetry published over CampNet and rendered for every station ----

void AdminServer::publishTelemetry() {
  CampNet::TelemetryPacket telemetry;
  buildTelemetry(telemetry);
  net_.setLocalTelemetry(telemetry);

  const size_t count = sessions_.recentCount();
  const uint32_t newestEndMs = count ? sessions_.recentAt(0).endMs : 0;
  if (recentPublished_ && count == publishedRecentCount_ && newestEndMs == publishedRecentEndMs_) return;
  CampNet::RecentPacket recent;
  buildRecent(recent);
  net_.setLocalRecent(recent);
  recentPublished_ = true;
  publishedRecentCount_ = count;
  publishedRecentEndMs_ = newestEndMs;
}

void AdminServer::buildTelemetry(CampNet::TelemetryPacket& t) const {
  memset(&t, 0, sizeof(t));
  t.uptimeS = millis() / 1000UL;
  t.freeHeap = ESP.getFreeHeap();
  t.minFreeHeap = ESP.getMinFreeHeap();
  t.audioUnderruns = speakerAudio_.bufferUnderruns();
  t.calibrationPulses = calibrationPulses_;
  t.pulsesPerGallon = settings_.pulsesPerGallon();
  t.sessionGallons = sessionGallons_;
  t.sessionLimit = sessionLimit_;
  t.musicKnobRaw = musicKnobRaw_;
  const bool knobCalibrated = settings_.musicKnobCalibrated();
  if (knobCalibrated) {
    for (uint8_t i = 0; i < CampNet::MUSIC_POSITIONS && i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
      t.musicPositions[i] = settings_.musicKnobPosition(i);
    }
  }
  const bool sdOk = pulseStorage_.healthy() && sessions_.healthy() && settings_.healthy() && registry_.healthy();
  t.flags = (sdOk ? CampNet::TELEM_SD_OK : 0) | (hubReady_ ? CampNet::TELEM_HUB_OK : 0) |
            (relayReady_ ? CampNet::TELEM_RELAY_OK : 0) | (rfidReady_ ? CampNet::TELEM_RFID_OK : 0) |
            (calibrationActive_ ? CampNet::TELEM_CALIBRATION_ACTIVE : 0) |
            (speakerAudio_.connected() ? CampNet::TELEM_SPEAKER_CONNECTED : 0) |
            (speakerAudio_.fileAvailable() ? CampNet::TELEM_AUDIO_FILE : 0) |
            (musicCalibrationActive_ ? CampNet::TELEM_MUSIC_CAL_ACTIVE : 0);
  t.features = (Config::HAS_MUSIC ? CampNet::FEATURE_MUSIC : 0) |
               (Config::HAS_LED_STRIP ? CampNet::FEATURE_LEDS : 0) |
               (Config::HAS_DOOR_SIGN ? CampNet::FEATURE_DOOR_SIGN : 0) |
               (enrollmentPending_ ? CampNet::FEATURE_ENROLL_PENDING : 0) |
               (knobCalibrated ? CampNet::FEATURE_MUSIC_CALIBRATED : 0) |
               (pumpOn_ ? CampNet::FEATURE_PUMP_ON : 0);
  t.doorState = doorState_;
  t.wifiClients = WiFi.softAPgetStationNum();
  t.speakerVolume = speakerAudio_.speakerVolumePercent();
  t.musicChannel = musicChannel_;
  t.musicCalNext = musicCalibrationNextPosition_;
  strlcpy(t.activeName, activeName_, sizeof(t.activeName));
  strlcpy(t.pendingName, pendingName_.c_str(), sizeof(t.pendingName));
  strlcpy(t.speaker, speakerAudio_.connectionLabel(), sizeof(t.speaker));
  strlcpy(t.playback, speakerAudio_.playbackLabel(), sizeof(t.playback));
  strlcpy(t.calibrationMessage, calibrationMessage_.c_str(), sizeof(t.calibrationMessage));
  strlcpy(t.message, lastMessage_.c_str(), sizeof(t.message));
}

void AdminServer::buildRecent(CampNet::RecentPacket& r) const {
  memset(&r, 0, sizeof(r));
  const size_t count = sessions_.recentCount();
  r.count = static_cast<uint8_t>(count < CampNet::RECENT_ENTRIES_PER_PACKET ? count : CampNet::RECENT_ENTRIES_PER_PACKET);
  for (uint8_t i = 0; i < r.count; ++i) {
    const SessionStorage::Record& record = sessions_.recentAt(i);
    CampNet::RecentEntry& entry = r.entries[i];
    entry.uidLen = CampNet::uidFromHex(record.uid, entry.uid);
    entry.gallons = record.gallons;
    const uint32_t seconds = (record.endMs - record.startMs) / 1000UL;
    entry.durationS = static_cast<uint16_t>(seconds > 65535UL ? 65535UL : seconds);
    entry.reason = CampNet::sessionReasonCode(record.reason);
  }
}

String AdminServer::telemetryJson(const CampNet::TelemetryPacket& t) const {
  const bool calibrated = t.features & CampNet::FEATURE_MUSIC_CALIBRATED;
  const uint8_t safeChannel =
      t.musicChannel >= 0 && t.musicChannel < Config::MUSIC_KNOB_POSITION_COUNT
          ? static_cast<uint8_t>(t.musicChannel) : 0;
  String body;
  body.reserve(1024);
  body += "{\"calibrationActive\":"; body += boolJson(t.flags & CampNet::TELEM_CALIBRATION_ACTIVE);
  body += ",\"calibrationPulses\":" + String(t.calibrationPulses);
  body += ",\"calibrationMessage\":\"" + jsonEscape(field(t.calibrationMessage)) + "\"";
  body += ",\"pulsesPerGallon\":" + String(t.pulsesPerGallon, 4);
  body += ",\"speaker\":\"" + jsonEscape(field(t.speaker)) + "\"";
  body += ",\"audioPlayback\":\"" + jsonEscape(field(t.playback)) + "\"";
  body += ",\"audioFile\":"; body += boolJson(t.flags & CampNet::TELEM_AUDIO_FILE);
  body += ",\"speakerVolume\":" + String(t.speakerVolume);
  body += ",\"musicKnobRaw\":" + String(t.musicKnobRaw);
  body += ",\"musicChannel\":" + String(t.musicChannel);
  body += ",\"musicChannelName\":\"" + jsonEscape(Config::MUSIC_CHANNEL_NAMES[safeChannel]) + "\"";
  body += ",\"musicKnobCalibrated\":"; body += boolJson(calibrated);
  body += ",\"musicCalibrationActive\":"; body += boolJson(t.flags & CampNet::TELEM_MUSIC_CAL_ACTIVE);
  body += ",\"musicCalibrationNext\":" + String(t.musicCalNext);
  body += ",\"musicPositions\":[";
  for (uint8_t i = 0; i < CampNet::MUSIC_POSITIONS; ++i) {
    if (i) body += ',';
    if (calibrated) body += String(t.musicPositions[i]);
    else body += "null";
  }
  body += "],\"enrollmentPending\":"; body += boolJson(t.features & CampNet::FEATURE_ENROLL_PENDING);
  body += ",\"pendingName\":\"" + jsonEscape(field(t.pendingName)) + "\"";
  body += ",\"message\":\"" + jsonEscape(field(t.message)) + "\"";
  body += ",\"features\":{\"music\":"; body += boolJson(t.features & CampNet::FEATURE_MUSIC);
  body += ",\"leds\":"; body += boolJson(t.features & CampNet::FEATURE_LEDS);
  body += ",\"doorSign\":"; body += boolJson(t.features & CampNet::FEATURE_DOOR_SIGN);
  body += "},\"health\":{\"uptimeS\":" + String(t.uptimeS);
  body += ",\"freeHeap\":" + String(t.freeHeap);
  body += ",\"minFreeHeap\":" + String(t.minFreeHeap);
  body += ",\"wifiClients\":" + String(t.wifiClients);
  body += ",\"hub\":"; body += boolJson(t.flags & CampNet::TELEM_HUB_OK);
  body += ",\"relay\":"; body += boolJson(t.flags & CampNet::TELEM_RELAY_OK);
  body += ",\"rfid\":"; body += boolJson(t.flags & CampNet::TELEM_RFID_OK);
  body += ",\"sd\":"; body += boolJson(t.flags & CampNet::TELEM_SD_OK);
  body += ",\"audioUnderruns\":" + String(t.audioUnderruns);
  body += "},\"session\":{\"active\":"; body += boolJson(t.activeName[0] != '\0');
  body += ",\"name\":\"" + jsonEscape(field(t.activeName)) + "\"";
  body += ",\"gallons\":" + String(t.sessionGallons, 3);
  body += ",\"limit\":" + String(t.sessionLimit, 2);
  body += ",\"pumpOn\":"; body += boolJson(t.features & CampNet::FEATURE_PUMP_ON);
  body += ",\"doorState\":" + String(t.doorState) + "}}";
  return body;
}

String AdminServer::recentJson(const CampNet::RecentPacket& r) const {
  const uint8_t count = r.count < CampNet::RECENT_ENTRIES_PER_PACKET ? r.count : CampNet::RECENT_ENTRIES_PER_PACKET;
  String body;
  body.reserve(count * 110 + 4);
  body += '[';
  for (uint8_t i = 0; i < count; ++i) {
    if (i) body += ',';
    const CampNet::RecentEntry& entry = r.entries[i];
    char hex[CampNet::UID_BYTES * 2 + 1];
    CampNet::uidToHex(entry.uid, entry.uidLen, hex);
    const char* name = registry_.nameFor(hex);
    body += "{\"name\":\"" + jsonEscape(name ? name : "Deleted member");
    body += "\",\"uid\":\"" + String(hex);
    body += "\",\"gallons\":" + String(entry.gallons, 4);
    body += ",\"durationS\":" + String(entry.durationS);
    body += ",\"reason\":\"" + String(CampNet::sessionReasonName(entry.reason)) + "\"}";
  }
  body += ']';
  return body;
}

String AdminServer::stationsJson() const {
  String body;
  body.reserve(1600 * (net_.peerCount() + 1) + 4);
  body += '[';
  bool first = true;
  CampNet::TelemetryPacket telemetry;
  CampNet::RecentPacket recent;
  for (uint8_t id = 1; id <= CampNet::MAX_STATIONS; ++id) {
    const bool local = id == Config::STATION_ID_VALUE;
    const CampNetLink::Peer& peer = net_.peer(id);
    if (!local && !peer.seen) continue;
    if (local) {
      buildTelemetry(telemetry);
      buildRecent(recent);
    } else {
      const CampNetLink::RemoteTelemetry& remote = net_.telemetry(id);
      if (remote.valid) memcpy(&telemetry, &remote.packet, sizeof(telemetry));
      else memset(&telemetry, 0, sizeof(telemetry));
      const CampNetLink::RemoteRecent& remoteRecent = net_.recent(id);
      if (remoteRecent.valid) memcpy(&recent, &remoteRecent.packet, sizeof(recent));
      else memset(&recent, 0, sizeof(recent));
    }
    if (!first) body += ',';
    first = false;
    const uint8_t role = local ? Config::STATION_ROLE_VALUE : peer.role;
    body += "{\"id\":" + String(id) + ",\"name\":\"" + String(Config::STATION_NAMES[id]) + "\"";
    body += ",\"role\":" + String(role) + ",\"roleName\":\"" + String(CampNet::roleName(role)) + "\"";
    body += ",\"local\":"; body += boolJson(local);
    body += ",\"online\":"; body += boolJson(local || net_.peerOnline(id));
    body += ",\"lastSeenS\":" + String(local ? 0UL : (millis() - peer.lastSeenMs) / 1000UL);
    body += ",\"telemetry\":";
    body += telemetryJson(telemetry);
    body += ",\"recent\":";
    body += recentJson(recent);
    body += '}';
  }
  body += ']';
  return body;
}

// ---- Station actions: one implementation per action, local or remote ----

int AdminServer::runAction(uint8_t action, const String& text, float value, String& message) {
  if (!Config::HAS_MUSIC && needsMusic(action)) {
    message = "Not available on a fill station";
    return 501;
  }
  switch (action) {
    case CampNet::CMD_ENROLL: {
      String name = text;
      name.trim();
      if (!registry_.healthy()) { message = "Member storage unavailable"; return 503; }
      if (name.isEmpty() || name.length() > 32) { message = "Name must be 1-32 characters"; return 400; }
      pendingName_ = name;
      enrollmentPending_ = true;
      lastMessage_ = "Waiting for wristband";
      message = "Tap wristband on reader";
      return 200;
    }
    case CampNet::CMD_CANCEL_ENROLL:
      enrollmentPending_ = false;
      pendingName_ = "";
      lastMessage_ = "Enrollment cancelled";
      message = lastMessage_;
      return 200;
    case CampNet::CMD_CALIBRATION_START:
      calibrationStartRequested_ = true;
      calibrationMessage_ = "Starting…";
      message = "Calibration requested";
      return 200;
    case CampNet::CMD_CALIBRATION_STOP:
      if (!(value > 0.0F)) { message = "Enter a known volume"; return 400; }
      calibrationKnownGallons_ = value;
      calibrationStopRequested_ = true;
      message = "Stop requested";
      return 200;
    case CampNet::CMD_MUSIC_CAL_START:
      musicCalibrationStartRequested_ = true;
      musicCalibrationMessage_ = "Starting...";
      message = "Music knob calibration requested";
      return 200;
    case CampNet::CMD_MUSIC_CAL_CAPTURE:
      if (!musicCalibrationActive_) { message = "Start music knob calibration first"; return 409; }
      musicCalibrationCaptureRequested_ = true;
      message = "Position capture requested";
      return 200;
    case CampNet::CMD_MUSIC_CAL_CANCEL:
      musicCalibrationCancelRequested_ = true;
      message = "Music knob calibration cancelled";
      return 200;
    case CampNet::CMD_AUDIO_TONE: {
      const bool started = speakerAudio_.playTestTone();
      message = started ? "Test tone started" : "Speaker not connected";
      return started ? 200 : 409;
    }
    case CampNet::CMD_AUDIO_PLAY: {
      const bool started = speakerAudio_.playSong();
      message = started ? "Song started" : "Connect speaker and upload audio first";
      return started ? 200 : 409;
    }
    case CampNet::CMD_AUDIO_STOP:
      speakerAudio_.stop();
      message = "Audio stopped";
      return 200;
    case CampNet::CMD_AUDIO_VOLUME: {
      if (value < 0.0F || value > 100.0F) { message = "Volume must be 0-100"; return 400; }
      const uint8_t percent = static_cast<uint8_t>(value);
      if (!settings_.setSpeakerVolumePercent(percent)) { message = "Could not save volume to SD card"; return 503; }
      speakerAudio_.setSpeakerVolumePercent(percent);
      message = String("Speaker volume set to ") + percent + "%";
      return 200;
    }
    case CampNet::CMD_SPEAKER_SEARCH:
      speakerSearchRequested_ = true;
      message = "Searching for speaker";
      return 200;
    case CampNet::CMD_REBOOT:
      rebootRequested_ = true;
      rebootReadyMs_ = millis();
      message = "Rebooting";
      return 200;
    case CampNet::CMD_END_SESSION:
      if (activeName_[0] == '\0') { message = "No active session"; return 409; }
      endSessionRequested_ = true;
      lastMessage_ = "Session end requested";
      message = "Ending session";
      return 200;
    default:
      message = "Unknown action";
      return 400;
  }
}

void AdminServer::sendAction(uint8_t action, const String& text, float value) {
  String message;
  const int code = runAction(action, text, value, message);
  sendJsonMessage(code, code == 200, message);
}

void AdminServer::drainRemoteCommands() {
  CampNetLink::IncomingCommand command;
  for (uint8_t n = 0; n < COMMAND_DRAIN_PER_LOOP && net_.takeIncomingCommand(command); ++n) {
    const uint8_t argLen = command.argLen < CampNet::COMMAND_ARG_BYTES ? command.argLen : CampNet::COMMAND_ARG_BYTES;
    String text;
    float value = 0.0F;
    switch (command.action) {
      case CampNet::CMD_ENROLL:
        text.reserve(argLen);
        for (uint8_t i = 0; i < argLen && command.args[i] != '\0'; ++i) text += static_cast<char>(command.args[i]);
        break;
      case CampNet::CMD_CALIBRATION_STOP:
        if (argLen >= sizeof(float)) memcpy(&value, command.args, sizeof(float));
        break;
      case CampNet::CMD_AUDIO_VOLUME:
        value = argLen >= 1 ? static_cast<float>(command.args[0]) : -1.0F;
        break;
      default:
        break;
    }
    String message;
    const int code = runAction(command.action, text, value, message);
    if (code == 200 && command.action == CampNet::CMD_REBOOT) rebootReadyMs_ = millis() + REMOTE_REBOOT_DELAY_MS;
    const uint8_t status = code == 200 ? CampNet::ACK_OK : code == 501 ? CampNet::ACK_UNSUPPORTED : CampNet::ACK_REJECTED;
    net_.respondToCommand(command, status, message.c_str());
    Serial.printf("[ADMIN] remote command %u from station %u -> %d %s\n",
                  command.action, command.fromStation, code, message.c_str());
  }
}

void AdminServer::handleCommandPost() {
  const uint8_t action = actionFromName(server_.arg("action"));
  if (action == 0) return sendJsonMessage(400, false, "Unknown action");
  const long station = server_.arg("station").toInt();
  if (station < 1 || station > CampNet::MAX_STATIONS) return sendJsonMessage(400, false, "Unknown station");

  String text = server_.arg("name");
  text.trim();
  float value = 0.0F;
  uint8_t args[CampNet::COMMAND_ARG_BYTES] = {0};
  uint8_t argLen = 0;
  switch (action) {
    case CampNet::CMD_ENROLL:
      if (text.isEmpty() || text.length() > 32) return sendJsonMessage(400, false, "Name must be 1-32 characters");
      argLen = static_cast<uint8_t>(text.length());
      memcpy(args, text.c_str(), argLen);
      break;
    case CampNet::CMD_CALIBRATION_STOP:
      value = server_.arg("gallons").toFloat();
      if (!(value > 0.0F)) return sendJsonMessage(400, false, "Enter a known volume");
      memcpy(args, &value, sizeof(value));
      argLen = sizeof(value);
      break;
    case CampNet::CMD_AUDIO_VOLUME: {
      long percent = 0;
      if (!parseVolume(server_.arg("volume"), percent)) return sendJsonMessage(400, false, "Volume must be 0-100");
      value = static_cast<float>(percent);
      args[0] = static_cast<uint8_t>(percent);
      argLen = 1;
      break;
    }
    default:
      break;
  }
  if (station == Config::STATION_ID_VALUE) return sendAction(action, text, value);

  const uint32_t nonce = net_.sendCommand(static_cast<uint8_t>(station), action, args, argLen);
  if (nonce == 0) return sendJsonMessage(503, false, "Station link unavailable");
  server_.send(202, "application/json",
               String("{\"ok\":true,\"pending\":true,\"nonce\":") + nonce + '}');
}

void AdminServer::handleCommandPoll() {
  const uint32_t nonce = strtoul(server_.arg("nonce").c_str(), nullptr, 10);
  const CampNetLink::CommandResult result = net_.commandResult(nonce);
  using State = CampNetLink::CommandResult::State;
  const char* state = result.state == State::Pending ? "pending"
                    : result.state == State::Done ? "done"
                    : result.state == State::Timeout ? "timeout" : "unknown";
  String message = field(result.message);
  if (result.state == State::Done && message.isEmpty()) {
    message = result.status == CampNet::ACK_OK ? "Done"
            : result.status == CampNet::ACK_UNSUPPORTED ? "Not supported on that station"
            : result.status == CampNet::ACK_UNAUTHORIZED ? "Station rejected the request as unauthorized"
            : "Rejected";
  }
  String body;
  body.reserve(160);
  body += String("{\"state\":\"") + state + "\",\"ok\":";
  body += boolJson(result.state == State::Done && result.status == CampNet::ACK_OK);
  body += ",\"status\":" + String(result.status);
  body += ",\"message\":\"" + jsonEscape(message) + "\"}";
  server_.send(200, "application/json", body);
}

// ---- Legacy JSON views ----

String AdminServer::sessionsJson() const {
  String body;
  body.reserve(sessions_.recentCount() * 128 + 4);
  body += '[';
  for (size_t i = 0; i < sessions_.recentCount(); ++i) {
    if (i) body += ',';
    const auto& record = sessions_.recentAt(i);
    const char* name = registry_.nameFor(record.uid);
    body += "{\"uid\":\"" + jsonEscape(record.uid) + "\",\"name\":\"";
    body += jsonEscape(name ? name : "Deleted member");
    body += "\",\"gallons\":" + String(record.gallons, 4);
    body += ",\"durationMs\":" + String(record.endMs - record.startMs);
    body += ",\"reason\":\"" + jsonEscape(record.reason) + "\"}";
  }
  body += ']';
  return body;
}

String AdminServer::healthJson() const {
  String body;
  body.reserve(256);
  body += "{\"uptimeMs\":" + String(millis());
  body += ",\"freeHeap\":" + String(ESP.getFreeHeap());
  body += ",\"minFreeHeap\":" + String(ESP.getMinFreeHeap());
  body += ",\"maxAllocHeap\":" + String(ESP.getMaxAllocHeap());
  body += ",\"freePsram\":" + String(ESP.getFreePsram());
  body += ",\"wifiClients\":" + String(WiFi.softAPgetStationNum());
  body += ",\"hub\":" + String(hubReady_ ? "true" : "false");
  body += ",\"relay\":" + String(relayReady_ ? "true" : "false");
  body += ",\"rfid\":" + String(rfidReady_ ? "true" : "false");
  body += ",\"sd\":" + String((pulseStorage_.healthy() && sessions_.healthy() &&
                               settings_.healthy() && registry_.healthy())
                                  ? "true" : "false");
  body += ",\"audioUnderruns\":" + String(speakerAudio_.bufferUnderruns());
  body += '}';
  return body;
}

String AdminServer::statusJson() const {
  String body;
  body.reserve(1024);
  body += "{\"station\":\"" + String(Config::STATION_NAME) + "\",\"ip\":\"" + address();
  body += "\",\"stationId\":" + String(Config::STATION_ID_VALUE);
  body += ",\"role\":" + String(Config::STATION_ROLE_VALUE);
  body += ",\"roleName\":\"" + String(CampNet::roleName(Config::STATION_ROLE_VALUE)) + "\"";
  body += ",\"ssid\":\"" + String(Config::WIFI_AP_NAME) + "\"";
  body += ",\"pagePassword\":" + String(Config::ADMIN_PAGE_PASSWORD ? "true" : "false");
  body += ",\"features\":{\"music\":" + String(Config::HAS_MUSIC ? "true" : "false");
  body += ",\"leds\":" + String(Config::HAS_LED_STRIP ? "true" : "false");
  body += ",\"doorSign\":" + String(Config::HAS_DOOR_SIGN ? "true" : "false") + "}";
  static const char* const limitKeys[CampNet::ROLE_COUNT] = {"shower", "water", "rv"};
  body += ",\"limits\":{";
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    body += String("\"") + limitKeys[role] + "\":{\"gal\":" + String(settings_.roleLimits(role).gallons, 1);
    body += ",\"min\":" + String(settings_.roleLimits(role).minutes) + "},";
  }
  body += "\"version\":" + String(settings_.limitsVersion()) + "}";
  body += ",\"membersVersion\":" + String(registry_.version());
  body += ",\"net\":{\"ready\":" + String(net_.ready() ? "true" : "false");
  body += ",\"channel\":" + String(CampNet::CHANNEL);
  body += ",\"rx\":" + String(net_.rxPackets()) + ",\"rxDropped\":" + String(net_.rxDropped());
  body += ",\"tx\":" + String(net_.txPackets()) + ",\"txFail\":" + String(net_.txFailures()) + "}";
  body += ",\"peers\":[";
  bool firstPeer = true;
  for (uint8_t id = 1; id <= CampNet::MAX_STATIONS; ++id) {
    const CampNetLink::Peer& peer = net_.peer(id);
    if (!peer.seen) continue;
    if (!firstPeer) body += ',';
    firstPeer = false;
    body += "{\"id\":" + String(id) + ",\"name\":\"" + String(Config::STATION_NAMES[id]) + "\"";
    body += ",\"role\":\"" + String(CampNet::roleName(peer.role)) + "\"";
    body += ",\"online\":" + String(net_.peerOnline(id) ? "true" : "false");
    body += ",\"lastSeenS\":" + String((millis() - peer.lastSeenMs) / 1000UL);
    body += ",\"state\":\"" + String(CampNet::doorStateName(peer.doorState)) + "\"";
    body += ",\"membersVersion\":" + String(peer.membersVersion);
    body += ",\"limitsVersion\":" + String(peer.limitsVersion) + "}";
  }
  body += "]";
  body += ",\"enrollmentPending\":" + String(enrollmentPending_ ? "true" : "false");
  body += ",\"pendingName\":\"" + jsonEscape(pendingName_) + "\",\"lastUid\":\"" + jsonEscape(lastUid_);
  body += "\",\"message\":\"" + jsonEscape(lastMessage_) + "\",\"pulsesPerGallon\":" + String(settings_.pulsesPerGallon(), 4);
  body += ",\"calibrationActive\":" + String(calibrationActive_ ? "true" : "false");
  body += ",\"calibrationPulses\":" + String(calibrationPulses_);
  body += ",\"calibrationMessage\":\"" + jsonEscape(calibrationMessage_) + "\"";
  body += ",\"speaker\":\"" + String(speakerAudio_.connectionLabel()) + "\"";
  body += ",\"audioPlayback\":\"" + String(speakerAudio_.playbackLabel()) + "\"";
  body += ",\"audioFile\":" + String(speakerAudio_.fileAvailable() ? "true" : "false");
  body += ",\"speakerVolume\":" + String(speakerAudio_.speakerVolumePercent());
  body += ",\"musicKnobRaw\":" + String(musicKnobRaw_);
  body += ",\"musicChannel\":" + String(musicChannel_);
  const uint8_t safeMusicChannel =
      musicChannel_ >= 0 && musicChannel_ < Config::MUSIC_KNOB_POSITION_COUNT
          ? static_cast<uint8_t>(musicChannel_)
          : 0;
  body += ",\"musicChannelName\":\"" +
          jsonEscape(Config::MUSIC_CHANNEL_NAMES[safeMusicChannel]) + "\"";
  body += ",\"musicKnobCalibrated\":" + String(settings_.musicKnobCalibrated() ? "true" : "false");
  body += ",\"musicCalibrationActive\":" + String(musicCalibrationActive_ ? "true" : "false");
  body += ",\"musicCalibrationNext\":" + String(musicCalibrationNextPosition_);
  body += ",\"musicCalibrationMessage\":\"" + jsonEscape(musicCalibrationMessage_) + "\"";
  body += ",\"musicPositions\":[";
  for (uint8_t i = 0; i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
    if (i) body += ',';
    if (settings_.musicKnobCalibrated()) body += String(settings_.musicKnobPosition(i));
    else body += "null";
  }
  body += "]}";
  return body;
}

String AdminServer::membersJson() const {
  String body;
  body.reserve(registry_.count() * 192 + 4);
  body += '[';
  for (size_t i = 0; i < registry_.count(); ++i) {
    if (i) body += ',';
    const char* uid = registry_.uidAt(i);
    body += "{\"uid\":\"" + jsonEscape(uid) + "\",\"name\":\"" + jsonEscape(registry_.nameAt(i));
    body += "\",\"allowance\":" + String(registry_.allowanceAt(i), 3);
    body += ",\"enabled\":" + String(registry_.enabledAt(i) ? "true" : "false");
    const float local = sessions_.gallonsFor(uid);
    body += ",\"gallons\":" + String(local, 4);
    body += ",\"sessions\":" + String(sessions_.sessionsFor(uid));
    float byRole[CampNet::ROLE_COUNT];
    for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
      byRole[role] = ledger_.remoteGallonsByRole(uid, role);
    }
    byRole[Config::STATION_ROLE_VALUE] += local;
    body += ",\"networkGallons\":" + String(local + ledger_.remoteGallonsFor(uid), 4);
    body += ",\"networkSessions\":" + String(sessions_.sessionsFor(uid) + ledger_.remoteSessionsFor(uid));
    body += ",\"showerGallons\":" + String(byRole[CampNet::ROLE_SHOWER], 4);
    body += ",\"waterGallons\":" + String(byRole[CampNet::ROLE_WATER_FILL], 4);
    body += ",\"rvGallons\":" + String(byRole[CampNet::ROLE_RV_FILL], 4);
    body += ",\"pulses\":" + String(static_cast<unsigned long long>(pulseStorage_.totalFor(uid))) + '}';
  }
  body += ']';
  return body;
}

// ---- Camp-wide (member / limits / password) endpoints ----

void AdminServer::renameMember() { server_.sendHeader("Location", "/api/member"); updateMember(); }
void AdminServer::updateMember() {
  const String uid = server_.arg("uid"), name = server_.arg("name");
  const float allowance = server_.arg("allowance").toFloat();
  const bool enabled = server_.arg("enabled") == "1";
  if (!registry_.update(uid.c_str(), name, allowance, enabled)) return sendJsonMessage(400, false, "Member update failed");
  net_.markMembersDirty();
  lastMessage_ = "Member updated"; sendJsonMessage(200, true, lastMessage_);
}
void AdminServer::deleteMember() { if (!registry_.remove(server_.arg("uid").c_str())) return sendJsonMessage(404, false, "Registration not found"); net_.markMembersDirty(); lastMessage_ = "Registration deleted"; sendJsonMessage(200, true, lastMessage_); }
void AdminServer::setRoleLimits() {
  static const char* const keys[CampNet::ROLE_COUNT][2] = {
      {"showerGal", "showerMin"}, {"waterGal", "waterMin"}, {"rvGal", "rvMin"}};
  SettingsStore::RoleLimits limits[CampNet::ROLE_COUNT];
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    const String gallons = server_.arg(keys[role][0]);
    const String minutes = server_.arg(keys[role][1]);
    if (gallons.isEmpty() || minutes.isEmpty()) return sendJsonMessage(400, false, "Fill in every limit");
    limits[role].gallons = gallons.toFloat();
    const long parsedMinutes = minutes.toInt();
    limits[role].minutes = static_cast<uint16_t>(constrain(parsedMinutes, 0L, 65535L));
  }
  if (!SettingsStore::limitsValid(limits)) {
    return sendJsonMessage(400, false, "Gallons must be 0.5-500 and minutes 1-180");
  }
  if (!settings_.setRoleLimits(limits)) return sendJsonMessage(503, false, "Could not save limits to SD card");
  net_.markLimitsDirty();
  lastMessage_ = "Station limits saved";
  sendJsonMessage(200, true, lastMessage_);
}
void AdminServer::changePassword() {
  if (!settings_.setPassword(server_.arg("password"))) return sendJsonMessage(400, false, "Password must be 8-64 characters");
  // CampNet syncs the new salted hash so one password opens every station.
  net_.markAuthDirty();
  sendJsonMessage(200, true, "Password changed");
}

void AdminServer::handleAudioUpload() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    audioUploadAuthorized_ = authorize();
    audioUploadFailed_ = !audioUploadAuthorized_;
    audioUploadBytes_ = 0;
    if (!audioUploadAuthorized_) return;
    speakerAudio_.stop();
    SD.remove(Config::AUDIO_PATH);
    audioUploadFile_ = SD.open(Config::AUDIO_PATH, FILE_WRITE);
    audioUploadFailed_ = !audioUploadFile_;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!audioUploadAuthorized_ || audioUploadFailed_) return;
    const size_t written = audioUploadFile_.write(upload.buf, upload.currentSize);
    audioUploadBytes_ += written;
    if (written != upload.currentSize) audioUploadFailed_ = true;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (audioUploadFile_) audioUploadFile_.close();
    if (audioUploadFailed_) SD.remove(Config::AUDIO_PATH);
    speakerAudio_.refreshFileAvailability();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (audioUploadFile_) audioUploadFile_.close();
    SD.remove(Config::AUDIO_PATH);
    audioUploadFailed_ = true;
    speakerAudio_.refreshFileAvailability();
  }
}

void AdminServer::sendJsonMessage(int code, bool ok, const String& message) { server_.send(code, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + jsonEscape(message) + "\"}"); }
String AdminServer::jsonEscape(const String& value) { String escaped; escaped.reserve(value.length() + 8); for (size_t i=0;i<value.length();++i){const char c=value[i]; if(c=='"'||c=='\\')escaped+='\\'; if(c=='\n')escaped+="\\n"; else if(c>=32)escaped+=c;} return escaped; }
