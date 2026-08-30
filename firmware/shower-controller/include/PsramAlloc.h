#pragma once

#include <Arduino.h>

// The classic ESP32 has ~320 KB of internal DRAM and Bluetooth + Wi-Fi + lwIP
// leave only a few tens of KB for the application, while the Tough's 4 MB of
// PSRAM sits mostly idle. Every large CPU-only table (usage ledger, member
// registry, network rings) is allocated here instead of as a static array so
// it lands in PSRAM; internal RAM is the fallback when PSRAM is missing.
// Not for DMA buffers or anything touched from an ISR.
template <typename T>
T* psramArray(size_t count) {
  void* memory = nullptr;
  if (psramFound()) memory = ps_calloc(count, sizeof(T));
  if (memory == nullptr) memory = calloc(count, sizeof(T));
  return static_cast<T*>(memory);
}
