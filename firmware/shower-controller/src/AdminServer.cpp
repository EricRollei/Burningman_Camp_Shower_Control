#include "AdminServer.h"

#include <WiFi.h>
#include <SD.h>
#include <mbedtls/base64.h>

#include "Config.h"

namespace {
const char ADMIN_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Camp Shower Admin</title><style>
:root{color-scheme:dark;--bg:#071817;--card:#102b28;--card2:#153632;--ink:#fff9ea;--muted:#a8c2bc;--a:#53e0a6;--warn:#ffc85b;--danger:#ff756c}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#16423b 0,var(--bg) 38%);color:var(--ink);font:16px system-ui,sans-serif}main{max-width:900px;margin:auto;padding:24px}
.eyebrow{color:var(--a);font-size:13px;font-weight:800;text-transform:uppercase;letter-spacing:.13em}h1{font-size:clamp(34px,8vw,58px);line-height:1;margin:.15em 0}.lede,p,small{color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}.card{background:linear-gradient(145deg,var(--card2),var(--card));border:1px solid #245047;border-radius:20px;padding:20px;margin:16px 0;box-shadow:0 14px 40px #0005}
.stat{font-size:32px;font-weight:800;color:var(--a)}label{display:block;font-weight:700;margin:12px 0 6px}input{width:100%;font:inherit;padding:12px;border:1px solid #52766f;border-radius:10px;background:#071b19;color:var(--ink)}
button{font:inherit;font-weight:800;border:0;border-radius:11px;padding:12px 15px;background:var(--a);color:#052019;cursor:pointer}button.secondary{background:#31524c;color:var(--ink)}button.danger{background:transparent;color:var(--danger);border:1px solid var(--danger)}button:disabled{opacity:.45}
.actions{display:flex;gap:9px;margin-top:12px;flex-wrap:wrap}.status{padding:13px;border-left:4px solid var(--a);background:#0d2522;border-radius:7px}.member{padding:15px 0;border-top:1px solid #31534d}.member:first-child{border-top:0}.member-head{display:flex;justify-content:space-between;gap:10px}.uid{font:12px ui-monospace,monospace;color:var(--muted)}.usage{color:var(--a);font-weight:700}.disabled{color:var(--warn)}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{text-align:left;padding:9px 5px;border-bottom:1px solid #31534d}th{color:var(--muted)}
</style></head><body><main><div class="eyebrow">Local controller · secured</div><h1>Shower Admin</h1><p class="lede">Members, water usage, limits, and station calibration.</p>
<div id="status" class="status">Connecting…</div><div class="grid"><section class="card"><div class="eyebrow">Station</div><div class="stat"><span id="total">0.00</span> gal</div><small>Total completed usage · <span id="cal">—</span> pulses/gal</small></section>
<section class="card"><div class="eyebrow">Enroll wristband</div><label for="name">Member name</label><input id="name" maxlength="32" placeholder="e.g. Dusty River"><div class="actions"><button id="arm">Enroll next tag</button><button id="cancel" class="secondary">Cancel</button></div></section></div>
<section class="card"><h2>Members</h2><div id="members"><small>Loading…</small></div></section>
<div class="grid"><section class="card"><h2>Flow calibration</h2><p>Place the shower head in a known-volume container, start dispensing, then stop at the measured volume.</p><label for="known">Known volume (gallons)</label><input id="known" type="number" min="0.01" max="100" step="0.01" value="1"><div class="actions"><button id="calStart">Start dispensing</button><button id="calStop" class="secondary">Stop &amp; save</button></div><p id="calStatus"></p></section>
<section class="card"><h2>Change password</h2><label for="password">New admin password</label><input id="password" type="password" minlength="8" maxlength="64"><div class="actions"><button id="changePassword">Update password</button></div><small>You will be prompted to sign in again.</small></section></div>
<section class="card"><h2>Shower speaker</h2><p id="audioStatus">Connecting…</p><div class="actions"><button id="tone">Test tone</button><button id="play">Play song</button><button id="stopAudio" class="secondary">Stop</button></div><label for="audioFile">Replace song (44.1 kHz stereo signed 16-bit PCM)</label><input id="audioFile" type="file"><div class="actions"><button id="uploadAudio" class="secondary">Upload audio</button></div><small>The Tough automatically searches for and reconnects to a speaker whose Bluetooth name contains “Select 4 Go”.</small></section>
<section class="card"><h2>Recent showers</h2><div style="overflow:auto"><table><thead><tr><th>Member</th><th>Gallons</th><th>Duration</th><th>Ended</th></tr></thead><tbody id="sessions"></tbody></table></div></section>
</main><script>
const $=s=>document.querySelector(s),esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function post(path,data={}){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const j=await r.json();if(!r.ok)throw Error(j.message||'Request failed');return j}
async function refresh(){try{const [s,m,h]=await Promise.all(['/api/status','/api/members','/api/sessions'].map(x=>fetch(x).then(r=>r.json())));$('#status').textContent=s.enrollmentPending?`Waiting for ${s.pendingName}'s wristband — tap it now.`:s.message;$('#cal').textContent=Number(s.pulsesPerGallon).toFixed(2);$('#calStatus').textContent=`${s.calibrationMessage} · ${s.calibrationPulses} pulses`;$('#calStart').disabled=s.calibrationActive;$('#calStop').disabled=!s.calibrationActive;
$('#audioStatus').textContent=`Speaker: ${s.speaker} · ${s.audioPlayback} · song ${s.audioFile?'ready':'not uploaded'}`;$('#tone').disabled=s.speaker!=='connected';$('#play').disabled=s.speaker!=='connected'||!s.audioFile;
let total=0;$('#members').innerHTML=m.members.length?m.members.map(x=>{total+=x.gallons;return `<div class="member"><div class="member-head"><div><strong>${esc(x.name)}</strong> ${x.enabled?'':'<span class="disabled">disabled</span>'}<div class="uid">${esc(x.uid)}</div></div><div class="usage">${Number(x.gallons).toFixed(2)} gal · ${x.sessions} showers</div></div><div class="actions"><button class="secondary" onclick="editMember('${x.uid}','${encodeURIComponent(x.name)}',${x.allowance},${x.enabled})">Edit · ${Number(x.allowance).toFixed(1)} gal limit</button><button class="danger" onclick="removeMember('${x.uid}')">Delete</button></div></div>`}).join(''):'<small>No members enrolled.</small>';$('#total').textContent=total.toFixed(2);
$('#sessions').innerHTML=h.sessions.length?h.sessions.map(x=>`<tr><td>${esc(x.name)}</td><td>${Number(x.gallons).toFixed(2)}</td><td>${Math.round(x.durationMs/1000)}s</td><td>${esc(x.reason)}</td></tr>`).join(''):'<tr><td colspan="4">No completed showers</td></tr>';}catch(e){$('#status').textContent=e.message}}
$('#arm').onclick=async()=>{try{await post('/api/enroll',{name:$('#name').value.trim()});refresh()}catch(e){alert(e.message)}};$('#cancel').onclick=async()=>{await post('/api/cancel');refresh()};
async function editMember(uid,name,allowance,enabled){name=decodeURIComponent(name);const nextName=prompt('Member name',name);if(!nextName)return;const nextAllowance=prompt('Per-shower gallon limit',allowance);if(!nextAllowance)return;const nextEnabled=confirm('Allow this wristband to start showers?');try{await post('/api/member',{uid,name:nextName,allowance:nextAllowance,enabled:nextEnabled?'1':'0'});refresh()}catch(e){alert(e.message)}}
async function removeMember(uid){if(!confirm('Delete this wristband registration? Historical usage remains.'))return;await post('/api/delete',{uid});refresh()}
$('#calStart').onclick=async()=>{try{await post('/api/calibration/start');refresh()}catch(e){alert(e.message)}};$('#calStop').onclick=async()=>{try{await post('/api/calibration/stop',{gallons:$('#known').value});refresh()}catch(e){alert(e.message)}};
$('#changePassword').onclick=async()=>{try{await post('/api/password',{password:$('#password').value});alert('Password changed. Sign in again with the new password.');location.reload()}catch(e){alert(e.message)}};
$('#tone').onclick=async()=>{try{await post('/api/audio/tone');refresh()}catch(e){alert(e.message)}};$('#play').onclick=async()=>{try{await post('/api/audio/play');refresh()}catch(e){alert(e.message)}};$('#stopAudio').onclick=async()=>{await post('/api/audio/stop');refresh()};$('#uploadAudio').onclick=async()=>{const f=$('#audioFile').files[0];if(!f)return alert('Choose a PCM file first');const body=new FormData();body.append('audio',f);$('#audioStatus').textContent=`Uploading ${f.name}…`;const r=await fetch('/api/audio/upload',{method:'POST',body});const j=await r.json();if(!r.ok)return alert(j.message||'Upload failed');refresh()};
refresh();setInterval(refresh,1500);
</script></body></html>)HTML";
}

AdminServer::AdminServer(MemberRegistry& registry, const PulseStorage& pulseStorage,
                         const SessionStorage& sessions, SettingsStore& settings,
                         SpeakerAudio& speakerAudio)
    : registry_(registry), pulseStorage_(pulseStorage), sessions_(sessions),
      settings_(settings), speakerAudio_(speakerAudio) {}

bool AdminServer::begin() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAP(Config::WIFI_AP_NAME, Config::WIFI_AP_PASSWORD)) return false;
  const char* headers[] = {"Authorization"};
  server_.collectHeaders(headers, 1);
  configureRoutes();
  server_.begin();
  started_ = true;
  return true;
}

void AdminServer::handle() { if (started_) server_.handleClient(); }

bool AdminServer::onTagScanned(const String& uid) {
  lastUid_ = uid;
  if (!enrollmentPending_) return false;
  const String name = pendingName_;
  const bool saved = registry_.upsert(uid.c_str(), name);
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

bool AdminServer::authorize() {
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
  server_.on("/api/status", HTTP_GET, [this]() { if (authorize()) sendStatus(); });
  server_.on("/api/members", HTTP_GET, [this]() { if (authorize()) sendMembers(); });
  server_.on("/api/sessions", HTTP_GET, [this]() {
    if (!authorize()) return;
    String body = "{\"sessions\":[";
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
    body += "]}";
    server_.send(200, "application/json", body);
  });
  server_.on("/api/enroll", HTTP_POST, [this]() { if (authorize()) armEnrollment(); });
  server_.on("/api/cancel", HTTP_POST, [this]() { if (authorize()) cancelEnrollment(); });
  server_.on("/api/member", HTTP_POST, [this]() { if (authorize()) updateMember(); });
  server_.on("/api/rename", HTTP_POST, [this]() { if (authorize()) renameMember(); });
  server_.on("/api/delete", HTTP_POST, [this]() { if (authorize()) deleteMember(); });
  server_.on("/api/password", HTTP_POST, [this]() { if (authorize()) changePassword(); });
  server_.on("/api/calibration/start", HTTP_POST, [this]() { if (authorize()) startCalibration(); });
  server_.on("/api/calibration/stop", HTTP_POST, [this]() { if (authorize()) stopCalibration(); });
  server_.on("/api/audio/tone", HTTP_POST, [this]() {
    if (!authorize()) return;
    sendJsonMessage(speakerAudio_.playTestTone() ? 200 : 409,
                    speakerAudio_.connected(), speakerAudio_.connected() ? "Test tone started" : "Speaker not connected");
  });
  server_.on("/api/audio/play", HTTP_POST, [this]() {
    if (!authorize()) return;
    const bool started = speakerAudio_.playSong();
    sendJsonMessage(started ? 200 : 409, started,
                    started ? "Song started" : "Connect speaker and upload audio first");
  });
  server_.on("/api/audio/stop", HTTP_POST, [this]() {
    if (!authorize()) return;
    speakerAudio_.stop();
    sendJsonMessage(200, true, "Audio stopped");
  });
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

void AdminServer::sendStatus() {
  String body = "{\"station\":\"" + String(Config::STATION_NAME) + "\",\"ip\":\"" + address();
  body += "\",\"enrollmentPending\":" + String(enrollmentPending_ ? "true" : "false");
  body += ",\"pendingName\":\"" + jsonEscape(pendingName_) + "\",\"lastUid\":\"" + jsonEscape(lastUid_);
  body += "\",\"message\":\"" + jsonEscape(lastMessage_) + "\",\"pulsesPerGallon\":" + String(settings_.pulsesPerGallon(), 4);
  body += ",\"calibrationActive\":" + String(calibrationActive_ ? "true" : "false");
  body += ",\"calibrationPulses\":" + String(calibrationPulses_);
  body += ",\"calibrationMessage\":\"" + jsonEscape(calibrationMessage_) + "\"";
  body += ",\"speaker\":\"" + String(speakerAudio_.connectionLabel()) + "\"";
  body += ",\"audioPlayback\":\"" + String(speakerAudio_.playbackLabel()) + "\"";
  body += ",\"audioFile\":" + String(speakerAudio_.fileAvailable() ? "true" : "false") + "}";
  server_.send(200, "application/json", body);
}

void AdminServer::sendMembers() {
  String body = "{\"members\":[";
  for (size_t i = 0; i < registry_.count(); ++i) {
    if (i) body += ',';
    const char* uid = registry_.uidAt(i);
    body += "{\"uid\":\"" + jsonEscape(uid) + "\",\"name\":\"" + jsonEscape(registry_.nameAt(i));
    body += "\",\"allowance\":" + String(registry_.allowanceAt(i), 3);
    body += ",\"enabled\":" + String(registry_.enabledAt(i) ? "true" : "false");
    body += ",\"gallons\":" + String(sessions_.gallonsFor(uid), 4);
    body += ",\"sessions\":" + String(sessions_.sessionsFor(uid));
    body += ",\"pulses\":" + String(static_cast<unsigned long long>(pulseStorage_.totalFor(uid))) + '}';
  }
  body += "]}";
  server_.send(200, "application/json", body);
}

void AdminServer::armEnrollment() {
  String name = server_.arg("name"); name.trim();
  if (!registry_.healthy()) return sendJsonMessage(503, false, "Member storage unavailable");
  if (name.isEmpty() || name.length() > 32) return sendJsonMessage(400, false, "Name must be 1-32 characters");
  pendingName_ = name; enrollmentPending_ = true; lastMessage_ = "Waiting for wristband";
  sendJsonMessage(200, true, "Tap wristband on reader");
}
void AdminServer::cancelEnrollment() { enrollmentPending_ = false; pendingName_ = ""; lastMessage_ = "Enrollment cancelled"; sendJsonMessage(200, true, lastMessage_); }
void AdminServer::renameMember() { server_.sendHeader("Location", "/api/member"); updateMember(); }
void AdminServer::updateMember() {
  const String uid = server_.arg("uid"), name = server_.arg("name");
  const float allowance = server_.arg("allowance").toFloat();
  const bool enabled = server_.arg("enabled") == "1";
  if (!registry_.update(uid.c_str(), name, allowance, enabled)) return sendJsonMessage(400, false, "Member update failed");
  lastMessage_ = "Member updated"; sendJsonMessage(200, true, lastMessage_);
}
void AdminServer::deleteMember() { if (!registry_.remove(server_.arg("uid").c_str())) return sendJsonMessage(404, false, "Registration not found"); lastMessage_ = "Registration deleted"; sendJsonMessage(200, true, lastMessage_); }
void AdminServer::changePassword() { if (!settings_.setPassword(server_.arg("password"))) return sendJsonMessage(400, false, "Password must be 8-64 characters"); sendJsonMessage(200, true, "Password changed"); }
void AdminServer::startCalibration() { calibrationStartRequested_ = true; calibrationMessage_ = "Starting…"; sendJsonMessage(200, true, "Calibration requested"); }
void AdminServer::stopCalibration() { const float gallons = server_.arg("gallons").toFloat(); if (gallons <= 0.0F) return sendJsonMessage(400, false, "Enter a known volume"); calibrationKnownGallons_ = gallons; calibrationStopRequested_ = true; sendJsonMessage(200, true, "Stop requested"); }

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
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (audioUploadFile_) audioUploadFile_.close();
    SD.remove(Config::AUDIO_PATH);
    audioUploadFailed_ = true;
  }
}

void AdminServer::sendJsonMessage(int code, bool ok, const String& message) { server_.send(code, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + jsonEscape(message) + "\"}"); }
String AdminServer::jsonEscape(const String& value) { String escaped; escaped.reserve(value.length() + 8); for (size_t i=0;i<value.length();++i){const char c=value[i]; if(c=='"'||c=='\\')escaped+='\\'; if(c=='\n')escaped+="\\n"; else if(c>=32)escaped+=c;} return escaped; }
