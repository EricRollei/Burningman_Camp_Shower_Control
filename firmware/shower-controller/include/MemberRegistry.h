#pragma once

#include <Arduino.h>

class MemberRegistry {
 public:
  bool begin();
  bool upsert(const char* uid, const String& name);
  bool remove(const char* uid);

  const char* nameFor(const char* uid) const;
  size_t count() const { return memberCount_; }
  const char* uidAt(size_t index) const;
  const char* nameAt(size_t index) const;
  bool healthy() const { return healthy_; }

 private:
  static constexpr size_t MAX_MEMBERS = 64;
  static constexpr size_t UID_SIZE = 21;
  static constexpr size_t NAME_SIZE = 33;

  struct Member {
    char uid[UID_SIZE] = {0};
    char name[NAME_SIZE] = {0};
  };

  int find(const char* uid) const;
  String cleanName(const String& name) const;
  bool save();

  Member members_[MAX_MEMBERS];
  size_t memberCount_ = 0;
  bool healthy_ = false;
};
