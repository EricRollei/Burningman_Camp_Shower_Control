#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "I2cHub.h"
#include "RfidReader.h"

// Reuse the production-tested RFID2 and PaHUB drivers without compiling the
// rest of the shower controller.
#include "../../shower-controller/src/I2cHub.cpp"
#include "../../shower-controller/src/RfidReader.cpp"

namespace {

constexpr uint8_t I2C_SDA = 32;
constexpr uint8_t I2C_SCL = 33;
constexpr uint32_t I2C_FREQUENCY = 100000;
constexpr uint8_t RFID_ADDRESS = 0x28;
constexpr uint8_t SD_SCK = 18;
constexpr uint8_t SD_MISO = 38;
constexpr uint8_t SD_MOSI = 23;
constexpr uint8_t SD_CS = 4;
constexpr uint32_t SD_FREQUENCY = 10000000;
constexpr char MEMBER_PATH[] = "/MEMBERS.CSV";
constexpr char VERSION_PATH[] = "/MEMBERS.VER";
constexpr char AP_NAME[] = "CampTagProgrammer";
constexpr char AP_PASSWORD[] = "dustybutthole";
constexpr size_t MAX_MEMBERS = 100;
constexpr size_t UID_SIZE = 21;
constexpr size_t NAME_SIZE = 33;

struct Member {
  char uid[UID_SIZE] = {0};
  char name[NAME_SIZE] = {0};
  float allowance = 0.0F;
  bool enabled = true;
};

Member members[MAX_MEMBERS];
size_t memberCount = 0;
uint32_t registryVersion = 0;
bool sdReady = false;
bool rfidReady = false;
bool hubReady = false;
int8_t rfidChannel = -1;
uint8_t hubAddress = 0;
uint32_t lastReaderProbeMs = 0;
uint32_t lastRfidPollMs = 0;
uint32_t lastScanMs = 0;
String lastScannedUid;
String pendingName;
String statusMessage = "Starting...";
String serialCommand;
bool screenDirty = true;

I2cHub i2cHub;
RfidReader rfid;
WebServer server(80);

String cleanName(const String& requested) {
  String input = requested;
  input.trim();
  String output;
  output.reserve(NAME_SIZE);
  for (size_t i = 0; i < input.length() && output.length() < NAME_SIZE - 1; ++i) {
    const char value = input[i];
    if (value == ',') output += ' ';
    else if (value >= 32 && value <= 126) output += value;
  }
  output.trim();
  return output;
}

String jsonEscape(const String& value) {
  String output;
  output.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '\\' || c == '"') { output += '\\'; output += c; }
    else if (c == '\n') output += "\\n";
    else if (static_cast<uint8_t>(c) >= 32) output += c;
  }
  return output;
}

int findMember(const char* uid) {
  if (uid == nullptr) return -1;
  for (size_t i = 0; i < memberCount; ++i) {
    if (strcmp(members[i].uid, uid) == 0) return static_cast<int>(i);
  }
  return -1;
}

bool writeVersion() {
  SD.remove(VERSION_PATH);
  File file = SD.open(VERSION_PATH, FILE_WRITE);
  if (!file) return false;
  file.println(static_cast<unsigned long>(registryVersion));
  file.flush();
  file.close();
  return true;
}

bool saveRegistry() {
  constexpr char tempPath[] = "/MEMBERS.TMP";
  constexpr char backupPath[] = "/MEMBERS.BAK";
  SD.remove(tempPath);
  SD.remove(backupPath);
  File file = SD.open(tempPath, FILE_WRITE);
  if (!file) return false;
  file.println("uid,name,allowance_gallons,enabled");
  for (size_t i = 0; i < memberCount; ++i) {
    file.printf("%s,%s,%.3f,%u\n", members[i].uid, members[i].name,
                members[i].allowance, members[i].enabled ? 1 : 0);
  }
  file.flush();
  file.close();
  if (SD.exists(MEMBER_PATH) && !SD.rename(MEMBER_PATH, backupPath)) {
    SD.remove(tempPath);
    return false;
  }
  if (!SD.rename(tempPath, MEMBER_PATH)) {
    if (SD.exists(backupPath)) SD.rename(backupPath, MEMBER_PATH);
    SD.remove(tempPath);
    return false;
  }
  SD.remove(backupPath);
  return writeVersion();
}

bool commitRegistry() {
  const uint32_t previous = registryVersion;
  ++registryVersion;
  if (saveRegistry()) return true;
  registryVersion = previous;
  return false;
}

