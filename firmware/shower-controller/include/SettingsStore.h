#pragma once

#include <Arduino.h>

class SettingsStore {
 public:
  bool begin();
  bool setCalibration(float pulsesPerGallon);
  bool setPassword(const String& password);
  bool verifyPassword(const String& password) const;

  bool healthy() const { return healthy_; }
  float pulsesPerGallon() const { return pulsesPerGallon_; }

 private:
  bool save();
  void computeHash(const String& password, uint8_t output[32]) const;
  static String toHex(const uint8_t* bytes, size_t length);
  static bool fromHex(const String& hex, uint8_t* bytes, size_t length);

  float pulsesPerGallon_ = 0.0F;
  uint8_t salt_[16] = {0};
  uint8_t passwordHash_[32] = {0};
  bool hasPassword_ = false;
  bool healthy_ = false;
};
