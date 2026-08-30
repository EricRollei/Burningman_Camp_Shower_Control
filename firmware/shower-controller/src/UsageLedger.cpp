#include "UsageLedger.h"

#include <SD.h>

#include "Config.h"
#include "PsramAlloc.h"

bool UsageLedger::begin() {
  entryCount_ = 0;
  if (entries_ == nullptr) entries_ = psramArray<Entry>(MAX_ENTRIES);
  if (entries_ == nullptr) return false;
  if (SD.cardType() == CARD_NONE) return false;
  if (!SD.exists(Config::NET_USAGE_PATH) && SD.exists("/NETUSAGE.BAK")) {
    SD.rename("/NETUSAGE.BAK", Config::NET_USAGE_PATH);
  }
  healthy_ = true;
  restore();
  return healthy_;
}

void UsageLedger::handle() {
  if (!dirty_ || !healthy_) return;
  if (millis() - lastChangeMs_ < Config::NET_LEDGER_SAVE_DEBOUNCE_MS) return;
  if (save()) dirty_ = false;
  else lastChangeMs_ = millis();  // retry after another debounce window
}

bool UsageLedger::upsert(uint8_t stationId, uint8_t role, const char* uidHex,
                         float gallons, uint32_t sessions) {
  if (entries_ == nullptr || uidHex == nullptr || uidHex[0] == '\0' || strlen(uidHex) >= UID_SIZE ||
      stationId == 0 || stationId > CampNet::MAX_STATIONS ||
      stationId == Config::STATION_ID_VALUE || !isfinite(gallons) || gallons < 0.0F) {
    return false;
  }
  int index = find(stationId, uidHex);
  if (index < 0) {
    if (entryCount_ >= MAX_ENTRIES) return false;
    index = static_cast<int>(entryCount_++);
    entries_[index].stationId = stationId;
    strlcpy(entries_[index].uid, uidHex, UID_SIZE);
    entries_[index].gallons = -1.0F;
    entries_[index].sessions = 0;
  }
  Entry& entry = entries_[index];
  const bool changed = entry.role != role || entry.sessions != sessions ||
                       fabsf(entry.gallons - gallons) > 0.0005F;
  if (!changed) return false;
  entry.role = role;
  entry.gallons = gallons;
  entry.sessions = sessions;
  dirty_ = true;
  lastChangeMs_ = millis();
  return true;
}

float UsageLedger::remoteGallonsFor(const char* uidHex) const {
  float total = 0.0F;
  for (size_t i = 0; i < entryCount_; ++i) {
    if (strcmp(entries_[i].uid, uidHex) == 0) total += entries_[i].gallons;
  }
  return total;
}

float UsageLedger::remoteGallonsByRole(const char* uidHex, uint8_t role) const {
  float total = 0.0F;
  for (size_t i = 0; i < entryCount_; ++i) {
    if (entries_[i].role == role && strcmp(entries_[i].uid, uidHex) == 0) {
      total += entries_[i].gallons;
    }
  }
  return total;
}

uint32_t UsageLedger::remoteSessionsFor(const char* uidHex) const {
  uint32_t total = 0;
  for (size_t i = 0; i < entryCount_; ++i) {
    if (strcmp(entries_[i].uid, uidHex) == 0) total += entries_[i].sessions;
  }
  return total;
}

int UsageLedger::find(uint8_t stationId, const char* uidHex) const {
  for (size_t i = 0; i < entryCount_; ++i) {
    if (entries_[i].stationId == stationId && strcmp(entries_[i].uid, uidHex) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void UsageLedger::restore() {
  File file = SD.open(Config::NET_USAGE_PATH, FILE_READ);
  if (!file) return;  // No ledger yet is fine; peers rebroadcast within seconds.
  bool firstLine = true;
  while (file.available() && entryCount_ < MAX_ENTRIES) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (firstLine) {
      firstLine = false;
      continue;
    }
    unsigned station = 0, role = 0, sessions = 0;
    char uid[UID_SIZE] = {0};
    float gallons = 0.0F;
    if (sscanf(line.c_str(), "%u,%u,%20[^,],%f,%u", &station, &role, uid, &gallons,
               &sessions) != 5) continue;
    if (station == 0 || station > CampNet::MAX_STATIONS ||
        station == Config::STATION_ID_VALUE || role >= CampNet::ROLE_COUNT) continue;
    if (find(static_cast<uint8_t>(station), uid) >= 0) continue;
    Entry& entry = entries_[entryCount_++];
    entry.stationId = static_cast<uint8_t>(station);
    entry.role = static_cast<uint8_t>(role);
    strlcpy(entry.uid, uid, UID_SIZE);
    entry.gallons = gallons;
    entry.sessions = sessions;
  }
  file.close();
}

bool UsageLedger::save() {
  constexpr char tempPath[] = "/NETUSAGE.TMP";
  constexpr char backupPath[] = "/NETUSAGE.BAK";
  SD.remove(tempPath);
  SD.remove(backupPath);
  File file = SD.open(tempPath, FILE_WRITE);
  if (!file) return false;
  file.println("station_id,role,uid,gallons,sessions");
  for (size_t i = 0; i < entryCount_; ++i) {
    file.printf("%u,%u,%s,%.4f,%lu\n", entries_[i].stationId, entries_[i].role,
                entries_[i].uid, entries_[i].gallons,
                static_cast<unsigned long>(entries_[i].sessions));
  }
  file.flush();
  file.close();
  if (SD.exists(Config::NET_USAGE_PATH) && !SD.rename(Config::NET_USAGE_PATH, backupPath)) {
    SD.remove(tempPath);
    return false;
  }
  if (!SD.rename(tempPath, Config::NET_USAGE_PATH)) {
    if (SD.exists(backupPath)) SD.rename(backupPath, Config::NET_USAGE_PATH);
    return false;
  }
  SD.remove(backupPath);
  return true;
}
