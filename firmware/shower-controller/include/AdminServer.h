#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "CampNet.h"
#include "MemberRegistry.h"
#include "PulseStorage.h"
#include "SessionStorage.h"
#include "SettingsStore.h"
#include "SpeakerAudio.h"
#include "UsageLedger.h"

// Local HTTP admin page plus the station's half of the "single admin page":
// it publishes this station's telemetry over CampNet, executes authenticated
// remote commands exactly like the matching local endpoint, and renders every
// station (local + peers) from the same JSON.
class AdminServer {
 public:
  AdminServer(MemberRegistry& registry, const PulseStorage& pulseStorage,
              const SessionStorage& sessions, SettingsStore& settings,
              SpeakerAudio& speakerAudio, const UsageLedger& ledger, CampNetLink& net);

  bool begin();
  void handle();
  bool onTagScanned(const String& uid);

  bool enrollmentPending() const { return enrollmentPending_; }
  const String& pendingName() const { return pendingName_; }
  String address() const;
  bool takeCalibrationStartRequest();
  bool takeCalibrationStopRequest(float& knownGallons);
  void reportCalibration(bool active, uint32_t pulses, const String& message);
  bool takeMusicCalibrationStartRequest();
  bool takeMusicCalibrationCaptureRequest();
  bool takeMusicCalibrationCancelRequest();
  void reportMusicKnob(uint16_t raw, int8_t channel, bool calibrationActive,
                       uint8_t nextPosition, const String& message);
  void reportHardware(bool hubReady, bool relayReady, bool rfidReady);
  void reportRelays(uint8_t state, bool testActive, uint8_t testChannel);
  bool takeRelayPolicyApplyRequest();
  bool takeRelayTestStartRequest(uint8_t& channel);
  bool takeRelayTestStopRequest();
  // Live session state for telemetry; activeName is "" when idle.
  void reportSession(const char* activeName, float sessionGallons, float sessionLimit,
                     bool pumpOn, uint8_t doorState);
  bool takeRebootRequest();
  bool takeSpeakerSearchRequest();
  // Set by a remote END SESSION command; main ends the session with "REMOTE".
  bool takeEndSessionRequest();
  bool started() const { return started_; }

 private:
  void configureRoutes();
  void sendOverview();
  String statusJson() const;
  String memberJson(size_t index) const;
  String membersJson() const;
  String sessionsJson() const;
  String healthJson() const;
  String stationsJson() const;
  String telemetryJson(const CampNet::TelemetryPacket& telemetry) const;
  String recentJson(const CampNet::RecentPacket& recent) const;
  void buildTelemetry(CampNet::TelemetryPacket& telemetry) const;
  void buildRecent(CampNet::RecentPacket& recent) const;
  void publishTelemetry();
  void drainRemoteCommands();
  // One implementation per station action, shared by the local HTTP route and
  // the remote COMMAND path. Returns an HTTP status code (200 = ok,
  // 501 = unsupported on this station role).
  int runAction(uint8_t action, const String& text, float value, String& message);
  void handleCommandPost();
  void handleCommandPoll();
  void sendAction(uint8_t action, const String& text, float value);
  void renameMember();
  void updateMember();
  void deleteMember();
  void changePassword();
  void setRoleLimits();
  void handleAudioUpload();
  bool authorize();
  void sendJsonMessage(int code, bool ok, const String& message);
  static String jsonEscape(const String& value);

  WebServer server_{80};
  MemberRegistry& registry_;
  const PulseStorage& pulseStorage_;
  const SessionStorage& sessions_;
  SettingsStore& settings_;
  SpeakerAudio& speakerAudio_;
  const UsageLedger& ledger_;
  CampNetLink& net_;
  bool started_ = false;
  bool enrollmentPending_ = false;
  String pendingName_;
  String lastUid_;
  String lastMessage_ = "Ready";
  bool calibrationStartRequested_ = false;
  bool calibrationStopRequested_ = false;
  bool calibrationActive_ = false;
  float calibrationKnownGallons_ = 0.0F;
  uint32_t calibrationPulses_ = 0;
  String calibrationMessage_ = "Ready to calibrate";
  bool musicCalibrationStartRequested_ = false;
  bool musicCalibrationCaptureRequested_ = false;
  bool musicCalibrationCancelRequested_ = false;
  bool musicCalibrationActive_ = false;
  uint16_t musicKnobRaw_ = 0;
  int8_t musicChannel_ = 0;
  uint8_t musicCalibrationNextPosition_ = 0;
  String musicCalibrationMessage_ = "Ready to calibrate";
  bool hubReady_ = false;
  bool relayReady_ = false;
  bool rfidReady_ = false;
  uint8_t relayState_ = 0;
  bool relayTestActive_ = false;
  uint8_t relayTestChannel_ = 0;
  bool relayPolicyApplyRequested_ = false;
  bool relayTestStartRequested_ = false;
  bool relayTestStopRequested_ = false;
  uint8_t requestedRelayTestChannel_ = 0;
  char activeName_[33] = {0};
  float sessionGallons_ = 0.0F;
  float sessionLimit_ = 0.0F;
  bool pumpOn_ = false;
  uint8_t doorState_ = CampNet::DOOR_UNAVAILABLE;
  bool rebootRequested_ = false;
  uint32_t rebootReadyMs_ = 0;
  bool speakerSearchRequested_ = false;
  bool endSessionRequested_ = false;
  bool routesConfigured_ = false;
  uint32_t lastTelemetryMs_ = 0;
  size_t publishedRecentCount_ = 0;
  uint32_t publishedRecentEndMs_ = 0;
  bool recentPublished_ = false;
  File audioUploadFile_;
  bool audioUploadAuthorized_ = false;
  bool audioUploadFailed_ = false;
  size_t audioUploadBytes_ = 0;
};
