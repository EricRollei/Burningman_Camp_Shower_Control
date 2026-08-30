#include "MemberRegistry.h"

#include <SD.h>

#include "Config.h"
#include "PsramAlloc.h"

bool MemberRegistry::allocate() {
  if (members_ == nullptr) members_ = psramArray<Member>(MAX_MEMBERS);
  if (scratch_ == nullptr) scratch_ = psramArray<Member>(MAX_MEMBERS);
  return members_ != nullptr && scratch_ != nullptr;
}

bool MemberRegistry::begin() {
  memberCount_ = 0;
  if (!allocate()) return false;
  if (SD.cardType() == CARD_NONE) return false;

  // Recover the previous complete file if power was lost during replacement.
  if (!SD.exists(Config::MEMBER_PATH) && SD.exists("/MEMBERS.BAK")) {
    SD.rename("/MEMBERS.BAK", Config::MEMBER_PATH);
  }

  if (!SD.exists(Config::MEMBER_PATH)) {
    File file = SD.open(Config::MEMBER_PATH, FILE_WRITE);
    if (!file) return false;
    file.println("uid,name,allowance_gallons,enabled");
    file.close();
  }

  File file = SD.open(Config::MEMBER_PATH, FILE_READ);
  if (!file) return false;

  bool firstLine = true;
  while (file.available() && memberCount_ < MAX_MEMBERS) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (firstLine) {
      firstLine = false;
      continue;
    }
    const int comma = line.indexOf(',');
    if (comma <= 0) continue;
    const int second = line.indexOf(',', comma + 1);
    const int third = second >= 0 ? line.indexOf(',', second + 1) : -1;
    const String uid = line.substring(0, comma);
    const String name = cleanName(second >= 0 ? line.substring(comma + 1, second)
                                               : line.substring(comma + 1));
    if (uid.length() >= UID_SIZE || name.isEmpty()) continue;
    strlcpy(members_[memberCount_].uid, uid.c_str(), UID_SIZE);
    strlcpy(members_[memberCount_].name, name.c_str(), NAME_SIZE);
    members_[memberCount_].allowanceGallons = Config::DEFAULT_ALLOWANCE_GALLONS;
    members_[memberCount_].enabled = true;
    if (second >= 0) {
      const float allowance = line.substring(second + 1, third >= 0 ? third : line.length()).toFloat();
      if (allowance > 0.0F && isfinite(allowance)) members_[memberCount_].allowanceGallons = allowance;
    }
    if (third >= 0) members_[memberCount_].enabled = line.substring(third + 1).toInt() != 0;
    ++memberCount_;
  }
  file.close();
  healthy_ = true;
  loadVersion();
  return true;
}

bool MemberRegistry::upsert(const char* uid, const String& requestedName) {
  if (!healthy_ || uid == nullptr || strlen(uid) == 0 ||
      strlen(uid) >= UID_SIZE) {
    return false;
  }
  const String name = cleanName(requestedName);
  if (name.isEmpty()) return false;

  int index = find(uid);
  if (index < 0) {
    if (memberCount_ >= MAX_MEMBERS) return false;
    index = static_cast<int>(memberCount_++);
    strlcpy(members_[index].uid, uid, UID_SIZE);
    members_[index].allowanceGallons = Config::DEFAULT_ALLOWANCE_GALLONS;
    members_[index].enabled = true;
  }
  strlcpy(members_[index].name, name.c_str(), NAME_SIZE);
  return bumpVersionAndSave();
}

bool MemberRegistry::update(const char* uid, const String& requestedName,
                            float allowanceGallons, bool enabled) {
  const int index = find(uid);
  const String name = cleanName(requestedName);
  // 0 means "use the station's role limit"; negative or non-finite is invalid.
  if (!healthy_ || index < 0 || name.isEmpty() || allowanceGallons < 0.0F ||
      !isfinite(allowanceGallons)) return false;
  const Member previous = members_[index];
  strlcpy(members_[index].name, name.c_str(), NAME_SIZE);
  members_[index].allowanceGallons = allowanceGallons;
  members_[index].enabled = enabled;
  if (bumpVersionAndSave()) return true;
  members_[index] = previous;
  return false;
}

