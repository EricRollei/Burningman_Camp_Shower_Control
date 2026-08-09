#include <M5Unified.h>
#include <Wire.h>

#include "AdminServer.h"
#include "Config.h"
#include "FlowMeter.h"
#include "MemberRegistry.h"
#include "PulseStorage.h"
#include "RelayController.h"
#include "RfidReader.h"

namespace {

FlowMeter flow;
PulseStorage storage;
MemberRegistry members;
AdminServer admin(members, storage);
RelayController relays;
RfidReader rfid;

char activeUid[21] = {0};
char activeName[33] = {0};
char lastScannedUid[21] = {0};
uint32_t lastScanMs = 0;
uint32_t lastRfidPollMs = 0;
uint32_t lastLogMs = 0;
uint32_t lastSensorTotal = 0;
uint32_t pendingPulses = 0;
uint32_t sessionPulses = 0;
bool rfidReady = false;
bool relayReady = false;
bool screenDirty = true;
String statusText = "Starting...";
String serialCommand;

bool hasActiveTag() { return activeUid[0] != '\0'; }

void setStatus(const String& text) {
  statusText = text;
  screenDirty = true;
  Serial.printf("[STATUS] %s\n", text.c_str());
}

String uidToHex(const uint8_t* uid, int length) {
  char hex[21] = {0};
  for (int i = 0; i < length && i < 10; ++i) {
    snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", uid[i]);
  }
  return String(hex);
}

bool devicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void drawRelayButton(uint8_t channel, int x, int y, int width, int height) {
  const bool on = relays.isOn(channel);
  const uint16_t fill = on ? TFT_DARKGREEN : TFT_DARKGREY;
  M5.Display.fillRoundRect(x, y, width, height, 8, fill);
  M5.Display.drawRoundRect(x, y, width, height, 8,
                           on ? TFT_GREEN : TFT_LIGHTGREY);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, fill);
  M5.Display.setTextSize(2);
  char label[20];
  snprintf(label, sizeof(label), "RELAY %u %s", channel, on ? "ON" : "OFF");
  M5.Display.drawString(label, x + width / 2, y + height / 2);
  M5.Display.setTextDatum(top_left);
}

void drawScreen() {
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.drawString("SHOWER HARDWARE MVP", 8, 6);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(hasActiveTag() ? "ACTIVE TAG" : "SCAN A TAG", 8, 34);
  M5.Display.setTextColor(hasActiveTag() ? TFT_GREEN : TFT_YELLOW);
  M5.Display.setTextSize(2);
  M5.Display.drawString(hasActiveTag() ? activeUid : "--", 8, 49);

  if (hasActiveTag()) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.drawString(activeName, 166, 52);
  }

  const uint64_t persisted = hasActiveTag() ? storage.totalFor(activeUid) : 0;
  char pulses[64];
  snprintf(pulses, sizeof(pulses), "session %lu | tag %llu",
           static_cast<unsigned long>(sessionPulses),
           static_cast<unsigned long long>(persisted + pendingPulses));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(pulses, 8, 73);

  const uint16_t sdColor = storage.healthy() ? TFT_GREEN : TFT_RED;
  const uint16_t i2cColor = (rfidReady && relayReady) ? TFT_GREEN : TFT_RED;
  M5.Display.setTextColor(sdColor);
  M5.Display.drawString(storage.healthy() ? "SD OK" : "SD FAIL", 8, 88);
  M5.Display.setTextColor(i2cColor);
  M5.Display.drawString((rfidReady && relayReady) ? "I2C OK" : "I2C CHECK", 65,
                        88);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString(statusText, 130, 88);

  if (hasActiveTag()) {
    M5.Display.fillRoundRect(240, 34, 72, 46, 6, TFT_MAROON);
    M5.Display.drawRoundRect(240, 34, 72, 46, 6, TFT_RED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
    M5.Display.setTextSize(1);
    M5.Display.drawString("END TAG", 276, 57);
    M5.Display.setTextDatum(top_left);
  }

  drawRelayButton(1, 8, 108, 148, 54);
  drawRelayButton(2, 164, 108, 148, 54);
  drawRelayButton(3, 8, 174, 148, 54);
  drawRelayButton(4, 164, 174, 148, 54);
  M5.Display.endWrite();
  screenDirty = false;
}

