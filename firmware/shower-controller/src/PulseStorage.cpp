#include "PulseStorage.h"

#include <SD.h>
#include <SPI.h>

#include "Config.h"

bool PulseStorage::begin() {
  SPI.begin(Config::SD_SCK, Config::SD_MISO, Config::SD_MOSI, Config::SD_CS);
  healthy_ = SD.begin(Config::SD_CS, SPI, Config::SD_FREQUENCY);
  if (!healthy_ || SD.cardType() == CARD_NONE) {
    healthy_ = false;
    return false;
  }

  cardSizeMB_ = SD.cardSize() / (1024ULL * 1024ULL);
  if (!SD.exists(Config::LOG_PATH)) {
    File file = SD.open(Config::LOG_PATH, FILE_WRITE);
    if (!file) {
      healthy_ = false;
      return false;
    }
    file.println("uptime_ms,uid,delta_pulses,tag_total_pulses,event");
    file.close();
  }

  restoreTotals();
  return healthy_;
}

bool PulseStorage::selectTag(const char* uid) {
  const int index = ensureTag(uid);
  return index >= 0 && append(uid, 0, totals_[index].pulses, "SELECT");
}

bool PulseStorage::recordPulses(const char* uid, uint32_t delta) {
  if (delta == 0) return true;
  const int index = ensureTag(uid);
  if (index < 0) return false;
  const uint64_t nextTotal = totals_[index].pulses + delta;
  if (!append(uid, delta, nextTotal, "PULSES")) return false;
  totals_[index].pulses = nextTotal;
  return true;
}

bool PulseStorage::endTag(const char* uid) {
  const int index = findTag(uid);
  return index >= 0 && append(uid, 0, totals_[index].pulses, "END");
}

uint64_t PulseStorage::totalFor(const char* uid) const {
  const int index = findTag(uid);
  return index >= 0 ? totals_[index].pulses : 0;
}

int PulseStorage::findTag(const char* uid) const {
  for (size_t i = 0; i < tagCount_; ++i) {
    if (strcmp(totals_[i].uid, uid) == 0) return static_cast<int>(i);
  }
  return -1;
}

int PulseStorage::ensureTag(const char* uid) {
  const int existing = findTag(uid);
  if (existing >= 0) return existing;
  if (tagCount_ >= MAX_TAGS || strlen(uid) >= UID_SIZE) return -1;

  strlcpy(totals_[tagCount_].uid, uid, UID_SIZE);
  totals_[tagCount_].pulses = 0;
  return static_cast<int>(tagCount_++);
}

bool PulseStorage::append(const char* uid, uint32_t delta, uint64_t total,
                          const char* event) {
  if (!healthy_) return false;
  File file = SD.open(Config::LOG_PATH, FILE_APPEND);
  if (!file) {
    healthy_ = false;
    return false;
  }

  file.printf("%lu,%s,%lu,%llu,%s\n", static_cast<unsigned long>(millis()),
              uid, static_cast<unsigned long>(delta), total, event);
  file.flush();
  file.close();
  return true;
}

void PulseStorage::restoreTotals() {
  File file = SD.open(Config::LOG_PATH, FILE_READ);
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

    unsigned long uptime = 0;
    char uid[UID_SIZE] = {0};
    unsigned long delta = 0;
    unsigned long long total = 0;
    char event[12] = {0};
    const int fields = sscanf(line.c_str(), "%lu,%20[^,],%lu,%llu,%11s",
                              &uptime, uid, &delta, &total, event);
    if (fields != 5) continue;

    const int index = ensureTag(uid);
    if (index >= 0) totals_[index].pulses = total;
  }
  file.close();
}
