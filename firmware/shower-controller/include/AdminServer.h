#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "MemberRegistry.h"
#include "PulseStorage.h"
#include "SessionStorage.h"
#include "SettingsStore.h"
#include "SpeakerAudio.h"

class AdminServer {
 public:
  AdminServer(MemberRegistry& registry, const PulseStorage& pulseStorage,
              const SessionStorage& sessions, SettingsStore& settings,
              SpeakerAudio& speakerAudio);

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
  bool takeRebootRequest();
  bool takeSpeakerSearchRequest();
  bool started() const { return started_; }

 private:
  void configureRoutes();
  String statusJson() const;
  String membersJson() const;
  String sessionsJson() const;
  String healthJson() const;
  void armEnrollment();
  void cancelEnrollment();
  void renameMember();
  void updateMember();
  void deleteMember();
  void changePassword();
  void startCalibration();
  void stopCalibration();
  void startMusicCalibration();
  void captureMusicCalibration();
  void cancelMusicCalibration();
  void setSpeakerVolume();
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
  bool rebootRequested_ = false;
  bool speakerSearchRequested_ = false;
  bool routesConfigured_ = false;
  File audioUploadFile_;
  bool audioUploadAuthorized_ = false;
  bool audioUploadFailed_ = false;
  size_t audioUploadBytes_ = 0;
};