bool flushPulses() {
  if (!hasActiveTag() || pendingPulses == 0) return true;
  const uint32_t delta = pendingPulses;
  if (!storage.recordPulses(activeUid, delta)) {
    setStatus("SD write failed");
    return false;
  }
  pendingPulses = 0;
  Serial.printf("[FLOW] uid=%s delta=%lu session=%lu total=%llu\n", activeUid,
                static_cast<unsigned long>(delta),
                static_cast<unsigned long>(sessionPulses),
                static_cast<unsigned long long>(storage.totalFor(activeUid)));
  screenDirty = true;
  return true;
}

void endActiveTag() {
  if (!hasActiveTag()) return;
  flushPulses();
  storage.endTag(activeUid);
  Serial.printf("[RFID] end uid=%s session_pulses=%lu\n", activeUid,
                static_cast<unsigned long>(sessionPulses));
  activeUid[0] = '\0';
  activeName[0] = '\0';
  pendingPulses = 0;
  sessionPulses = 0;
  setStatus("Tag ended");
}

void selectTag(const String& uid) {
  const bool changing = hasActiveTag() && uid != activeUid;
  if (changing) endActiveTag();
  if (!changing && hasActiveTag()) flushPulses();

  strlcpy(activeUid, uid.c_str(), sizeof(activeUid));
  const char* registeredName = members.nameFor(activeUid);
  strlcpy(activeName, registeredName != nullptr ? registeredName : "Unregistered",
          sizeof(activeName));
  pendingPulses = 0;
  sessionPulses = 0;
  if (!storage.selectTag(activeUid)) {
    setStatus("Tag active; SD failed");
  } else {
    setStatus("Counting flow pulses");
  }
  Serial.printf("[RFID] selected uid=%s restored_total=%llu\n", activeUid,
                static_cast<unsigned long long>(storage.totalFor(activeUid)));
  screenDirty = true;
}

void pollFlow() {
  const uint32_t sensorTotal = flow.totalPulses();
  const uint32_t delta = sensorTotal - lastSensorTotal;
  lastSensorTotal = sensorTotal;
  if (delta == 0) return;

  if (hasActiveTag()) {
    pendingPulses += delta;
    sessionPulses += delta;
    screenDirty = true;
  } else {
    Serial.printf("[FLOW] unassigned=%lu sensor_total=%lu\n",
                  static_cast<unsigned long>(delta),
                  static_cast<unsigned long>(sensorTotal));
  }
}

void pollRfid() {
  if (!rfidReady || millis() - lastRfidPollMs < 80) return;
  lastRfidPollMs = millis();

  uint8_t uidBytes[10];
  const int length = rfid.readUid(uidBytes, sizeof(uidBytes));
  if (length == 0) {
    if (rfid.lastError() < -1) {
      Serial.printf("[RFID] fault=%d version=0x%02X\n", rfid.lastError(),
                    rfid.version());
      rfid.clearError();
    }
    return;
  }

  const String uid = uidToHex(uidBytes, length);
  const bool repeat = uid == lastScannedUid && millis() - lastScanMs < 2500;
  strlcpy(lastScannedUid, uid.c_str(), sizeof(lastScannedUid));
  lastScanMs = millis();
  rfid.haltTag();
  if (admin.onTagScanned(uid)) {
    setStatus(String("Enrolled ") + uid);
    return;
  }

  if (!repeat) selectTag(uid);
}

void toggleRelay(uint8_t channel) {
  if (!relayReady) {
    setStatus("Relay not detected");
    return;
  }
  if (!relays.toggle(channel)) {
    relayReady = false;
    setStatus("Relay write failed");
    return;
  }
  Serial.printf("[RELAY] channel=%u state=%s mask=0x%02X\n", channel,
                relays.isOn(channel) ? "ON" : "OFF", relays.state());
  setStatus(String("Relay ") + channel +
            (relays.isOn(channel) ? " ON" : " OFF"));
}

void handleTouch() {
  const auto& touch = M5.Touch.getDetail();
  if (!touch.wasPressed()) return;

  if (hasActiveTag() && touch.x >= 240 && touch.y >= 34 && touch.y <= 80) {
    endActiveTag();
    return;
  }
  if (touch.y >= 108 && touch.y <= 162) {
    toggleRelay(touch.x < 160 ? 1 : 2);
  } else if (touch.y >= 174 && touch.y <= 230) {
    toggleRelay(touch.x < 160 ? 3 : 4);
  }
}