bool loadRegistry() {
  memberCount = 0;
  if (!SD.exists(MEMBER_PATH)) {
    registryVersion = 0;
    return saveRegistry();
  }
  File file = SD.open(MEMBER_PATH, FILE_READ);
  if (!file) return false;
  bool firstLine = true;
  while (file.available() && memberCount < MAX_MEMBERS) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (firstLine) { firstLine = false; continue; }
    const int comma1 = line.indexOf(',');
    const int comma2 = comma1 >= 0 ? line.indexOf(',', comma1 + 1) : -1;
    const int comma3 = comma2 >= 0 ? line.indexOf(',', comma2 + 1) : -1;
    if (comma1 <= 0) continue;
    const String uid = line.substring(0, comma1);
    const String name = cleanName(line.substring(comma1 + 1, comma2 >= 0 ? comma2 : line.length()));
    if (uid.isEmpty() || uid.length() >= UID_SIZE || name.isEmpty()) continue;
    Member& member = members[memberCount++];
    strlcpy(member.uid, uid.c_str(), sizeof(member.uid));
    strlcpy(member.name, name.c_str(), sizeof(member.name));
    member.allowance = comma2 >= 0 ? line.substring(comma2 + 1, comma3 >= 0 ? comma3 : line.length()).toFloat() : 0.0F;
    if (!isfinite(member.allowance) || member.allowance < 0.0F) member.allowance = 0.0F;
    member.enabled = comma3 < 0 || line.substring(comma3 + 1).toInt() != 0;
  }
  file.close();
  File versionFile;
  if (SD.exists(VERSION_PATH)) versionFile = SD.open(VERSION_PATH, FILE_READ);
  if (versionFile) {
    registryVersion = static_cast<uint32_t>(strtoul(versionFile.readStringUntil('\n').c_str(), nullptr, 10));
    versionFile.close();
  } else if (!writeVersion()) {
    return false;
  }
  return true;
}

String uidToHex(const uint8_t* uid, int length) {
  char hex[UID_SIZE] = {0};
  for (int i = 0; i < length && i < 10; ++i) {
    snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", uid[i]);
  }
  return String(hex);
}

void setStatus(const String& message) {
  statusMessage = message;
  screenDirty = true;
  Serial.printf("[STATUS] %s\n", message.c_str());
}

void drawCentered(const String& text, int y, int size, uint16_t color) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text, M5.Display.width() / 2, y);
}

void drawScreen() {
  if (!screenDirty) return;
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  drawCentered("TAG PROGRAMMER", 22, 2, TFT_CYAN);
  drawCentered(rfidReady ? "RFID READY" : "RFID NOT FOUND", 54, 2,
               rfidReady ? TFT_GREEN : TFT_ORANGE);
  drawCentered(sdReady ? String(memberCount) + " tags saved" : "SD CARD REQUIRED", 82, 2,
               sdReady ? TFT_WHITE : TFT_RED);
  if (!pendingName.isEmpty()) {
    drawCentered("ARMED FOR", 112, 1, TFT_LIGHTGREY);
    drawCentered(pendingName, 133, 2, TFT_YELLOW);
    drawCentered("Tap wristband now", 164, 2, TFT_GREEN);
  } else {
    drawCentered(statusMessage.substring(0, 38), 122, 1, TFT_WHITE);
    if (!lastScannedUid.isEmpty()) drawCentered(lastScannedUid, 148, 2, TFT_YELLOW);
  }
  drawCentered("WiFi: CampTagProgrammer", 198, 1, TFT_CYAN);
  drawCentered("Open http://192.168.4.1", 218, 1, TFT_LIGHTGREY);
  M5.Display.endWrite();
  screenDirty = false;
}

void discoverReader() {
  rfidReady = false;
  hubReady = false;
  rfidChannel = -1;
  for (uint8_t address = 0x70; address <= 0x77; ++address) {
    if (!i2cHub.begin(Wire, address)) continue;
    hubReady = true;
    hubAddress = address;
    rfidChannel = i2cHub.findDevice(RFID_ADDRESS);
    if (rfidChannel >= 0) {
      rfidReady = rfid.begin(Wire, RFID_ADDRESS, &i2cHub, rfidChannel);
    }
    break;
  }
  if (!hubReady) {
    Wire.beginTransmission(RFID_ADDRESS);
    if (Wire.endTransmission() == 0) rfidReady = rfid.begin(Wire, RFID_ADDRESS);
  }
  Serial.printf("[I2C] mode=%s hub=0x%02X channel=%d rfid=%s version=0x%02X\n",
                hubReady ? "pahub" : "direct", hubAddress, rfidChannel,
                rfidReady ? "ready" : "missing", rfidReady ? rfid.version() : 0);
  screenDirty = true;
}