bool MemberRegistry::remove(const char* uid) {
  const int index = find(uid);
  if (!healthy_ || index < 0) return false;
  for (size_t i = static_cast<size_t>(index); i + 1 < memberCount_; ++i) {
    members_[i] = members_[i + 1];
  }
  --memberCount_;
  return bumpVersionAndSave();
}

const char* MemberRegistry::nameFor(const char* uid) const {
  const int index = find(uid);
  return index >= 0 ? members_[index].name : nullptr;
}

const char* MemberRegistry::uidAt(size_t index) const {
  return index < memberCount_ ? members_[index].uid : nullptr;
}

const char* MemberRegistry::nameAt(size_t index) const {
  return index < memberCount_ ? members_[index].name : nullptr;
}

float MemberRegistry::allowanceFor(const char* uid) const {
  const int index = find(uid);
  return index >= 0 ? members_[index].allowanceGallons : 0.0F;
}

float MemberRegistry::allowanceAt(size_t index) const {
  return index < memberCount_ ? members_[index].allowanceGallons : 0.0F;
}

bool MemberRegistry::enabledFor(const char* uid) const {
  const int index = find(uid);
  return index >= 0 && members_[index].enabled;
}

bool MemberRegistry::enabledAt(size_t index) const {
  return index < memberCount_ && members_[index].enabled;
}

void MemberRegistry::noteRemoteVersion(uint32_t version) {
  if (version > highestSeenVersion_) highestSeenVersion_ = version;
}

size_t MemberRegistry::fillSnapshot(CampNet::MemberEntry* entries, size_t capacity) const {
  size_t written = 0;
  for (size_t i = 0; i < memberCount_ && written < capacity; ++i) {
    CampNet::MemberEntry& entry = entries[written];
    memset(&entry, 0, sizeof(entry));
    entry.uidLen = CampNet::uidFromHex(members_[i].uid, entry.uid);
    if (entry.uidLen == 0) continue;
    strlcpy(entry.name, members_[i].name, sizeof(entry.name));
    entry.allowance = members_[i].allowanceGallons;
    entry.enabled = members_[i].enabled ? 1 : 0;
    ++written;
  }
  return written;
}

bool MemberRegistry::entryToMember(const CampNet::MemberEntry& entry, Member& member) {
  if (entry.uidLen == 0 || entry.uidLen > CampNet::UID_BYTES) return false;
  char hex[CampNet::UID_BYTES * 2 + 1];
  CampNet::uidToHex(entry.uid, entry.uidLen, hex);
  strlcpy(member.uid, hex, sizeof(member.uid));
  char name[CampNet::NAME_BYTES];
  memcpy(name, entry.name, sizeof(name));
  name[sizeof(name) - 1] = '\0';
  strlcpy(member.name, name, sizeof(member.name));
  if (member.name[0] == '\0') return false;
  member.allowanceGallons =
      (isfinite(entry.allowance) && entry.allowance >= 0.0F) ? entry.allowance : 0.0F;
  member.enabled = entry.enabled != 0;
  return true;
}

bool MemberRegistry::sameAs(const CampNet::MemberEntry* entries, size_t count) const {
  if (count != memberCount_) return false;
  for (size_t i = 0; i < count; ++i) {
    Member candidate;
    if (!entryToMember(entries[i], candidate)) return false;
    const int index = find(candidate.uid);
    if (index < 0) return false;
    const Member& local = members_[index];
    if (strcmp(local.name, candidate.name) != 0 || local.enabled != candidate.enabled ||
        fabsf(local.allowanceGallons - candidate.allowanceGallons) > 0.001F) {
      return false;
    }
  }
  return true;
}

