#include <M5Unified.h>
#include <Wire.h>

#include "AdminServer.h"
#include "Config.h"
#include "FlowMeter.h"
#include "MemberRegistry.h"
#include "PulseStorage.h"
#include "RelayController.h"
#include "RfidReader.h"
#include "SessionStorage.h"
#include "SettingsStore.h"

namespace {
enum class ScreenState { IDLE, ACTIVE, MESSAGE, CALIBRATION };

FlowMeter flow;
PulseStorage pulseStorage;
MemberRegistry members;
SessionStorage sessions;
SettingsStore settings;
AdminServer admin(members, pulseStorage, sessions, settings);
RelayController relays;
RfidReader rfid;

ScreenState screenState = ScreenState::IDLE;
char activeUid[21] = {0};
char activeName[33] = {0};
char lastScannedUid[21] = {0};
uint32_t lastScanMs = 0;
uint32_t lastRfidPollMs = 0;
uint32_t lastLogMs = 0;
uint32_t lastSensorTotal = 0;
uint32_t pendingPulses = 0;
uint32_t sessionPulses = 0;
uint32_t sessionStartMs = 0;
uint32_t messageUntilMs = 0;
uint32_t calibrationStartPulses = 0;
uint32_t calibrationStartMs = 0;
float activeAllowance = 0.0F;
float summaryGallons = 0.0F;
bool rfidReady = false;
bool relayReady = false;
bool calibrationActive = false;
bool screenDirty = true;
String messageTitle;
String messageBody;
String serialCommand;

bool sessionActive() { return activeUid[0] != '\0'; }
float gallonsFor(uint32_t pulses) {
  const float calibration = settings.pulsesPerGallon();
  return calibration > 0.0F ? pulses / calibration : 0.0F;
}

void setMessage(const String& title, const String& body, uint32_t durationMs = 3500) {
  messageTitle = title;
  messageBody = body;
  messageUntilMs = millis() + durationMs;
  screenState = ScreenState::MESSAGE;
  screenDirty = true;
  Serial.printf("[STATUS] %s: %s\n", title.c_str(), body.c_str());
}

String uidToHex(const uint8_t* uid, int length) {
  char hex[21] = {0};
  for (int i = 0; i < length && i < 10; ++i) snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", uid[i]);
  return String(hex);
}

bool devicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void drawCentered(const String& text, int y, int size, uint16_t color) {
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(text, 160, y);
}

void drawHeader(const char* label, uint16_t accent = TFT_CYAN) {
  M5.Display.fillRect(0, 0, 320, 28, 0x0861);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(accent, 0x0861);
  M5.Display.drawString(label, 12, 14);
  M5.Display.setTextDatum(middle_right);
  M5.Display.setTextColor((pulseStorage.healthy() && relayReady && rfidReady) ? TFT_GREEN : TFT_ORANGE, 0x0861);
  M5.Display.drawString((pulseStorage.healthy() && relayReady && rfidReady) ? "SYSTEM READY" : "SERVICE NEEDED", 308, 14);
}

void drawScreen() {
  M5.Display.startWrite();
  M5.Display.fillScreen(0x020E);
  if (screenState == ScreenState::IDLE) {
    drawHeader("CAMP SHOWER");
    drawCentered("READY WHEN", 52, 3, TFT_WHITE);
    drawCentered("YOU ARE", 82, 3, TFT_WHITE);
    M5.Display.fillCircle(160, 145, 34, 0x044A);
    M5.Display.drawCircle(160, 145, 34, TFT_CYAN);
    drawCentered("TAP", 128, 2, TFT_CYAN);
    drawCentered("WRISTBAND", 151, 1, TFT_WHITE);
    drawCentered("10 gallon default · admin adjustable", 214, 1, TFT_LIGHTGREY);
  } else if (screenState == ScreenState::ACTIVE) {
    drawHeader("SHOWER IN PROGRESS", TFT_GREEN);
    drawCentered(activeName, 38, 2, TFT_WHITE);
    char amount[32];
    snprintf(amount, sizeof(amount), "%.2f gal", gallonsFor(sessionPulses));
    drawCentered(amount, 72, 4, TFT_CYAN);
    char limit[40];
    snprintf(limit, sizeof(limit), "of %.1f gallon limit", activeAllowance);
    drawCentered(limit, 116, 1, TFT_LIGHTGREY);
    M5.Display.fillRoundRect(18, 140, 284, 14, 7, TFT_DARKGREY);
    const float ratio = constrain(gallonsFor(sessionPulses) / activeAllowance, 0.0F, 1.0F);
    M5.Display.fillRoundRect(18, 140, static_cast<int>(284 * ratio), 14, 7, ratio > .85F ? TFT_ORANGE : TFT_GREEN);
    M5.Display.fillRoundRect(24, 174, 272, 52, 12, TFT_MAROON);
    M5.Display.drawRoundRect(24, 174, 272, 52, 12, TFT_RED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
    M5.Display.drawString("END SHOWER", 160, 200);
  } else if (screenState == ScreenState::CALIBRATION) {
    drawHeader("ADMIN CALIBRATION", TFT_ORANGE);
    drawCentered("DISPENSING", 48, 3, TFT_ORANGE);
    const uint32_t pulses = flow.totalPulses() - calibrationStartPulses;
    drawCentered(String(pulses) + " pulses", 100, 3, TFT_WHITE);
    drawCentered("Stop and enter the known volume", 152, 1, TFT_LIGHTGREY);
    drawCentered("from the admin page", 171, 1, TFT_LIGHTGREY);
    drawCentered("Pump is ON", 211, 1, TFT_GREEN);
  } else {
    drawHeader("CAMP SHOWER");
    drawCentered(messageTitle, 66, 3, messageTitle == "NOT AUTHORIZED" ? TFT_ORANGE : TFT_CYAN);
    drawCentered(messageBody, 122, 2, TFT_WHITE);
    if (summaryGallons > 0.0F) drawCentered(String(summaryGallons, 2) + " gallons used", 166, 2, TFT_GREEN);
    drawCentered("Returning to ready…", 215, 1, TFT_LIGHTGREY);
  }
  M5.Display.endWrite();
  screenDirty = false;
}

bool flushPulses() {
  if (!sessionActive() || pendingPulses == 0) return true;
  const uint32_t delta = pendingPulses;
  if (!pulseStorage.recordPulses(activeUid, delta)) return false;
  pendingPulses = 0;
  return true;
}

void stopPump() {
  if (relayReady && !relays.allOff()) relayReady = false;
}

void endSession(const char* reason) {
  if (!sessionActive()) return;
  stopPump();
  const bool rawLogged = flushPulses() && pulseStorage.endTag(activeUid);
  const uint32_t endMs = millis();
  summaryGallons = gallonsFor(sessionPulses);
  const bool sessionLogged = sessions.append(sessionStartMs, endMs, activeUid,
                                             sessionPulses, summaryGallons,
                                             activeAllowance, reason);
  const bool logged = rawLogged && sessionLogged;
  Serial.printf("[SESSION] end uid=%s pulses=%lu gallons=%.4f reason=%s logged=%s\n",
                activeUid, static_cast<unsigned long>(sessionPulses), summaryGallons,
                reason, logged ? "yes" : "no");
  activeUid[0] = '\0'; activeName[0] = '\0'; pendingPulses = 0; sessionPulses = 0;
  setMessage(logged ? "SHOWER COMPLETE" : "LOGGING ERROR",
             logged ? "Thank you!" : "Tell a camp admin", 5000);
}

void startSession(const String& uid) {
  const char* name = members.nameFor(uid.c_str());
  if (name == nullptr || !members.enabledFor(uid.c_str())) {
    summaryGallons = 0.0F;
    setMessage("NOT AUTHORIZED", name ? "Wristband disabled" : "See a camp admin");
    return;
  }
  if (!pulseStorage.healthy() || !sessions.healthy() || !settings.healthy()) {
    summaryGallons = 0.0F;
    setMessage("UNAVAILABLE", "Usage storage needs service");
    return;
  }
  if (!relayReady || !relays.set(Config::PUMP_RELAY, true)) {
    relayReady = false;
    stopPump();
    summaryGallons = 0.0F;
    setMessage("UNAVAILABLE", "Pump control needs service");
    return;
  }
  strlcpy(activeUid, uid.c_str(), sizeof(activeUid));
  strlcpy(activeName, name, sizeof(activeName));
  activeAllowance = members.allowanceFor(activeUid);
  pendingPulses = 0; sessionPulses = 0; sessionStartMs = millis(); lastLogMs = millis();
  if (!pulseStorage.selectTag(activeUid)) {
    stopPump();
    activeUid[0] = '\0';
    setMessage("UNAVAILABLE", "Could not start usage log");
    return;
  }
  screenState = ScreenState::ACTIVE;
  screenDirty = true;
  Serial.printf("[SESSION] start uid=%s name=%s allowance=%.2f relay=%u\n", activeUid,
                activeName, activeAllowance, Config::PUMP_RELAY);
}

void pollFlow() {
  const uint32_t sensorTotal = flow.totalPulses();
  const uint32_t delta = sensorTotal - lastSensorTotal;
  lastSensorTotal = sensorTotal;
  if (delta == 0) return;
  if (sessionActive()) {
    pendingPulses += delta;
    sessionPulses += delta;
    screenDirty = true;
    if (gallonsFor(sessionPulses) >= activeAllowance) endSession("LIMIT");
  }
}

void pollRfid() {
  if (!rfidReady || millis() - lastRfidPollMs < 80) return;
  lastRfidPollMs = millis();
  uint8_t uidBytes[10];
  const int length = rfid.readUid(uidBytes, sizeof(uidBytes));
  if (length <= 0) return;
  const String uid = uidToHex(uidBytes, length);
  const bool repeat = uid == lastScannedUid && millis() - lastScanMs < 2500;
  strlcpy(lastScannedUid, uid.c_str(), sizeof(lastScannedUid));
  lastScanMs = millis();
  rfid.haltTag();
  if (admin.onTagScanned(uid)) { setMessage("WRISTBAND ADDED", "Enrollment complete"); return; }
  if (repeat || calibrationActive) return;
  if (sessionActive()) {
    if (uid == activeUid) return;
    setMessage("SHOWER BUSY", "Finish the current shower");
    screenState = ScreenState::ACTIVE;
    screenDirty = true;
    return;
  }
  startSession(uid);
}

void handleCalibration() {
  if (admin.takeCalibrationStartRequest()) {
    if (sessionActive()) {
      admin.reportCalibration(false, 0, "Finish the active shower first");
    } else if (!relayReady || !relays.set(Config::PUMP_RELAY, true)) {
      stopPump();
      admin.reportCalibration(false, 0, "Pump control failed");
    } else {
      calibrationStartPulses = flow.totalPulses();
      calibrationStartMs = millis();
      calibrationActive = true;
      screenState = ScreenState::CALIBRATION;
      screenDirty = true;
      admin.reportCalibration(true, 0, "Dispensing into known container");
    }
  }
  if (calibrationActive) {
    const uint32_t pulses = flow.totalPulses() - calibrationStartPulses;
    admin.reportCalibration(true, pulses, "Dispensing into known container");
    static uint32_t lastCalibrationDraw = 0;
    if (millis() - lastCalibrationDraw > 250) { screenDirty = true; lastCalibrationDraw = millis(); }
    if (millis() - calibrationStartMs >= Config::MAX_CALIBRATION_MS) {
      stopPump();
      calibrationActive = false;
      admin.reportCalibration(false, pulses, "Safety timeout; value unchanged");
      setMessage("CALIBRATION STOPPED", "10 minute safety timeout");
    }
  }
  float knownGallons = 0.0F;
  if (admin.takeCalibrationStopRequest(knownGallons)) {
    if (!calibrationActive) {
      admin.reportCalibration(false, 0, "Calibration was not running");
      return;
    }
    stopPump();
    const uint32_t pulses = flow.totalPulses() - calibrationStartPulses;
    calibrationActive = false;
    if (pulses == 0 || knownGallons <= 0.0F) {
      admin.reportCalibration(false, pulses, "No pulses captured; value unchanged");
      setMessage("CALIBRATION FAILED", "No flow pulses captured");
      return;
    }
    const float ratio = pulses / knownGallons;
    const bool saved = settings.setCalibration(ratio);
    admin.reportCalibration(false, pulses, saved ? String("Saved ") + String(ratio, 2) + " pulses/gal" : "Could not save calibration");
    setMessage(saved ? "CALIBRATION SAVED" : "CALIBRATION FAILED",
               saved ? String(ratio, 1) + " pulses / gallon" : "Check SD card", 5000);
  }
}

void handleTouch() {
  const auto& touch = M5.Touch.getDetail();
  if (!touch.wasPressed()) return;
  if (screenState == ScreenState::ACTIVE && touch.y >= 166) endSession("BUTTON");
}

void printStatus() {
  Serial.printf("[STATE] state=%u uid=%s pulses=%lu gallons=%.3f limit=%.2f relay=0x%02X sd=%s rfid=%s calibration=%.2f\n",
                static_cast<unsigned>(screenState), sessionActive() ? activeUid : "NONE",
                static_cast<unsigned long>(sessionPulses), gallonsFor(sessionPulses),
                activeAllowance, relays.state(), pulseStorage.healthy() ? "ok" : "fail",
                rfidReady ? "ok" : "fail", settings.pulsesPerGallon());
}

void processSerialCommand(String command) {
  command.trim(); command.toLowerCase();
  if (command == "off") { if (sessionActive()) endSession("SERIAL"); else { stopPump(); setMessage("PUMP OFF", "Safety stop complete"); } }
  else if (command == "end") endSession("SERIAL");
  else if (command == "status") printStatus();
  else if (!command.isEmpty()) Serial.println("[HELP] commands: off end status");
}

void handleSerial() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n' || value == '\r') { if (!serialCommand.isEmpty()) { processSerialCommand(serialCommand); serialCommand = ""; } }
    else if (serialCommand.length() < 32) serialCommand += value;
  }
}
}

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
  const bool sdReady = pulseStorage.begin();
  const bool membersReady = sdReady && members.begin();
  const bool settingsReady = sdReady && settings.begin();
  const bool sessionsReady = sdReady && sessions.begin();

  Wire.end(); delay(10);
  Wire.begin(Config::I2C_SDA, Config::I2C_SCL, Config::I2C_FREQUENCY);
  relayReady = relays.begin(Wire, Config::RELAY_ADDRESS);
  rfidReady = rfid.begin(Wire, Config::RFID_ADDRESS);
  if (relayReady) relays.allOff();
  const bool adminReady = admin.begin();

  Serial.printf("[BOOT] sd=%s members=%s settings=%s sessions=%s relay=%s rfid=%s admin=%s\n",
                sdReady?"ok":"fail", membersReady?"ok":"fail", settingsReady?"ok":"fail",
                sessionsReady?"ok":"fail", relayReady?"ok":"fail", rfidReady?"ok":"fail", adminReady?"ok":"fail");
  Serial.printf("[WEB] ssid=%s address=http://%s/ user=%s\n", Config::WIFI_AP_NAME,
                admin.address().c_str(), Config::ADMIN_USERNAME);
  Serial.printf("[I2C] relay_0x26=%s rfid_0x28=%s\n",
                devicePresent(Config::RELAY_ADDRESS)?"yes":"no", devicePresent(Config::RFID_ADDRESS)?"yes":"no");
  Serial.println("[HELP] commands: off end status");
  screenDirty = true;
}

void loop() {
  M5.update();
  admin.handle();
  handleTouch();
  handleSerial();
  pollFlow();
  pollRfid();
  handleCalibration();
  if (sessionActive() && pendingPulses > 0 && millis() - lastLogMs >= Config::LOG_INTERVAL_MS) {
    if (!flushPulses()) endSession("SD_ERROR");
    lastLogMs = millis();
  }
  if (sessionActive() && millis() - sessionStartMs >= Config::MAX_SESSION_MS) {
    endSession("TIMEOUT");
  }
  if (screenState == ScreenState::MESSAGE && static_cast<int32_t>(millis() - messageUntilMs) >= 0) {
    summaryGallons = 0.0F;
    screenState = ScreenState::IDLE;
    screenDirty = true;
  }
  static uint32_t lastDrawMs = 0;
  if (screenDirty && millis() - lastDrawMs >= 100) { drawScreen(); lastDrawMs = millis(); }
  delay(5);
}
