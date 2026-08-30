#include "SessionStorage.h"

#include <SD.h>

#include "Config.h"
#include "PsramAlloc.h"

bool SessionStorage::begin() {
  if (totals_ == nullptr) totals_ = psramArray<Total>(MAX_TOTALS);
  if (recent_ == nullptr) recent_ = psramArray<Record>(MAX_RECENT);
  if (totals_ == nullptr || recent_ == nullptr) return false;
  totalCount_ = 0;
  recentCount_ = 0;
  recentNext_ = 0;
  if (SD.cardType() == CARD_NONE) return false;
  if (!SD.exists(Config::SESSION_PATH)) {
    File file = SD.open(Config::SESSION_PATH, FILE_WRITE);
    if (!file) return false;
    file.println("start_ms,end_ms,uid,pulses,gallons,allowance_gallons,end_reason");
    file.close();
  }
  healthy_ = true;
  restore();
  return healthy_;
}

bool SessionStorage::append(uint32_t startMs, uint32_t endMs, const char* uid,
                            uint32_t pulses, float gallons, float allowance,
                            const char* reason) {
  if (!healthy_ || uid == nullptr || reason == nullptr) return false;
  File file = SD.open(Config::SESSION_PATH, FILE_APPEND);
  if (!file) {
    healthy_ = false;
    return false;
  }
  file.printf("%lu,%lu,%s,%lu,%.4f,%.3f,%s\n",
              static_cast<unsigned long>(startMs),
              static_cast<unsigned long>(endMs), uid,
              static_cast<unsigned long>(pulses), gallons, allowance, reason);
  file.flush();
  file.close();

  Record record;
  record.startMs = startMs;
  record.endMs = endMs;
  strlcpy(record.uid, uid, sizeof(record.uid));
  record.pulses = pulses;
  record.gallons = gallons;
  record.allowance = allowance;
  strlcpy(record.reason, reason, sizeof(record.reason));
  const int index = ensureTotal(uid);
  if (index >= 0) {
    totals_[index].gallons += gallons;
    ++totals_[index].sessions;
  }
  remember(record);
  return true;
}

float SessionStorage::gallonsFor(const char* uid) const {
  for (size_t i = 0; i < totalCount_; ++i) {
    if (strcmp(totals_[i].uid, uid) == 0) return totals_[i].gallons;
  }
  return 0.0F;
}

uint32_t SessionStorage::sessionsFor(const char* uid) const {
  for (size_t i = 0; i < totalCount_; ++i) {
    if (strcmp(totals_[i].uid, uid) == 0) return totals_[i].sessions;
  }
  return 0;
}

const SessionStorage::Record& SessionStorage::recentAt(size_t index) const {
  static Record empty;
  if (recent_ == nullptr || index >= recentCount_) return empty;
  const size_t oldest = recentCount_ < MAX_RECENT ? 0 : recentNext_;
  const size_t physical = (oldest + recentCount_ - 1 - index) % MAX_RECENT;
  return recent_[physical];
}

int SessionStorage::ensureTotal(const char* uid) {
  for (size_t i = 0; i < totalCount_; ++i) {
    if (strcmp(totals_[i].uid, uid) == 0) return static_cast<int>(i);
  }
  if (totalCount_ >= MAX_TOTALS || strlen(uid) >= sizeof(totals_[0].uid)) return -1;
  strlcpy(totals_[totalCount_].uid, uid, sizeof(totals_[totalCount_].uid));
  return static_cast<int>(totalCount_++);
}

void SessionStorage::remember(const Record& record) {
  recent_[recentNext_] = record;
  recentNext_ = (recentNext_ + 1) % MAX_RECENT;
  if (recentCount_ < MAX_RECENT) ++recentCount_;
}

void SessionStorage::restore() {
  File file = SD.open(Config::SESSION_PATH, FILE_READ);
  if (!file) {
    healthy_ = false;
    return;
  }
  bool firstLine = true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (firstLine) {
      firstLine = false;
      continue;
    }
    Record record;
    unsigned long start = 0, end = 0, pulses = 0;
    const int fields = sscanf(line.c_str(), "%lu,%lu,%20[^,],%lu,%f,%f,%15s",
                              &start, &end, record.uid, &pulses,
                              &record.gallons, &record.allowance, record.reason);
    if (fields != 7) continue;
    record.startMs = start;
    record.endMs = end;
    record.pulses = pulses;
    const int index = ensureTotal(record.uid);
    if (index >= 0) {
      totals_[index].gallons += record.gallons;
      ++totals_[index].sessions;
    }
    remember(record);
  }
  file.close();
}
