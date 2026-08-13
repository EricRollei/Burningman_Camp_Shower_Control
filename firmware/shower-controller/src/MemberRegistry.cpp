#include "MemberRegistry.h"

#include <SD.h>

#include "Config.h"

bool MemberRegistry::begin() {
  memberCount_ = 0;
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
      if (allowance > 0.0F) members_[memberCount_].allowanceGallons = allowance;
    }
    if (third >= 0) members_[memberCount_].enabled = line.substring(third + 1).toInt() != 0;
    ++memberCount_;
  }
  file.close();
  healthy_ = true;
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
  return save();
}

bool MemberRegistry::update(const char* uid, const String& requestedName,
                            float allowanceGallons, bool enabled) {
  const int index = find(uid);
  const String name = cleanName(requestedName);
  if (!healthy_ || index < 0 || name.isEmpty() || allowanceGallons <= 0.0F ||
      !isfinite(allowanceGallons)) return false;
  const Member previous = members_[index];
  strlcpy(members_[index].name, name.c_str(), NAME_SIZE);
  members_[index].allowanceGallons = allowanceGallons;
  members_[index].enabled = enabled;
  if (save()) return true;
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
  return save();
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

int MemberRegistry::find(const char* uid) const {
  if (uid == nullptr) return -1;
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
