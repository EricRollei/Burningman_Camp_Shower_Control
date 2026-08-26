#pragma once

#include <Arduino.h>

#include "Config.h"

class SettingsStore {
 public:
  bool begin();
  bool setCalibration(float pulsesPerGallon);
  bool setMusicKnobCalibration(
      const uint16_t positions[Config::MUSIC_KNOB_POSITION_COUNT]);
  bool setSpeakerVolumePercent(uint8_t percent);
  bool setPassword(const String& password);
  bool verifyPassword(const String& password) const;

  bool healthy() const { return healthy_; }
  float pulsesPerGallon() const { return pulsesPerGallon_; }
  bool musicKnobCalibrated() const { return musicKnobCalibrated_; }
  uint16_t musicKnobPosition(uint8_t index) const;
  uint8_t nearestMusicPosition(uint16_t raw) const;
  uint8_t speakerVolumePercent() const { return speakerVolumePercent_; }

 private:
  bool save();
  void computeHash(const String& password, uint8_t output[32]) const;
  static String toHex(const uint8_t* bytes, size_t length);
  static bool fromHex(const String& hex, uint8_t* bytes, size_t length);

  float pulsesPerGallon_ = 0.0F;
  uint8_t salt_[16] = {0};
  uint8_t passwordHash_[32] = {0};
  uint16_t musicKnobPositions_[Config::MUSIC_KNOB_POSITION_COUNT] = {0};
  uint8_t speakerVolumePercent_ = Config::DEFAULT_SPEAKER_VOLUME_PERCENT;
  bool hasPassword_ = false;
  bool musicKnobCalibrated_ = false;
  bool healthy_ = false;
};