bool MemberRegistry::applyRemoteSnapshot(uint8_t fromStationId, uint32_t version,
                                         const CampNet::MemberEntry* entries,
                                         size_t count) {
  noteRemoteVersion(version);
  if (!healthy_ || entries == nullptr) return false;
  if (version < version_) return false;
  if (sameAs(entries, count)) {
    // Identical content: just catch up the version number so that a later
    // local edit sorts after this snapshot everywhere.
    if (version > version_) {
      version_ = version;
      saveVersion();
    }
    return false;
  }

  // Rollback copy lives in PSRAM (~4 KB is too much for the loop task's stack).
  Member* previous = scratch_;
  memcpy(previous, members_, sizeof(Member) * MAX_MEMBERS);
  const size_t previousCount = memberCount_;
  const uint32_t previousVersion = version_;

  if (version > version_) {
    // Strictly newer snapshot replaces the table wholesale (this is how
    // deletions and edits propagate).
    memberCount_ = 0;
    for (size_t i = 0; i < count && memberCount_ < MAX_MEMBERS; ++i) {
      Member member;
      if (!entryToMember(entries[i], member)) continue;
      if (find(member.uid) >= 0) continue;
      members_[memberCount_++] = member;
    }
    version_ = version;
    if (save() && saveVersion()) return true;
  } else {
    // Same version, different content (two stations edited before hearing
    // from each other, or two fresh SD cards). Merge by union so nobody's
    // wristband disappears, then re-version so the union wins everywhere.
    (void)fromStationId;
    for (size_t i = 0; i < count && memberCount_ < MAX_MEMBERS; ++i) {
      Member member;
      if (!entryToMember(entries[i], member)) continue;
      if (find(member.uid) >= 0) continue;
      members_[memberCount_++] = member;
    }
    if (bumpVersionAndSave()) return true;
  }

  memcpy(members_, previous, sizeof(Member) * MAX_MEMBERS);
  memberCount_ = previousCount;
  version_ = previousVersion;
  return false;
}

int MemberRegistry::find(const char* uid) const {
  if (uid == nullptr || members_ == nullptr) return -1;
  for (size_t i = 0; i < memberCount_; ++i) {
    if (strcmp(members_[i].uid, uid) == 0) return static_cast<int>(i);
  }
  return -1;
}

String MemberRegistry::cleanName(const String& requestedName) const {
  String name = requestedName;
  name.trim();
  String cleaned;
  cleaned.reserve(NAME_SIZE);
  for (size_t i = 0; i < name.length() && cleaned.length() < NAME_SIZE - 1;
       ++i) {
    const char value = name[i];
    if (value == ',') {
      cleaned += ' ';
    } else if (value >= 32 && value <= 126) {
      cleaned += value;
    }
  }
  cleaned.trim();
  return cleaned;
}

bool MemberRegistry::bumpVersionAndSave() {
  const uint32_t previousVersion = version_;
  version_ = max(version_, highestSeenVersion_) + 1;
  highestSeenVersion_ = version_;
  if (save() && saveVersion()) return true;
  version_ = previousVersion;
  return false;
}

void MemberRegistry::loadVersion() {
  version_ = 0;
  File file = SD.open(Config::MEMBER_VERSION_PATH, FILE_READ);
  if (!file) return;
  String line = file.readStringUntil('\n');
  file.close();
  line.trim();
  version_ = static_cast<uint32_t>(strtoul(line.c_str(), nullptr, 10));
  highestSeenVersion_ = max(highestSeenVersion_, version_);
}

bool MemberRegistry::saveVersion() {
  SD.remove(Config::MEMBER_VERSION_PATH);
  File file = SD.open(Config::MEMBER_VERSION_PATH, FILE_WRITE);
  if (!file) return false;
  file.println(static_cast<unsigned long>(version_));
  file.flush();
  file.close();
  return true;
}

bool MemberRegistry::save() {
  constexpr char tempPath[] = "/MEMBERS.TMP";
  constexpr char backupPath[] = "/MEMBERS.BAK";
  SD.remove(tempPath);
  SD.remove(backupPath);
  File file = SD.open(tempPath, FILE_WRITE);
  if (!file) return false;
  file.println("uid,name,allowance_gallons,enabled");
  for (size_t i = 0; i < memberCount_; ++i) {
    file.printf("%s,%s,%.3f,%u\n", members_[i].uid, members_[i].name,
                members_[i].allowanceGallons, members_[i].enabled ? 1 : 0);
  }
  file.flush();
  file.close();

  if (SD.exists(Config::MEMBER_PATH) &&
      !SD.rename(Config::MEMBER_PATH, backupPath)) {
    SD.remove(tempPath);
    healthy_ = false;
    return false;
  }
  if (!SD.rename(tempPath, Config::MEMBER_PATH)) {
    if (SD.exists(backupPath)) SD.rename(backupPath, Config::MEMBER_PATH);
    SD.remove(tempPath);
    healthy_ = false;
    return false;
  }
  SD.remove(backupPath);
  return true;
}