void handleScannedUid(const String& uid) {
  lastScannedUid = uid;
  lastScanMs = millis();
  const int existing = findMember(uid.c_str());
  if (pendingName.isEmpty()) {
    if (existing >= 0) setStatus("Verified: " + String(members[existing].name));
    else setStatus("Unknown tag - arm a name first");
    return;
  }
  const String name = pendingName;
  pendingName = "";
  if (!sdReady) { setStatus("Cannot save: insert SD card"); return; }
  if (existing >= 0) {
    setStatus("Already enrolled as " + String(members[existing].name));
    return;
  }
  if (memberCount >= MAX_MEMBERS) { setStatus("Registry full (100 tags)"); return; }
  Member& member = members[memberCount++];
  strlcpy(member.uid, uid.c_str(), sizeof(member.uid));
  strlcpy(member.name, name.c_str(), sizeof(member.name));
  member.allowance = 0.0F;
  member.enabled = true;
  if (!commitRegistry()) {
    --memberCount;
    setStatus("SD save failed - tag not enrolled");
    return;
  }
  setStatus("Saved " + name);
}

void pollRfid() {
  if (!rfidReady) {
    if (millis() - lastReaderProbeMs >= 5000) {
      lastReaderProbeMs = millis();
      discoverReader();
    }
    return;
  }
  if (millis() - lastRfidPollMs < 80) return;
  lastRfidPollMs = millis();
  uint8_t uidBytes[10];
  const int length = rfid.readUid(uidBytes, sizeof(uidBytes));
  if (length <= 0) return;
  const String uid = uidToHex(uidBytes, length);
  rfid.haltTag();
  if (uid == lastScannedUid && millis() - lastScanMs < 2500) return;
  Serial.printf("[SCAN] uid=%s\n", uid.c_str());
  handleScannedUid(uid);
}

void printBackup() {
  Serial.println("[BACKUP] BEGIN MEMBERS.CSV");
  Serial.println("uid,name,allowance_gallons,enabled");
  for (size_t i = 0; i < memberCount; ++i) {
    Serial.printf("%s,%s,%.3f,%u\n", members[i].uid, members[i].name,
                  members[i].allowance, members[i].enabled ? 1 : 0);
  }
  Serial.println("[BACKUP] END MEMBERS.CSV");
  Serial.println("[BACKUP] BEGIN MEMBERS.VER");
  Serial.println(static_cast<unsigned long>(registryVersion));
  Serial.println("[BACKUP] END MEMBERS.VER");
  Serial.printf("[BACKUP] COMPLETE members=%u version=%lu\n",
                static_cast<unsigned>(memberCount),
                static_cast<unsigned long>(registryVersion));
}

