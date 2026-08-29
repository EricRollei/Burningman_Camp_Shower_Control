#pragma once

#include <Arduino.h>

#include "CampNetProtocol.h"

// Latest per-wristband totals reported by every *other* station on CampNet.
// Combined with this station's own SessionStorage totals it yields a member's
// camp-wide usage. Entries are idempotent upserts; the table is persisted to
// the SD card (debounced) so totals survive a reboot while a peer is off.
class UsageLedger {
 public:
  bool begin();
  void handle();

  // Returns true if the stored value changed.
  bool upsert(uint8_t stationId, uint8_t role, const char* uidHex, float gallons,
              uint32_t sessions);
  float remoteGallonsFor(const char* uidHex) const;
  float remoteGallonsByRole(const char* uidHex, uint8_t role) const;
  uint32_t remoteSessionsFor(const char* uidHex) const;
  size_t entryCount() const { return entryCount_; }
  bool healthy() const { return healthy_; }

 private:
  static constexpr size_t MAX_ENTRIES = CampNet::MAX_STATIONS * 64;
  static constexpr size_t UID_SIZE = 21;
  struct Entry {
    uint8_t stationId = 0;
    uint8_t role = 0;
    char uid[UID_SIZE] = {0};
    float gallons = 0.0F;
    uint32_t sessions = 0;
  };

  int find(uint8_t stationId, const char* uidHex) const;
  bool save();
  void restore();

  Entry entries_[MAX_ENTRIES];
  size_t entryCount_ = 0;
  bool dirty_ = false;
  bool healthy_ = false;
  uint32_t lastChangeMs_ = 0;
};
