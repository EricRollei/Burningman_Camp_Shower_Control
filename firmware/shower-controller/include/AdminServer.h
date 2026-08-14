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

 private:
  void configureRoutes();
  void sendStatus();
  void sendMembers();
  void armEnrollment();
  void cancelEnrollment();
  void renameMember();
  void updateMember();
  void deleteMember();
  void changePassword();
  void startCalibration();
  void stopCalibration();
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
  File audioUploadFile_;
  bool audioUploadAuthorized_ = false;
  bool audioUploadFailed_ = false;
  size_t audioUploadBytes_ = 0;
};
