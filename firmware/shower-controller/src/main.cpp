#include <M5Unified.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "AdminServer.h"
#include "CampNet.h"
#include "Config.h"
#include "FlowMeter.h"
#include "I2cHub.h"
#include "LightShow.h"
#include "MemberRegistry.h"
#include "PulseStorage.h"
#include "RelayController.h"
#include "RfidReader.h"
#include "SessionStorage.h"
#include "SettingsStore.h"
#include "SpeakerAudio.h"
#include "StationDisplay.h"
#include "UsageLedger.h"

namespace {
enum class ScreenState { IDLE, ACTIVE, MESSAGE, CALIBRATION };

FlowMeter flow;
PulseStorage pulseStorage;
MemberRegistry members;
SessionStorage sessions;
SettingsStore settings;
SpeakerAudio speakerAudio;
UsageLedger ledger;
CampNetLink campNet(members, sessions, settings, ledger);
AdminServer admin(members, pulseStorage, sessions, settings, speakerAudio, ledger, campNet);
RelayController relays;
RfidReader rfid;
I2cHub i2cHub;
LightShow lightShow;
StationDisplay stationDisplay;

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
uint32_t lastBusProbeMs = 0;
uint32_t buttonChangedMs = 0;
uint32_t lastMusicKnobReadMs = 0;
uint32_t lastMusicKnobLogMs = 0;
uint32_t lastMusicStartAttemptMs = 0;
uint32_t lastMusicMovementMs = 0;
uint16_t musicKnobRaw = 0;
uint16_t musicMotionReferenceRaw = 0;
uint16_t musicCalibrationPositions[Config::MUSIC_KNOB_POSITION_COUNT] = {0};
float activeAllowance = 0.0F;
uint32_t activeTimeoutMs = 0;
float summaryGallons = 0.0F;
uint32_t summaryElapsedMs = 0;
// Camp-wide totals for the wristband that just finished (this station + every
// other station's last snapshot), shown on the summary screen.
float summaryTotalGallons = 0.0F;
uint32_t lastRemoteChangeCount = 0;
bool rfidReady = false;
bool relayReady = false;
bool hubReady = false;
uint8_t hubAddress = Config::PAHUB_ADDRESS;
int8_t relayChannel = -1;
int8_t rfidChannel = -1;
bool calibrationActive = false;
bool relayTestActive = false;
uint8_t relayTestChannel = 0;
uint32_t relayTestStartMs = 0;
bool screenDirty = true;
bool buttonRawPressed = false;
bool buttonStablePressed = false;
bool musicStartPending = false;
bool musicKnobMoving = false;
bool musicStaticStarted = false;
bool musicCalibrationActive = false;
bool summaryReady = false;
bool summaryLogged = false;
uint8_t musicCalibrationNextPosition = 0;
int8_t musicChannel = -1;
String musicCalibrationMessage = "Ready to calibrate";
String messageTitle;
String messageBody;
String serialCommand;

bool sessionActive() { return activeUid[0] != '\0'; }

void markRelayFailure() {
  relays.allOff();
  relayReady = false;
}

bool setConfiguredRelay(uint8_t channel, bool on) {
  if (channel == 0) return true;
  if (!relayReady || !relays.set(channel, on)) {
    markRelayFailure();
    return false;
  }
  return true;
}

void shutdownAllRelays() {
  if (relayReady && !relays.allOff()) relayReady = false;
}

bool applyAccessoryPolicy() {
  const SettingsStore::RelayConfig& config = settings.relayConfig();
  return !config.accessoryEnabled || config.accessory == 0 ||
         setConfiguredRelay(config.accessory, true);
}

void stopPump() {
  setConfiguredRelay(settings.relayConfig().pump, false);
}

void stopSessionOutputs() {
  const SettingsStore::RelayConfig& config = settings.relayConfig();
  if (!setConfiguredRelay(config.pump, false)) return;
  setConfiguredRelay(config.charger, false);
}

uint8_t doorDisplayState() {
  if (sessionActive()) return CampNet::DOOR_IN_USE;
  if (calibrationActive || relayTestActive || !pulseStorage.healthy() || !sessions.healthy() ||
      !settings.healthy() || !relayReady || !rfidReady) return CampNet::DOOR_UNAVAILABLE;
  return CampNet::DOOR_OPEN;
}

// Session limits for a wristband at this station: a member's own shower
// allowance (if set) overrides the shower limit; fills always use the role
// limit. Time limits always come from the role.
float effectiveAllowanceFor(const char* uid) {
  const float roleLimit = settings.roleLimits(Config::STATION_ROLE_VALUE).gallons;
  const float memberLimit = members.allowanceFor(uid);
  if (Config::IS_SHOWER && memberLimit > 0.0F) return memberLimit;
  return roleLimit > 0.0F ? roleLimit : Config::DEFAULT_ROLE_LIMIT_GALLONS[Config::STATION_ROLE_VALUE];
}

uint32_t effectiveTimeoutMs() {
  uint16_t minutes = settings.roleLimits(Config::STATION_ROLE_VALUE).minutes;
  if (minutes < Config::MIN_LIMIT_MINUTES || minutes > Config::MAX_LIMIT_MINUTES) {
    minutes = Config::DEFAULT_ROLE_LIMIT_MINUTES[Config::STATION_ROLE_VALUE];
  }
  return static_cast<uint32_t>(minutes) * 60UL * 1000UL;
}

void serviceCampNet() {
  campNet.setDoorState(doorDisplayState(), sessionActive());
  campNet.handle();
  ledger.handle();
  // Limits or members adopted from another station change what the idle
  // screen advertises.
  if (campNet.remoteChangeCount() != lastRemoteChangeCount) {
    lastRemoteChangeCount = campNet.remoteChangeCount();
    screenDirty = true;
  }
}

float gallonsFor(uint32_t pulses) {
  const float calibration = settings.pulsesPerGallon();
  return calibration > 0.0F ? pulses / calibration : 0.0F;
}

void setMessage(const String& title, const String& body, uint32_t durationMs = 3500) {
  summaryReady = false;
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

bool displayReady() {
  return pulseStorage.healthy() && sessions.healthy() && settings.healthy() &&
         relayReady && rfidReady;
}

void drawScreen() {
  const bool ready = displayReady();
  const uint8_t peers = campNet.peerCount();
  if (screenState == ScreenState::IDLE) {
    stationDisplay.drawIdle(Config::STATION_ROLE_VALUE, ready, peers);
  } else if (screenState == ScreenState::ACTIVE) {
    const bool pumpOn = relays.isOn(settings.relayConfig().pump);
    const float burnTotal = sessions.gallonsFor(activeUid) +
                            ledger.remoteGallonsFor(activeUid);
    stationDisplay.drawSession(Config::STATION_ROLE_VALUE, activeName, burnTotal,
                               gallonsFor(sessionPulses), millis() - sessionStartMs,
                               pumpOn, ready, peers);
  } else if (screenState == ScreenState::CALIBRATION) {
    const uint32_t pulses = flow.totalPulses() - calibrationStartPulses;
    stationDisplay.drawCalibration(pulses, ready, peers);
  } else {
    if (summaryReady) {
      stationDisplay.drawSummary(Config::STATION_ROLE_VALUE, summaryGallons,
                                 summaryTotalGallons, summaryElapsedMs,
                                 summaryLogged, ready, peers);
    } else {
      stationDisplay.drawMessage(Config::STATION_ROLE_VALUE, messageTitle,
                                 messageBody, ready, peers);
    }
  }
  screenDirty = false;
}

void discoverI2cDevices() {
  if (!hubReady) {
    for (uint8_t address = 0x70; address <= 0x77 && !hubReady; ++address) {
      hubReady = i2cHub.begin(Wire, address);
      if (hubReady) hubAddress = address;
    }
  }
  if (!hubReady) return;

  if (!relayReady) {
    relayChannel = i2cHub.findDevice(Config::RELAY_ADDRESS);
    relayReady = relayChannel >= 0 && relays.begin(Wire, Config::RELAY_ADDRESS, &i2cHub, relayChannel);
    if (relayReady && !applyAccessoryPolicy()) relayReady = false;
  }
  if (!rfidReady) {
    rfidChannel = i2cHub.findDevice(Config::RFID_ADDRESS);
    rfidReady = rfidChannel >= 0 && rfid.begin(Wire, Config::RFID_ADDRESS, &i2cHub, rfidChannel);
  }
}

void serviceI2cRecovery() {
  if ((hubReady && relayReady && rfidReady) ||
      millis() - lastBusProbeMs < 5000 || sessionActive() || calibrationActive ||
      relayTestActive) return;
  lastBusProbeMs = millis();
  discoverI2cDevices();
}

bool flushPulses() {
  if (!sessionActive() || pendingPulses == 0) return true;
  const uint32_t delta = pendingPulses;
  if (!pulseStorage.recordPulses(activeUid, delta)) return false;
  pendingPulses = 0;
  return true;
}

void endSession(const char* reason) {
  if (!sessionActive()) return;
  stopSessionOutputs();
  const bool rawLogged = flushPulses() && pulseStorage.endTag(activeUid);
  const uint32_t endMs = millis();
  summaryGallons = gallonsFor(sessionPulses);
  summaryElapsedMs = endMs - sessionStartMs;
  const bool sessionLogged = sessions.append(sessionStartMs, endMs, activeUid,
                                             sessionPulses, summaryGallons,
                                             activeAllowance, reason);
  const bool logged = rawLogged && sessionLogged;
  // Camp-wide total: this station's completed sessions (now including this
  // one) plus the latest snapshot from every other station.
  summaryTotalGallons = sessions.gallonsFor(activeUid) +
                        ledger.remoteGallonsFor(activeUid);
  campNet.markUsageDirty();
  Serial.printf("[SESSION] end uid=%s pulses=%lu gallons=%.4f reason=%s logged=%s total=%.2f\n",
                activeUid, static_cast<unsigned long>(sessionPulses), summaryGallons,
                reason, logged ? "yes" : "no", summaryTotalGallons);
  activeUid[0] = '\0'; activeName[0] = '\0'; pendingPulses = 0; sessionPulses = 0;
  setMessage(logged ? Config::ROLE_COMPLETE_LABELS[Config::STATION_ROLE_VALUE] : "LOGGING ERROR",
             logged ? "Thank you!" : "Tell a camp admin", Config::SUMMARY_DISPLAY_MS);
  summaryLogged = logged;
  summaryReady = true;
}

void startSession(const String& uid) {
  const char* name = members.nameFor(uid.c_str());
  if (name == nullptr || !members.enabledFor(uid.c_str())) {
    summaryGallons = 0.0F; summaryElapsedMs = 0;
    setMessage("NOT AUTHORIZED", name ? "Wristband disabled" : "See a camp admin");
    return;
  }
  if (!pulseStorage.healthy() || !sessions.healthy() || !settings.healthy()) {
    summaryGallons = 0.0F; summaryElapsedMs = 0;
    setMessage("UNAVAILABLE", "Usage storage needs service");
    return;
  }
  // Authentication opens a session; the physical button starts water flow.
  if (!setConfiguredRelay(settings.relayConfig().pump, false)) {
    summaryGallons = 0.0F; summaryElapsedMs = 0;
    setMessage("UNAVAILABLE", "Pump control needs service");
    return;
  }
  strlcpy(activeUid, uid.c_str(), sizeof(activeUid));
  strlcpy(activeName, name, sizeof(activeName));
  activeAllowance = effectiveAllowanceFor(activeUid);
  activeTimeoutMs = effectiveTimeoutMs();
  pendingPulses = 0; sessionPulses = 0; sessionStartMs = millis(); lastLogMs = millis();
  if (!pulseStorage.selectTag(activeUid)) {
    stopSessionOutputs();
    activeUid[0] = '\0';
    activeName[0] = '\0';
    summaryGallons = 0.0F;
    summaryElapsedMs = 0;
    setMessage("UNAVAILABLE", "Could not start usage log");
    return;
  }
  if (!setConfiguredRelay(settings.relayConfig().charger, true)) {
    stopSessionOutputs();
    pulseStorage.endTag(activeUid);
    activeUid[0] = '\0';
    activeName[0] = '\0';
    summaryGallons = 0.0F;
    summaryElapsedMs = 0;
    setMessage("UNAVAILABLE", "Phone charger control needs service");
    return;
  }
  screenState = ScreenState::ACTIVE;
  screenDirty = true;
  Serial.printf("[SESSION] start uid=%s name=%s allowance=%.2f timeout_min=%lu relay=%u pump=off\n",
                activeUid, activeName, activeAllowance,
                static_cast<unsigned long>(activeTimeoutMs / 60000UL), settings.relayConfig().pump);
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
  if (!rfidReady || relayTestActive || millis() - lastRfidPollMs < 80) return;
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
    const char* name = members.nameFor(uid.c_str());
    if (name == nullptr || !members.enabledFor(uid.c_str())) {
      // An unknown tag must not end someone else's shower; just show the denial.
      setMessage("NOT AUTHORIZED", name ? "Wristband disabled" : "See a camp admin");
      screenState = ScreenState::ACTIVE;
      screenDirty = true;
      return;
    }
    // Someone forgot to log out: close their shower and log the new member in.
    endSession("HANDOFF");
  }
  startSession(uid);
}

void handleCalibration() {
  if (admin.takeCalibrationStartRequest()) {
    if (relayTestActive) {
      admin.reportCalibration(false, 0, "Finish the relay test first");
    } else if (sessionActive()) {
      admin.reportCalibration(false, 0, "Finish the active shower first");
    } else if (!setConfiguredRelay(settings.relayConfig().pump, true)) {
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

void handleRelayAdmin() {
  if (admin.takeRelayPolicyApplyRequest() && relayReady) {
    shutdownAllRelays();
    if (relayReady) applyAccessoryPolicy();
  }

  uint8_t requestedChannel = 0;
  if (admin.takeRelayTestStartRequest(requestedChannel) && !sessionActive() &&
      !calibrationActive && !relayTestActive && relayReady) {
    shutdownAllRelays();
    if (relayReady && setConfiguredRelay(requestedChannel, true)) {
      relayTestActive = true;
      relayTestChannel = requestedChannel;
      relayTestStartMs = millis();
      Serial.printf("[RELAY] test channel=%u duration_ms=%lu\n", requestedChannel,
                    static_cast<unsigned long>(Config::RELAY_TEST_MS));
    }
  }

  const bool stopRequested = admin.takeRelayTestStopRequest();
  const bool timedOut = relayTestActive &&
                        millis() - relayTestStartMs >= Config::RELAY_TEST_MS;
  if (relayTestActive && (stopRequested || timedOut)) {
    shutdownAllRelays();
    relayTestActive = false;
    relayTestChannel = 0;
    if (relayReady) applyAccessoryPolicy();
    Serial.printf("[RELAY] test stopped reason=%s\n", timedOut ? "timeout" : "admin");
  }
}

// The physical GPIO button is the only member-facing water control. The first
// press starts the water; the next one ends the session (pump off).
void toggleShowerWater(const char* source, const char* endReason) {
  if (calibrationActive) {
    Serial.printf("[%s] ignored during calibration\n", source);
    return;
  }
  if (!sessionActive()) {
    Serial.printf("[%s] ignored; no authorized session\n", source);
    return;
  }

  if (relays.isOn(settings.relayConfig().pump)) {
    // Second activation: the shower is over. endSession() turns the pump off.
    Serial.printf("[%s] finish shower\n", source);
    endSession(endReason);
    return;
  }
  // First activation: start the water.
  if (!setConfiguredRelay(settings.relayConfig().pump, true)) {
    Serial.printf("[%s] pump relay write failed\n", source);
    endSession("RELAY_ERROR");
    return;
  }
  Serial.printf("[%s] pump=on\n", source);
  screenDirty = true;
}

void handlePumpButton() {
  const bool pressed = digitalRead(Config::PUMP_BUTTON_PIN) == LOW;
  if (pressed != buttonRawPressed) {
    buttonRawPressed = pressed;
    buttonChangedMs = millis();
  }
  if (millis() - buttonChangedMs < Config::BUTTON_DEBOUNCE_MS ||
      pressed == buttonStablePressed) return;

  buttonStablePressed = pressed;
  if (!pressed) return;
  toggleShowerWater("BUTTON", "BUTTON");
}

void handleMusicKnob() {
  const uint32_t now = millis();
  if (now - lastMusicKnobReadMs < Config::MUSIC_KNOB_SAMPLE_MS) return;
  lastMusicKnobReadMs = now;

  const uint16_t sample = analogRead(Config::MUSIC_KNOB_PIN);
  // A small low-pass filter removes ADC noise without making the knob sluggish.
  musicKnobRaw = static_cast<uint16_t>(
      (static_cast<uint32_t>(musicKnobRaw) * 7U + sample) / 8U);

  if (admin.takeMusicCalibrationStartRequest()) {
    speakerAudio.stop();
    musicStartPending = false;
    musicKnobMoving = false;
    musicStaticStarted = false;
    musicMotionReferenceRaw = musicKnobRaw;
    musicChannel = 0;
    musicCalibrationActive = true;
    musicCalibrationNextPosition = 0;
    memset(musicCalibrationPositions, 0, sizeof(musicCalibrationPositions));
    musicCalibrationMessage = "Set knob to position 0, then capture";
    Serial.println("[MUSIC] calibration started; waiting for position 0");
  }
  if (admin.takeMusicCalibrationCancelRequest()) {
    musicCalibrationActive = false;
    musicCalibrationNextPosition = 0;
    musicChannel = -1;
    musicKnobMoving = false;
    musicStaticStarted = false;
    musicMotionReferenceRaw = musicKnobRaw;
    musicCalibrationMessage = "Calibration cancelled; previous values retained";
    Serial.println("[MUSIC] calibration cancelled");
  }
  if (admin.takeMusicCalibrationCaptureRequest() && musicCalibrationActive) {
    const uint8_t captured = musicCalibrationNextPosition;
    musicCalibrationPositions[captured] = musicKnobRaw;
    ++musicCalibrationNextPosition;
    Serial.printf("[MUSIC] calibration position=%u raw=%u\n", captured, musicKnobRaw);
    if (musicCalibrationNextPosition >= Config::MUSIC_KNOB_POSITION_COUNT) {
      if (settings.setMusicKnobCalibration(musicCalibrationPositions)) {
        musicCalibrationActive = false;
        musicCalibrationNextPosition = 0;
        musicChannel = -1;
        musicKnobMoving = false;
        musicStaticStarted = false;
        musicMotionReferenceRaw = musicKnobRaw;
        musicCalibrationMessage = "Saved positions 0-9";
        Serial.println("[MUSIC] calibration saved");
      } else {
        musicCalibrationNextPosition = 0;
        musicCalibrationMessage =
            "Invalid spacing or range; return to position 0 and recapture";
        Serial.println("[MUSIC] calibration rejected; points must be ordered and span at least 1000");
      }
    } else {
      musicCalibrationMessage = "Set knob to position " +
                                String(musicCalibrationNextPosition) +
                                ", then capture";
    }
  }

  if (musicCalibrationActive) {
    musicMotionReferenceRaw = musicKnobRaw;
  } else {
    const uint16_t movement = static_cast<uint16_t>(abs(
        static_cast<int>(musicKnobRaw) -
        static_cast<int>(musicMotionReferenceRaw)));
    if (movement >= Config::MUSIC_KNOB_MOTION_THRESHOLD) {
      musicMotionReferenceRaw = musicKnobRaw;
      lastMusicMovementMs = now;
      if (!musicKnobMoving) {
        musicKnobMoving = true;
        musicStartPending = false;
        musicStaticStarted = speakerAudio.startRadioStatic();
        lastMusicStartAttemptMs = now;
        Serial.printf("[MUSIC] knob moving raw=%u; tuning static=%s\n",
                      musicKnobRaw, musicStaticStarted ? "on" : "waiting");
      }
    }

    if (musicKnobMoving && !musicStaticStarted &&
        now - lastMusicStartAttemptMs >= Config::MUSIC_KNOB_RETRY_MS) {
      lastMusicStartAttemptMs = now;
      musicStaticStarted = speakerAudio.startRadioStatic();
    }

    const bool settled = musicKnobMoving &&
                         now - lastMusicMovementMs >=
                             Config::MUSIC_KNOB_SETTLE_MS;
    if (settled || (!musicKnobMoving && musicChannel < 0)) {
      const int8_t previous = musicChannel;
      uint8_t candidate = settings.nearestMusicPosition(musicKnobRaw);
      if (!settings.musicKnobCalibrated() && musicChannel >= 0) {
        if (musicChannel == 0 && musicKnobRaw < Config::MUSIC_KNOB_ON_RAW)
          candidate = 0;
        if (musicChannel > 0 && musicKnobRaw > Config::MUSIC_KNOB_OFF_RAW)
          candidate = 1;
      } else if (settings.musicKnobCalibrated() && musicChannel >= 0 &&
                 candidate != static_cast<uint8_t>(musicChannel)) {
        const uint16_t currentDistance = static_cast<uint16_t>(abs(
            static_cast<int>(musicKnobRaw) -
            static_cast<int>(settings.musicKnobPosition(musicChannel))));
        const uint16_t candidateDistance = static_cast<uint16_t>(abs(
            static_cast<int>(musicKnobRaw) -
            static_cast<int>(settings.musicKnobPosition(candidate))));
        if (candidateDistance + Config::MUSIC_KNOB_CHANNEL_HYSTERESIS >=
            currentDistance) {
          candidate = static_cast<uint8_t>(musicChannel);
        }
      }

      musicKnobMoving = false;
      musicStaticStarted = false;
      musicMotionReferenceRaw = musicKnobRaw;
      musicChannel = candidate;
      musicStartPending = candidate > 0;
      lastMusicStartAttemptMs = now - Config::MUSIC_KNOB_RETRY_MS;
      if (candidate == 0) {
        speakerAudio.stop();
        Serial.printf("[MUSIC] knob settled raw=%u channel %d -> quiet\n",
                      musicKnobRaw, previous);
      } else {
        Serial.printf("[MUSIC] knob settled raw=%u channel %d -> %u (%s)\n",
                      musicKnobRaw, previous, candidate,
                      Config::MUSIC_CHANNEL_NAMES[candidate]);
      }
    }

    if (!musicKnobMoving && musicStartPending &&
        now - lastMusicStartAttemptMs >= Config::MUSIC_KNOB_RETRY_MS) {
      lastMusicStartAttemptMs = now;
      if (speakerAudio.playChannel(static_cast<uint8_t>(musicChannel))) {
        musicStartPending = false;
        Serial.printf("[MUSIC] channel=%d song started\n", musicChannel);
      } else {
        Serial.println("[MUSIC] waiting for speaker and channel file");
      }
    }
  }

  admin.reportMusicKnob(musicKnobRaw, musicChannel, musicCalibrationActive,
                        musicCalibrationNextPosition, musicCalibrationMessage);

  if (now - lastMusicKnobLogMs >= 1000) {
    lastMusicKnobLogMs = now;
    Serial.printf("[MUSIC] knob=%u/4095 channel=%d moving=%s calibrated=%s\n",
                  musicKnobRaw, musicChannel, musicKnobMoving ? "yes" : "no",
                  settings.musicKnobCalibrated() ? "yes" : "no");
  }
}

void reportAdminState() {
  admin.reportHardware(hubReady, relayReady, rfidReady);
  admin.reportRelays(relays.state(), relayTestActive, relayTestChannel);
  admin.reportSession(activeName, sessionActive() ? gallonsFor(sessionPulses) : 0.0F,
                      sessionActive() ? activeAllowance : 0.0F,
                      relayReady && relays.isOn(settings.relayConfig().pump),
                      doorDisplayState());
}

void serviceReliability() {
  const uint32_t now = millis();

  // The radio (soft-AP + ESP-NOW) and admin server retry at runtime instead of
  // staying dead until a reboot.
  static uint32_t lastAdminRetryMs = 0;
  if ((!campNet.ready() || !admin.started()) &&
      now - lastAdminRetryMs >= Config::ADMIN_RETRY_INTERVAL_MS) {
    lastAdminRetryMs = now;
    if (!campNet.ready()) {
      Serial.printf("[NET] retry %s\n", campNet.begin() ? "recovered" : "failed");
    }
    if (campNet.ready() && !admin.started()) {
      if (admin.begin()) {
        Serial.printf("[WEB] admin server recovered at http://%s/\n",
                      admin.address().c_str());
      } else {
        Serial.println("[WEB] admin server retry failed");
      }
    }
  }

  reportAdminState();
  // A remote END SESSION over CampNet can only stop water, never start it.
  if (admin.takeEndSessionRequest() && sessionActive()) endSession("REMOTE");
  if (admin.takeSpeakerSearchRequest() && Config::HAS_MUSIC) speakerAudio.requestDiscovery();
  if (admin.takeRebootRequest()) {
    Serial.println("[SYSTEM] reboot requested from admin page");
    if (sessionActive()) endSession("REBOOT");
    shutdownAllRelays();
    admin.handle();  // let the HTTP response flush before restarting
    delay(250);
    ESP.restart();
  }

  static uint32_t lastHealthLogMs = 0;
  if (now - lastHealthLogMs >= Config::HEALTH_LOG_INTERVAL_MS) {
    lastHealthLogMs = now;
    Serial.printf("[HEALTH] uptime=%lus heap=%u min_heap=%u max_alloc=%u psram=%u wifi_clients=%u underruns=%lu net_peers=%u net_rx=%lu net_tx=%lu net_txfail=%lu net_rxdrop=%lu\n",
                  static_cast<unsigned long>(now / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram(),
                  WiFi.softAPgetStationNum(),
                  static_cast<unsigned long>(speakerAudio.bufferUnderruns()),
                  static_cast<unsigned>(campNet.peerCount()),
                  static_cast<unsigned long>(campNet.rxPackets()),
                  static_cast<unsigned long>(campNet.txPackets()),
                  static_cast<unsigned long>(campNet.txFailures()),
                  static_cast<unsigned long>(campNet.rxDropped()));
  }
}

void printStatus() {
  Serial.printf("[STATION] id=%u role=%s name=%s ssid=%s channel=%u peers=%u members=%u/v%lu limits=v%lu\n",
                Config::STATION_ID_VALUE, CampNet::roleName(Config::STATION_ROLE_VALUE),
                Config::STATION_NAME, Config::WIFI_AP_NAME, CampNet::CHANNEL,
                static_cast<unsigned>(campNet.peerCount()), static_cast<unsigned>(members.count()),
                static_cast<unsigned long>(members.version()),
                static_cast<unsigned long>(settings.limitsVersion()));
  Serial.printf("[STATE] state=%u uid=%s pulses=%lu gallons=%.3f limit=%.2f relay=0x%02X sd=%s hub=%s@0x%02X relay_ch=%d rfid=%s rfid_ch=%d calibration=%.2f\n",
                static_cast<unsigned>(screenState), sessionActive() ? activeUid : "NONE",
                static_cast<unsigned long>(sessionPulses), gallonsFor(sessionPulses),
                activeAllowance, relays.state(), pulseStorage.healthy() ? "ok" : "fail",
                hubReady ? "ok" : "fail", hubAddress, relayChannel,
                rfidReady ? "ok" : "fail", rfidChannel, settings.pulsesPerGallon());
  const SettingsStore::RelayConfig& relayConfig = settings.relayConfig();
  Serial.printf("[RELAY] pump=%u charger=%u accessory=%u accessory_enabled=%s state=0x%02X test=%u\n",
                relayConfig.pump, relayConfig.charger, relayConfig.accessory,
                relayConfig.accessoryEnabled ? "yes" : "no", relays.state(),
                relayTestActive ? relayTestChannel : 0);
  Serial.printf("[MUSIC] knob=%u/4095 channel=%d calibrated=%s speaker=%s speaker_volume=%u%% playback=%s pcm_bass=%u pcm_mid=%u pcm_treble=%u pcm_volume=%u\n",
                musicKnobRaw, musicChannel,
                settings.musicKnobCalibrated() ? "yes" : "no",
                speakerAudio.connectionLabel(),
                speakerAudio.speakerVolumePercent(), speakerAudio.playbackLabel(),
                speakerAudio.bassLevel(), speakerAudio.midLevel(),
                speakerAudio.trebleLevel(), speakerAudio.volumeLevel());
}

void processSerialCommand(String command) {
  command.trim(); command.toLowerCase();
  if (command == "off") { if (sessionActive()) endSession("SERIAL"); else { stopSessionOutputs(); setMessage("WATER OFF", "Pump and charger stopped"); } }
  else if (command == "end") endSession("SERIAL");
  else if (command == "status") printStatus();
  else if (command == "tone") Serial.println(speakerAudio.playTestTone() ? "[AUDIO] tone requested" : "[AUDIO] speaker not connected");
  else if (command == "play") Serial.println(speakerAudio.playSong() ? "[AUDIO] song requested" : "[AUDIO] speaker/file unavailable");
  else if (command.length() == 5 && command.startsWith("play") &&
           command[4] >= '1' && command[4] <= '9') {
    const uint8_t channel = static_cast<uint8_t>(command[4] - '0');
    Serial.printf("[AUDIO] channel %u %s\n", channel,
                  speakerAudio.playChannel(channel) ? "requested" : "unavailable");
  }
  else if (command == "stop") speakerAudio.stop();
  else if (!command.isEmpty()) Serial.println("[HELP] commands: off end status tone play play1..play9 stop");
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
  // Tough's AXP192 gates 5 V to the external HY2.0 ports. Keep Port A powered
  // explicitly before probing the PaHUB rather than relying on library defaults.
  M5.Power.setExtOutput(true);
  delay(150);
  M5.Display.setRotation(1);
  stationDisplay.begin();
  pinMode(Config::PUMP_BUTTON_PIN, INPUT_PULLUP);
  buttonRawPressed = digitalRead(Config::PUMP_BUTTON_PIN) == LOW;
  buttonStablePressed = buttonRawPressed;
  buttonChangedMs = millis();
  if (Config::HAS_MUSIC) {
    pinMode(Config::MUSIC_KNOB_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(Config::MUSIC_KNOB_PIN, ADC_11db);
    uint32_t initialKnobTotal = 0;
    for (uint8_t i = 0; i < 16; ++i) initialKnobTotal += analogRead(Config::MUSIC_KNOB_PIN);
    musicKnobRaw = static_cast<uint16_t>(initialKnobTotal / 16U);
    musicMotionReferenceRaw = musicKnobRaw;
  }
  if (Config::HAS_LED_STRIP) lightShow.begin();
  drawScreen();

  flow.begin(Config::FLOW_PIN);
  lastSensorTotal = flow.totalPulses();
  const bool sdReady = pulseStorage.begin();
  const bool membersReady = sdReady && members.begin();
  const bool settingsReady = sdReady && settings.begin();
  const bool sessionsReady = sdReady && sessions.begin();
  const bool ledgerReady = sdReady && ledger.begin();

  Wire.end(); delay(10);
  Wire.begin(Config::I2C_SDA, Config::I2C_SCL, Config::I2C_FREQUENCY);
  discoverI2cDevices();
  const bool netReady = campNet.begin();
  const bool adminReady = netReady && admin.begin();
  bool audioReady = false;
  if (Config::HAS_MUSIC) {
    speakerAudio.setSpeakerVolumePercent(settings.speakerVolumePercent());
    audioReady = speakerAudio.begin();
  }

  Serial.printf("[BOOT] station=%u role=%s board=%u ext5v=%s sd=%s members=%s settings=%s sessions=%s ledger=%s hub=%s relay=%s rfid=%s net=%s admin=%s\n",
                Config::STATION_ID_VALUE, CampNet::roleName(Config::STATION_ROLE_VALUE),
                static_cast<unsigned>(M5.getBoard()), M5.Power.getExtOutput()?"on":"off",
                sdReady?"ok":"fail", membersReady?"ok":"fail", settingsReady?"ok":"fail",
                sessionsReady?"ok":"fail", ledgerReady?"ok":"fail", hubReady?"ok":"fail",
                relayReady?"ok":"fail", rfidReady?"ok":"fail", netReady?"ok":"fail",
                adminReady?"ok":"fail");
  Serial.printf("[WEB] ssid=%s address=http://%s/ page_password=%s\n", Config::WIFI_AP_NAME,
                admin.address().c_str(), Config::ADMIN_PAGE_PASSWORD ? "on" : "off");
  Serial.printf("[NET] channel=%u door_sign=%s members_version=%lu limits_version=%lu\n",
                CampNet::CHANNEL, Config::HAS_DOOR_SIGN ? "yes" : "no",
                static_cast<unsigned long>(members.version()),
                static_cast<unsigned long>(settings.limitsVersion()));
  if (Config::HAS_MUSIC) {
    Serial.printf("[AUDIO] source=%s file=%s path=%s\n", audioReady ? "ready" : "failed",
                  speakerAudio.fileAvailable() ? "ready" : "missing", Config::AUDIO_PATH);
    Serial.printf("[MUSIC] knob_pin=%u raw=%u on=%u off=%u\n", Config::MUSIC_KNOB_PIN,
                  musicKnobRaw, Config::MUSIC_KNOB_ON_RAW, Config::MUSIC_KNOB_OFF_RAW);
  } else {
    Serial.println("[AUDIO] disabled for this station role");
  }
  Serial.printf("[I2C] pahub_0x%02X=%s relay_0x26=ch%d rfid_0x28=ch%d\n",
                hubAddress, hubReady?"yes":"no", relayChannel, rfidChannel);
  const SettingsStore::RelayConfig& relayConfig = settings.relayConfig();
  Serial.printf("[RELAY] pump=%u charger=%u accessory=%u accessory_enabled=%s state=0x%02X\n",
                relayConfig.pump, relayConfig.charger, relayConfig.accessory,
                relayConfig.accessoryEnabled ? "yes" : "no", relays.state());
  Serial.println("[HELP] commands: off end status tone play play1..play9 stop");
  screenDirty = true;

  // If the main loop ever wedges (I2C bus hang, SD stall, deadlock), reboot
  // instead of sitting dead until someone power-cycles the station. Only this
  // task is subscribed; radio and system tasks keep their own supervision.
  esp_task_wdt_init(Config::WDT_TIMEOUT_S, true);
  esp_task_wdt_add(nullptr);
}

void loop() {
  esp_task_wdt_reset();
  M5.update();
  // Refresh lifecycle truth before WebServer or remote COMMAND handlers can
  // accept an idle-only relay action.
  reportAdminState();
  admin.handle();
  if (Config::HAS_MUSIC) speakerAudio.handle();
  if (Config::HAS_LED_STRIP) lightShow.handle(speakerAudio);
  serviceCampNet();
  serviceI2cRecovery();
  handleRelayAdmin();
  serviceReliability();
  handlePumpButton();
  if (Config::HAS_MUSIC) handleMusicKnob();
  handleSerial();
  pollFlow();
  pollRfid();
  handleCalibration();
  if (sessionActive() && pendingPulses > 0 && millis() - lastLogMs >= Config::LOG_INTERVAL_MS) {
    if (!flushPulses()) endSession("SD_ERROR");
    lastLogMs = millis();
  }
  if (sessionActive() && millis() - sessionStartMs >= activeTimeoutMs) {
    endSession("TIMEOUT");
  }
  if (screenState == ScreenState::MESSAGE && static_cast<int32_t>(millis() - messageUntilMs) >= 0) {
    summaryGallons = 0.0F;
    summaryElapsedMs = 0;
    summaryReady = false;
    screenState = ScreenState::IDLE;
    screenDirty = true;
  }
  static uint32_t lastActiveScreenTickMs = 0;
  if (screenState == ScreenState::ACTIVE &&
      relays.isOn(settings.relayConfig().pump) &&
      millis() - lastActiveScreenTickMs >= 1000) {
    lastActiveScreenTickMs = millis();
    screenDirty = true;
  }
  static uint32_t lastDrawMs = 0;
  if (screenDirty && millis() - lastDrawMs >= 250) { drawScreen(); lastDrawMs = millis(); }
  delay(5);
}