void printStatus() {
  Serial.printf(
      "[STATE] uid=%s session=%lu pending=%lu sensor=%lu tag_total=%llu "
      "relay_mask=0x%02X sd=%s rfid=%s relay=%s\n",
      hasActiveTag() ? activeUid : "NONE",
      static_cast<unsigned long>(sessionPulses),
      static_cast<unsigned long>(pendingPulses),
      static_cast<unsigned long>(flow.totalPulses()),
      static_cast<unsigned long long>(hasActiveTag() ? storage.totalFor(activeUid)
                                                    : 0),
      relays.state(), storage.healthy() ? "ok" : "fail",
      rfidReady ? "ok" : "fail", relayReady ? "ok" : "fail");
}

void processSerialCommand(String command) {
  command.trim();
  command.toLowerCase();
  if (command.length() == 2 && command[0] == 'r' && command[1] >= '1' &&
      command[1] <= '4') {
    toggleRelay(command[1] - '0');
  } else if (command == "off") {
    if (relays.allOff()) {
      setStatus("All relays OFF");
    } else {
      setStatus("Relay OFF failed");
    }
  } else if (command == "end") {
    endActiveTag();
  } else if (command == "status") {
    printStatus();
  } else if (command.length() > 0) {
    Serial.println("[HELP] commands: r1 r2 r3 r4 off end status");
  }
}

void handleSerial() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n' || value == '\r') {
      if (serialCommand.length() > 0) {
        processSerialCommand(serialCommand);
        serialCommand = "";
      }
    } else if (serialCommand.length() < 32) {
      serialCommand += value;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setTextWrap(false);
  drawScreen();

  flow.begin(Config::FLOW_PIN);
  lastSensorTotal = flow.totalPulses();

  const bool sdReady = storage.begin();
  Serial.printf("[SD] ready=%s size_mb=%llu path=%s\n", sdReady ? "yes" : "no",
                static_cast<unsigned long long>(storage.cardSizeMB()),
                Config::LOG_PATH);
  const bool membersReady = sdReady && members.begin();
  Serial.printf("[MEMBERS] ready=%s count=%u path=%s\n",
                membersReady ? "yes" : "no",
                static_cast<unsigned>(members.count()), Config::MEMBER_PATH);

  // M5Unified owns Wire1 for internal Tough hardware. External Port A devices
  // stay on Wire so M5.update() cannot reassign their pins.
  Wire.end();
  delay(10);
  Wire.begin(Config::I2C_SDA, Config::I2C_SCL, Config::I2C_FREQUENCY);

  relayReady = relays.begin(Wire, Config::RELAY_ADDRESS);
  rfidReady = rfid.begin(Wire, Config::RFID_ADDRESS);
  Serial.printf("[I2C] relay_0x26=%s rfid_0x28=%s rfid_version=0x%02X\n",
                devicePresent(Config::RELAY_ADDRESS) ? "yes" : "no",
                devicePresent(Config::RFID_ADDRESS) ? "yes" : "no",
                rfid.version());

  const bool adminReady = admin.begin();
  Serial.printf("[WEB] ready=%s ssid=%s address=http://%s/\n",
                adminReady ? "yes" : "no", Config::WIFI_AP_NAME,
                admin.address().c_str());

  setStatus(sdReady && relayReady && rfidReady && membersReady && adminReady
                ? String("Setup: ") + admin.address()
                : "Check red status");
  Serial.printf("[SMOKE] sd=%s rfid=%s relay=%s flow_pin=%u ready=%u\n",
                sdReady ? "ok" : "fail", rfidReady ? "ok" : "fail",
                relayReady ? "ok" : "fail", Config::FLOW_PIN,
                sdReady && relayReady && rfidReady);
  Serial.println("[HELP] commands: r1 r2 r3 r4 off end status");
  screenDirty = true;
}

void loop() {
  M5.update();
  admin.handle();
  handleTouch();
  handleSerial();
  pollFlow();
  pollRfid();

  if (hasActiveTag() && pendingPulses > 0 &&
      millis() - lastLogMs >= Config::LOG_INTERVAL_MS) {
    // Retry on the normal interval if the card is unavailable; never hammer
    // the SPI bus or serial log continuously after a write failure.
    flushPulses();
    lastLogMs = millis();
  }

  static uint32_t lastDrawMs = 0;
  if (screenDirty && millis() - lastDrawMs >= 100) {
    drawScreen();
    lastDrawMs = millis();
  }
  delay(5);
}