void handleSerial() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n' || value == '\r') {
      serialCommand.trim();
      String lowered = serialCommand;
      lowered.toLowerCase();
      if (lowered == "backup") {
        printBackup();
      } else if (lowered.startsWith("rename ")) {
        const int nameSeparator = serialCommand.indexOf(' ', 7);
        String uid = nameSeparator > 7 ? serialCommand.substring(7, nameSeparator) : "";
        const String name = cleanName(nameSeparator > 0 ? serialCommand.substring(nameSeparator + 1) : "");
        uid.toUpperCase();
        const int index = findMember(uid.c_str());
        if (index < 0 || name.isEmpty()) {
          Serial.println("[RENAME] failed: use rename <UID> <name>");
        } else {
          const Member previous = members[index];
          strlcpy(members[index].name, name.c_str(), sizeof(members[index].name));
          if (commitRegistry()) {
            setStatus("Updated " + name);
            Serial.printf("[RENAME] OK uid=%s old=%s new=%s version=%lu\n",
                          uid.c_str(), previous.name, members[index].name,
                          static_cast<unsigned long>(registryVersion));
          } else {
            members[index] = previous;
            Serial.println("[RENAME] failed: SD save error");
          }
        }
      } else if (!serialCommand.isEmpty()) {
        Serial.println("[HELP] commands: backup, rename <UID> <name>");
      }
      serialCommand = "";
    } else if (serialCommand.length() < 32) {
      serialCommand += value;
    }
  }
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Camp Tag Programmer</title><style>
:root{font-family:system-ui;color:#effffb;background:#071512}body{max-width:760px;margin:auto;padding:18px}h1{color:#70ead3}section{background:#102621;border:1px solid #31534d;border-radius:12px;padding:16px;margin:14px 0}input{box-sizing:border-box;width:100%;padding:12px;margin:7px 0;background:#071512;color:white;border:1px solid #52766e;border-radius:7px}button,a.button{display:inline-block;padding:11px 14px;margin:6px 5px 0 0;border:0;border-radius:7px;background:#51d7be;color:#06110f;font-weight:700;text-decoration:none}.secondary{background:#aac8c1!important}.danger{background:#ff8b7c!important}.status{border-left:4px solid #51d7be}.member{padding:12px 0;border-top:1px solid #31534d}.uid{font:12px ui-monospace;color:#9bb9b2}.row{display:flex;justify-content:space-between;gap:10px;align-items:start}.muted{color:#9bb9b2}.bad{color:#ff9b8e}.good{color:#72e4a1}</style></head>
<body><h1>Camp Tag Programmer</h1><section class="status"><strong id="hardware">Loading…</strong><p id="message"></p></section>
<section><h2>Enroll next wristband</h2><label>Member name</label><input id="name" maxlength="32" placeholder="e.g. Dusty River"><button id="arm">Arm next scan</button><button id="cancel" class="secondary">Cancel</button><p class="muted">After arming, tap exactly one wristband. Scan without arming to verify a saved wristband.</p></section>
<section><div class="row"><h2>Saved members</h2><strong id="count"></strong></div><div id="members"></div></section>
<section><h2>Production files</h2><a class="button" href="/download/members">Download registry</a><a class="button secondary" href="/download/version">Download version</a><p class="muted">Copy both files to each production SD card while its controller is powered off.</p></section>
<script>
const $=s=>document.querySelector(s),esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function post(path,data){const body=new URLSearchParams(data);const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const j=await r.json();if(!r.ok)throw Error(j.message||'Request failed');return j}
async function refresh(){try{const s=await(await fetch('/api/status',{cache:'no-store'})).json();$('#hardware').innerHTML=`<span class="${s.rfid&&s.sd?'good':'bad'}">RFID ${s.rfid?'ready':'missing'} · SD ${s.sd?'ready':'missing'}</span>`;$('#message').textContent=s.pending?`Waiting for ${s.pending} — tap wristband now.`:s.message;$('#count').textContent=`${s.members.length} / 100`;$('#members').innerHTML=s.members.length?s.members.map(m=>`<div class="member"><div class="row"><div><strong>${esc(m.name)}</strong> ${m.enabled?'':'<span class="bad">disabled</span>'}<div class="uid">${esc(m.uid)}</div></div><div><button class="secondary" onclick="editMember('${m.uid}','${encodeURIComponent(m.name)}',${m.allowance},${m.enabled})">Edit</button><button class="danger" onclick="removeMember('${m.uid}')">Delete</button></div></div></div>`).join(''):'<span class="muted">No tags enrolled yet.</span>'}catch(e){$('#hardware').textContent='Programmer not responding'} }
$('#arm').onclick=async()=>{const name=$('#name').value.trim();if(!name)return alert('Enter a member name first.');try{await post('/api/arm',{name});refresh()}catch(e){alert(e.message)}};
$('#cancel').onclick=async()=>{await post('/api/cancel',{});refresh()};
async function editMember(uid,name,allowance,enabled){name=decodeURIComponent(name);const n=prompt('Member name',name);if(!n)return;const a=prompt('Custom shower limit in gallons (0 = station limit)',allowance);if(a===null)return;const en=confirm('Allow this wristband to start sessions?');try{await post('/api/member',{uid,name:n,allowance:a,enabled:en?'1':'0'});refresh()}catch(e){alert(e.message)}}
async function removeMember(uid){if(!confirm('Delete this wristband registration?'))return;try{await post('/api/delete',{uid});refresh()}catch(e){alert(e.message)}}
setInterval(refresh,750);refresh();
</script></body></html>)HTML";

void sendJsonMessage(int code, bool ok, const String& message) {
  server.send(code, "application/json", String("{\"ok\":") + (ok ? "true" : "false") +
              ",\"message\":\"" + jsonEscape(message) + "\"}");
}

