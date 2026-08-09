#include "AdminServer.h"

#include <WiFi.h>

#include "Config.h"

namespace {

const char ADMIN_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Camp Shower Setup</title>
<style>
:root{color-scheme:dark;--bg:#111b1b;--card:#1b2928;--ink:#f4f1e8;--muted:#a9bbb7;--a:#46d6a2;--danger:#ff766d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif}
main{max-width:720px;margin:auto;padding:20px}.eyebrow{color:var(--a);font-weight:700;text-transform:uppercase;letter-spacing:.08em}
h1{font-size:clamp(30px,8vw,48px);margin:.2em 0}p{color:var(--muted)}.card{background:var(--card);border-radius:16px;padding:18px;margin:16px 0}
label{display:block;font-weight:700;margin-bottom:8px}input{width:100%;font:inherit;padding:13px;border:1px solid #526663;border-radius:10px;background:#0d1716;color:var(--ink)}
button{font:inherit;font-weight:700;border:0;border-radius:10px;padding:12px 15px;background:var(--a);color:#092019;cursor:pointer}
button.secondary{background:#344744;color:var(--ink)}button.danger{background:transparent;color:var(--danger);border:1px solid var(--danger)}
.actions{display:flex;gap:10px;margin-top:12px;flex-wrap:wrap}.status{padding:12px;border-left:4px solid var(--a);background:#13211f;color:var(--ink)}
.member{display:grid;grid-template-columns:1fr auto;gap:10px;padding:12px 0;border-top:1px solid #344744}.member:first-child{border-top:0}
.uid{font:12px ui-monospace,monospace;color:var(--muted)}.total{color:var(--muted);font-size:14px}small{color:var(--muted)}
</style>
</head>
<body><main>
<div class="eyebrow">Local controller</div><h1>Camp Shower Setup</h1>
<p>Enroll wristbands against this controller. No internet connection is required.</p>
<section class="card"><label for="name">Member name</label><input id="name" maxlength="32" autocomplete="off" placeholder="e.g. Dusty River">
<div class="actions"><button id="arm">Enroll next tag</button><button id="cancel" class="secondary">Cancel</button></div></section>
<div id="status" class="status">Connecting…</div>
<section class="card"><h2>Registered tags</h2><div id="members"><small>Loading…</small></div></section>
</main>
<script>
const $=s=>document.querySelector(s), esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function post(path,data={}){const body=new URLSearchParams(data);const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});return r.json()}
async function refresh(){try{const [s,m]=await Promise.all([fetch('/api/status').then(r=>r.json()),fetch('/api/members').then(r=>r.json())]);
$('#status').textContent=s.enrollmentPending?`Waiting for ${s.pendingName}'s tag — tap it on the RFID reader.`:`${s.message}${s.lastUid?' · Last tag '+s.lastUid:''}`;
$('#members').innerHTML=m.members.length?m.members.map(x=>`<div class="member"><div><strong>${esc(x.name)}</strong><div class="uid">${esc(x.uid)}</div><div class="total">${x.pulses} recorded pulses</div></div><div class="actions"><button class="secondary" onclick="renameTag('${encodeURIComponent(x.uid)}')">Rename</button><button class="danger" onclick="removeTag('${encodeURIComponent(x.uid)}')">Delete</button></div></div>`).join(''):'<small>No tags enrolled yet.</small>';
}catch(e){$('#status').textContent='Controller unavailable — retrying…'}}
$('#arm').onclick=async()=>{const name=$('#name').value.trim();if(!name){$('#status').textContent='Enter a member name first.';return}await post('/api/enroll',{name});refresh()};
$('#cancel').onclick=async()=>{await post('/api/cancel');refresh()};
async function removeTag(uid){if(!confirm('Delete this registration? Usage logs remain intact.'))return;await post('/api/delete',{uid:decodeURIComponent(uid)});refresh()}
async function renameTag(uid){const name=prompt('New member name');if(!name||!name.trim())return;await post('/api/rename',{uid:decodeURIComponent(uid),name:name.trim()});refresh()}
refresh();setInterval(refresh,1500);
</script></body></html>
)HTML";

}  // namespace

AdminServer::AdminServer(MemberRegistry& registry, const PulseStorage& storage)
    : registry_(registry), storage_(storage) {}

bool AdminServer::begin() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAP(Config::WIFI_AP_NAME, Config::WIFI_AP_PASSWORD)) {
    return false;
  }
  configureRoutes();
  server_.begin();
  started_ = true;
  return true;
}

void AdminServer::handle() {
  if (started_) server_.handleClient();
}

bool AdminServer::onTagScanned(const String& uid) {
  lastUid_ = uid;
  if (!enrollmentPending_) return false;

  const String enrolledName = pendingName_;
  const bool saved = registry_.upsert(uid.c_str(), enrolledName);
  enrollmentPending_ = false;
  pendingName_ = "";
  lastMessage_ = saved ? enrolledName + " enrolled" : "Enrollment save failed";
  return true;
}

String AdminServer::address() const {
  return started_ ? WiFi.softAPIP().toString() : String("offline");
}

void AdminServer::configureRoutes() {
  server_.on("/", HTTP_GET, [this]() {
    server_.send_P(200, "text/html", ADMIN_PAGE);
  });
  server_.on("/api/status", HTTP_GET, [this]() { sendStatus(); });
  server_.on("/api/members", HTTP_GET, [this]() { sendMembers(); });
  server_.on("/api/enroll", HTTP_POST, [this]() { armEnrollment(); });
  server_.on("/api/cancel", HTTP_POST, [this]() { cancelEnrollment(); });
  server_.on("/api/rename", HTTP_POST, [this]() { renameMember(); });
  server_.on("/api/delete", HTTP_POST, [this]() { deleteMember(); });
  server_.onNotFound([this]() {
    if (server_.uri().startsWith("/api/")) {
      sendJsonMessage(404, false, "Not found");
    } else {
      server_.sendHeader("Location", "/", true);
      server_.send(302, "text/plain", "");
    }
  });
}

void AdminServer::sendStatus() {
  String body = "{\"station\":\"";
  body += Config::STATION_NAME;
  body += "\",\"ip\":\"" + address() + "\",\"enrollmentPending\":";
  body += enrollmentPending_ ? "true" : "false";
  body += ",\"pendingName\":\"" + jsonEscape(pendingName_) + "\"";
  body += ",\"lastUid\":\"" + jsonEscape(lastUid_) + "\"";
  body += ",\"message\":\"" + jsonEscape(lastMessage_) + "\"}";
  server_.send(200, "application/json", body);
}

void AdminServer::sendMembers() {
  String body = "{\"members\":[";
  for (size_t i = 0; i < registry_.count(); ++i) {
    if (i > 0) body += ',';
    const char* uid = registry_.uidAt(i);
    body += "{\"uid\":\"" + jsonEscape(uid) + "\",\"name\":\"";
    body += jsonEscape(registry_.nameAt(i));
    body += "\",\"pulses\":";
    body += String(static_cast<unsigned long long>(storage_.totalFor(uid)));
    body += '}';
  }
  body += "]}";
  server_.send(200, "application/json", body);
}

void AdminServer::armEnrollment() {
  String name = server_.arg("name");
  name.trim();
  if (!registry_.healthy()) {
    sendJsonMessage(503, false, "Member storage unavailable");
    return;
  }
  if (name.isEmpty() || name.length() > 32) {
    sendJsonMessage(400, false, "Name must be 1-32 characters");
    return;
  }
  pendingName_ = name;
  enrollmentPending_ = true;
  lastMessage_ = "Waiting for tag";
  sendJsonMessage(200, true, "Tap tag on reader");
}

void AdminServer::cancelEnrollment() {
  enrollmentPending_ = false;
  pendingName_ = "";
  lastMessage_ = "Enrollment cancelled";
  sendJsonMessage(200, true, lastMessage_);
}

void AdminServer::renameMember() {
  const String uid = server_.arg("uid");
  const String name = server_.arg("name");
  if (registry_.nameFor(uid.c_str()) == nullptr) {
    sendJsonMessage(404, false, "Registration not found");
    return;
  }
  if (!registry_.upsert(uid.c_str(), name)) {
    sendJsonMessage(400, false, "Rename failed");
    return;
  }
  lastMessage_ = "Member renamed";
  sendJsonMessage(200, true, lastMessage_);
}

void AdminServer::deleteMember() {
  const String uid = server_.arg("uid");
  if (!registry_.remove(uid.c_str())) {
    sendJsonMessage(404, false, "Registration not found");
    return;
  }
  lastMessage_ = "Registration deleted";
  sendJsonMessage(200, true, lastMessage_);
}

void AdminServer::sendJsonMessage(int code, bool ok, const String& message) {
  String body = String("{\"ok\":") + (ok ? "true" : "false") +
                ",\"message\":\"" + jsonEscape(message) + "\"}";
  server_.send(code, "application/json", body);
}

String AdminServer::jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\\') escaped += '\\';
    if (c == '\n') {
      escaped += "\\n";
    } else if (c >= 32) {
      escaped += c;
    }
  }
  return escaped;
}
