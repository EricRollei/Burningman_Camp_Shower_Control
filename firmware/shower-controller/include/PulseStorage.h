#pragma once

#include <Arduino.h>

class PulseStorage {
 public:
  bool begin();
  bool selectTag(const char* uid);
  bool recordPulses(const char* uid, uint32_t delta);
  bool endTag(const char* uid);
  uint64_t totalFor(const char* uid) const;

  bool healthy() const { return healthy_; }
  uint64_t cardSizeMB() const { return cardSizeMB_; }

 private:
  static constexpr size_t MAX_TAGS = 64;
  static constexpr size_t UID_SIZE = 21;

  struct TagTotal {
    char uid[UID_SIZE] = {0};
    uint64_t pulses = 0;
  };

  int findTag(const char* uid) const;
  int ensureTag(const char* uid);
  bool append(const char* uid, uint32_t delta, uint64_t total,
              const char* event);
  void restoreTotals();

  TagTotal totals_[MAX_TAGS];
  size_t tagCount_ = 0;
  bool healthy_ = false;
  uint64_t cardSizeMB_ = 0;
};

