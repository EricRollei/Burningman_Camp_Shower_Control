#pragma once

#include <Arduino.h>

#include "Config.h"

class SettingsStore {
 public:
  struct RoleLimits {
    float gallons = 0.0F;
    uint16_t minutes = 0;
  };

  struct RelayConfig {
    uint8_t pump = Config::DEFAULT_PUMP_RELAY;
    uint8_t charger = Config::DEFAULT_CHARGER_RELAY;
    uint8_t accessory = Config::DEFAULT_ACCESSORY_RELAY;
    bool accessoryEnabled = Config::DEFAULT_ACCESSORY_ENABLED;
  };

  bool begin();
  bool setCalibration(float pulsesPerGallon);
  bool setMusicKnobCalibration(
      const uint16_t positions[Config::MUSIC_KNOB_POSITION_COUNT]);
  bool setSpeakerVolumePercent(uint8_t percent);
  bool setRelayConfig(const RelayConfig& config);
  bool setAccessoryEnabled(bool enabled);
  const RelayConfig& relayConfig() const { return relayConfig_; }
  static bool relayConfigValid(const RelayConfig& config);
  bool setPassword(const String& password);
  bool verifyPassword(const String& password) const;

  // Admin password sync across stations, versioned like limits. The initial
  // password a fresh card is seeded with stays at version 0 so any real
  // change made anywhere in camp wins over it.
  uint32_t authVersion() const { return authVersion_; }
  const uint8_t* salt() const { return salt_; }
  const uint8_t* passwordHash() const { return passwordHash_; }
  // Network update. Adopts a strictly newer version, or an equal version with
  // different content from a lower station id. Returns true if adopted.
  bool applyRemoteAuth(uint32_t version, uint8_t fromStationId,
                       const uint8_t salt[16], const uint8_t hash[32]);

  // Per-role session limits, synced across stations by version number.
  const RoleLimits& roleLimits(uint8_t role) const;
  uint32_t limitsVersion() const { return limitsVersion_; }
  // Local admin edit: validates, bumps the version past anything seen on the
  // network, and saves. Returns false if any value is out of bounds.
  bool setRoleLimits(const RoleLimits limits[CampNet::ROLE_COUNT]);
  // Network update. Adopts a strictly newer version, or an equal version from
  // a lower station id (deterministic tie-break). Returns true if adopted.
  bool applyRemoteLimits(uint32_t version, uint8_t fromStationId,
                         const RoleLimits limits[CampNet::ROLE_COUNT]);
  static bool limitsValid(const RoleLimits limits[CampNet::ROLE_COUNT]);

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
  RelayConfig relayConfig_;
  RoleLimits roleLimits_[CampNet::ROLE_COUNT];
  uint32_t limitsVersion_ = 0;
  uint32_t highestSeenLimitsVersion_ = 0;
  uint32_t authVersion_ = 0;
  uint32_t highestSeenAuthVersion_ = 0;
  bool hasPassword_ = false;
  bool musicKnobCalibrated_ = false;
  bool healthy_ = false;
};
