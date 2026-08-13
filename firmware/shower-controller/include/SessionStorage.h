#pragma once

#include <Arduino.h>

class SessionStorage {
 public:
  struct Record {
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    char uid[21] = {0};
    uint32_t pulses = 0;
    float gallons = 0.0F;
    float allowance = 0.0F;
    char reason[16] = {0};
  };

  bool begin();
  bool append(uint32_t startMs, uint32_t endMs, const char* uid,
              uint32_t pulses, float gallons, float allowance,
              const char* reason);
  float gallonsFor(const char* uid) const;
  uint32_t sessionsFor(const char* uid) const;
  size_t recentCount() const { return recentCount_; }
  const Record& recentAt(size_t index) const;
  bool healthy() const { return healthy_; }

 private:
  static constexpr size_t MAX_TOTALS = 64;
  static constexpr size_t MAX_RECENT = 32;
  struct Total {
    char uid[21] = {0};
    float gallons = 0.0F;
    uint32_t sessions = 0;
  };
  int ensureTotal(const char* uid);
  void remember(const Record& record);
  void restore();

  Total totals_[MAX_TOTALS];
  size_t totalCount_ = 0;
  Record recent_[MAX_RECENT];
  size_t recentCount_ = 0;
  size_t recentNext_ = 0;
  bool healthy_ = false;
};
