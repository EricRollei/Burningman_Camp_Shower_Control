#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "MemberRegistry.h"
#include "PulseStorage.h"

class AdminServer {
 public:
  AdminServer(MemberRegistry& registry, const PulseStorage& storage);

  bool begin();
  void handle();
  bool onTagScanned(const String& uid);

  bool enrollmentPending() const { return enrollmentPending_; }
  const String& pendingName() const { return pendingName_; }
  String address() const;

 private:
  void configureRoutes();
  void sendStatus();
  void sendMembers();
  void armEnrollment();
  void cancelEnrollment();
  void renameMember();
  void deleteMember();
  void sendJsonMessage(int code, bool ok, const String& message);
  static String jsonEscape(const String& value);

  WebServer server_{80};
  MemberRegistry& registry_;
  const PulseStorage& storage_;
  bool started_ = false;
  bool enrollmentPending_ = false;
  String pendingName_;
  String lastUid_;
  String lastMessage_ = "Ready";
};