void setupWeb() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGE); });
  server.on("/api/status", HTTP_GET, []() {
    String body = "{\"rfid\":" + String(rfidReady ? "true" : "false") +
                  ",\"sd\":" + String(sdReady ? "true" : "false") +
                  ",\"message\":\"" + jsonEscape(statusMessage) +
                  "\",\"pending\":\"" + jsonEscape(pendingName) + "\",\"members\":[";
    for (size_t i = 0; i < memberCount; ++i) {
      if (i) body += ',';
      body += "{\"uid\":\"" + String(members[i].uid) + "\",\"name\":\"" +
              jsonEscape(members[i].name) + "\",\"allowance\":" + String(members[i].allowance, 3) +
              ",\"enabled\":" + String(members[i].enabled ? "true" : "false") + '}';
    }
    body += "]}";
    server.send(200, "application/json", body);
  });
  server.on("/api/arm", HTTP_POST, []() {
    if (!sdReady) return sendJsonMessage(503, false, "Insert a microSD card first");
    if (!rfidReady) return sendJsonMessage(503, false, "RFID2 reader not found");
    const String name = cleanName(server.arg("name"));
    if (name.isEmpty()) return sendJsonMessage(400, false, "Member name is required");
    pendingName = name;
    setStatus("Waiting for wristband");
    sendJsonMessage(200, true, statusMessage);
  });
  server.on("/api/cancel", HTTP_POST, []() {
    pendingName = "";
    setStatus("Enrollment cancelled");
    sendJsonMessage(200, true, statusMessage);
  });
  server.on("/api/member", HTTP_POST, []() {
    const int index = findMember(server.arg("uid").c_str());
    const String name = cleanName(server.arg("name"));
    const float allowance = server.arg("allowance").toFloat();
    if (index < 0 || name.isEmpty() || allowance < 0.0F || !isfinite(allowance)) {
      return sendJsonMessage(400, false, "Invalid member update");
    }
    const Member previous = members[index];
    strlcpy(members[index].name, name.c_str(), sizeof(members[index].name));
    members[index].allowance = allowance;
    members[index].enabled = server.arg("enabled") == "1";
    if (!commitRegistry()) { members[index] = previous; return sendJsonMessage(500, false, "SD save failed"); }
    setStatus("Updated " + name);
    sendJsonMessage(200, true, statusMessage);
  });
  server.on("/api/delete", HTTP_POST, []() {
    const int index = findMember(server.arg("uid").c_str());
    if (index < 0) return sendJsonMessage(404, false, "Registration not found");
    const Member removed = members[index];
    const size_t previousCount = memberCount;
    for (size_t i = static_cast<size_t>(index); i + 1 < memberCount; ++i) members[i] = members[i + 1];
    --memberCount;
    if (!commitRegistry()) {
      for (size_t i = memberCount; i > static_cast<size_t>(index); --i) members[i] = members[i - 1];
      members[index] = removed;
      memberCount = previousCount;
      return sendJsonMessage(500, false, "SD save failed");
    }
    setStatus("Registration deleted");
    sendJsonMessage(200, true, statusMessage);
  });
  server.on("/download/members", HTTP_GET, []() {
    File file = SD.open(MEMBER_PATH, FILE_READ);
    if (!file) return server.send(404, "text/plain", "MEMBERS.CSV unavailable");
    server.sendHeader("Content-Disposition", "attachment; filename=MEMBERS.CSV");
    server.streamFile(file, "text/csv");
    file.close();
  });
  server.on("/download/version", HTTP_GET, []() {
    File file = SD.open(VERSION_PATH, FILE_READ);
    if (!file) return server.send(404, "text/plain", "MEMBERS.VER unavailable");
    server.sendHeader("Content-Disposition", "attachment; filename=MEMBERS.VER");
    server.streamFile(file, "text/plain");
    file.close();
  });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  auto config = M5.config();
  M5.begin(config);
  M5.Power.setExtOutput(true);
  delay(150);
  M5.Display.setRotation(1);
  M5.Display.setTextWrap(false);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdReady = SD.begin(SD_CS, SPI, SD_FREQUENCY) && SD.cardType() != CARD_NONE;
  if (sdReady) sdReady = loadRegistry();

  Wire.end();
  delay(10);
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQUENCY);
  discoverReader();

  WiFi.mode(WIFI_AP);
  const bool wifiReady = WiFi.softAP(AP_NAME, AP_PASSWORD);
  setupWeb();
  setStatus(sdReady ? "Ready - enter a name in browser" : "Insert microSD card and reboot");
  Serial.printf("[BOOT] board=%u ext5v=%s sd=%s members=%u version=%lu rfid=%s wifi=%s\n",
                static_cast<unsigned>(M5.getBoard()), M5.Power.getExtOutput() ? "on" : "off",
                sdReady ? "ready" : "missing", static_cast<unsigned>(memberCount),
                static_cast<unsigned long>(registryVersion), rfidReady ? "ready" : "missing",
                wifiReady ? "ready" : "failed");
  Serial.printf("[WEB] ssid=%s password=%s address=http://%s/\n", AP_NAME, AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
  Serial.println("[HELP] USB commands: backup, rename <UID> <name>");
  drawScreen();
}

void loop() {
  M5.update();
  server.handleClient();
  handleSerial();
  pollRfid();
  drawScreen();
  delay(2);
}
