#pragma once

#include <Arduino.h>

#include "CampNetProtocol.h"

class MemberRegistry {
 public:
  static constexpr size_t MAX_MEMBERS = 100;
  static_assert(MAX_MEMBERS <= CampNet::MEMBER_ENTRIES_PER_PACKET * 32,
                "CampNet member chunk mask is too small");

  bool begin();
  bool upsert(const char* uid, const String& name);
  bool update(const char* uid, const String& name, float allowanceGallons,
              bool enabled);
  bool remove(const char* uid);

  const char* nameFor(const char* uid) const;
  size_t count() const { return memberCount_; }
  const char* uidAt(size_t index) const;
  const char* nameAt(size_t index) const;
  float allowanceFor(const char* uid) const;
  float allowanceAt(size_t index) const;
  bool enabledFor(const char* uid) const;
  bool enabledAt(size_t index) const;
  bool healthy() const { return healthy_; }

  // CampNet sync. The registry carries a version that every local edit bumps
  // past anything seen on the network. A remote snapshot with a newer version
  // replaces the table; an equal version with different content is merged by
  // union and re-versioned so every station converges on the same set.
  uint32_t version() const { return version_; }
  void noteRemoteVersion(uint32_t version);
  size_t fillSnapshot(CampNet::MemberEntry* entries, size_t capacity) const;
  // Returns true if the local table changed (caller should rebroadcast).
  bool applyRemoteSnapshot(uint8_t fromStationId, uint32_t version,
                           const CampNet::MemberEntry* entries, size_t count);

 private:
  static constexpr size_t UID_SIZE = 21;
  static constexpr size_t NAME_SIZE = 33;

  struct Member {
    char uid[UID_SIZE] = {0};
    char name[NAME_SIZE] = {0};
    float allowanceGallons = 0.0F;
    bool enabled = true;
  };

  int find(const char* uid) const;
  String cleanName(const String& name) const;
  bool save();
  void loadVersion();
  bool bumpVersionAndSave();
  bool sameAs(const CampNet::MemberEntry* entries, size_t count) const;
  static bool entryToMember(const CampNet::MemberEntry& entry, Member& member);

  bool allocate();

  Member* members_ = nullptr;   // PSRAM, MAX_MEMBERS
  Member* scratch_ = nullptr;   // PSRAM, MAX_MEMBERS; rollback copy for snapshots
  size_t memberCount_ = 0;
  uint32_t version_ = 0;
  uint32_t highestSeenVersion_ = 0;
  bool healthy_ = false;
};
