#include "SettingsStore.h"

#include <SD.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include "Config.h"

bool SettingsStore::begin() {
  if (SD.cardType() == CARD_NONE) return false;

  bool loadedCalibration = false;
  bool loadedMusicPositions[Config::MUSIC_KNOB_POSITION_COUNT] = {false};
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    roleLimits_[role].gallons = Config::DEFAULT_ROLE_LIMIT_GALLONS[role];
    roleLimits_[role].minutes = Config::DEFAULT_ROLE_LIMIT_MINUTES[role];
  }
  if (SD.exists(Config::SETTINGS_PATH)) {
    File file = SD.open(Config::SETTINGS_PATH, FILE_READ);
    if (!file) return false;
    bool firstLine = true;
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (firstLine) {
        firstLine = false;
        continue;
      }
      const int comma = line.indexOf(',');
      if (comma <= 0) continue;
      const String key = line.substring(0, comma);
      const String value = line.substring(comma + 1);
      if (key == "calibration_ppg") {
        const float parsed = value.toFloat();
        if (parsed > 0.0F) {
          pulsesPerGallon_ = parsed;
          loadedCalibration = true;
        }
      } else if (key.startsWith("music_knob_")) {
        const int index = key.substring(11).toInt();
        const long parsed = value.toInt();
        if (index >= 0 && index < Config::MUSIC_KNOB_POSITION_COUNT &&
            parsed >= 0 && parsed <= 4095) {
          musicKnobPositions_[index] = static_cast<uint16_t>(parsed);
          loadedMusicPositions[index] = true;
        }
      } else if (key == "speaker_volume_percent") {
        const long parsed = value.toInt();
        if (parsed >= 0 && parsed <= 100) {
          speakerVolumePercent_ = static_cast<uint8_t>(parsed);
        }
      } else if (key.startsWith("limit_") && key.endsWith("_gal")) {
        const String roleKey = key.substring(6, key.length() - 4);
        const float parsed = value.toFloat();
        const int role = roleKey == "shower" ? 0 : roleKey == "water" ? 1 : roleKey == "rv" ? 2 : -1;
        if (role >= 0 && parsed >= Config::MIN_LIMIT_GALLONS && parsed <= Config::MAX_LIMIT_GALLONS) {
          roleLimits_[role].gallons = parsed;
        }
      } else if (key.startsWith("limit_") && key.endsWith("_min")) {
        const String roleKey = key.substring(6, key.length() - 4);
        const long parsed = value.toInt();
        const int role = roleKey == "shower" ? 0 : roleKey == "water" ? 1 : roleKey == "rv" ? 2 : -1;
        if (role >= 0 && parsed >= Config::MIN_LIMIT_MINUTES && parsed <= Config::MAX_LIMIT_MINUTES) {
          roleLimits_[role].minutes = static_cast<uint16_t>(parsed);
        }
      } else if (key == "limits_version") {
        limitsVersion_ = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
        highestSeenLimitsVersion_ = limitsVersion_;
      } else if (key == "admin_salt") {
        hasPassword_ = fromHex(value, salt_, sizeof(salt_));
      } else if (key == "admin_hash") {
        hasPassword_ = hasPassword_ &&
                       fromHex(value, passwordHash_, sizeof(passwordHash_));
      }
    }
    file.close();
  }

  musicKnobCalibrated_ = true;
  for (bool loaded : loadedMusicPositions) musicKnobCalibrated_ &= loaded;
  if (musicKnobCalibrated_) {
    const bool ascending = musicKnobPositions_[Config::MUSIC_KNOB_POSITION_COUNT - 1] >
                           musicKnobPositions_[0];
    const uint16_t span = ascending
                              ? musicKnobPositions_[Config::MUSIC_KNOB_POSITION_COUNT - 1] -
                                    musicKnobPositions_[0]
                              : musicKnobPositions_[0] -
                                    musicKnobPositions_[Config::MUSIC_KNOB_POSITION_COUNT - 1];
    if (span < Config::MUSIC_KNOB_MIN_CALIBRATION_SPAN) {
      musicKnobCalibrated_ = false;
    }
    for (uint8_t i = 1; i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
      const int difference = static_cast<int>(musicKnobPositions_[i]) -
                             static_cast<int>(musicKnobPositions_[i - 1]);
      if ((ascending && difference < Config::MUSIC_KNOB_MIN_POINT_SPACING) ||
          (!ascending && difference > -static_cast<int>(Config::MUSIC_KNOB_MIN_POINT_SPACING))) {
        musicKnobCalibrated_ = false;
        break;
      }
    }
  }

  if (!loadedCalibration) pulsesPerGallon_ = Config::DEFAULT_PULSES_PER_GALLON;
  healthy_ = true;
  if (!hasPassword_) {
    for (uint8_t& value : salt_) value = static_cast<uint8_t>(esp_random());
    computeHash(Config::INITIAL_ADMIN_PASSWORD, passwordHash_);
    hasPassword_ = true;
    return save();
  }
  return loadedCalibration || save();
}

bool SettingsStore::setMusicKnobCalibration(
    const uint16_t positions[Config::MUSIC_KNOB_POSITION_COUNT]) {
  if (!healthy_ || positions == nullptr) return false;
  const bool ascending = positions[Config::MUSIC_KNOB_POSITION_COUNT - 1] > positions[0];
  const uint16_t span = ascending
                            ? positions[Config::MUSIC_KNOB_POSITION_COUNT - 1] - positions[0]
                            : positions[0] - positions[Config::MUSIC_KNOB_POSITION_COUNT - 1];
  if (span < Config::MUSIC_KNOB_MIN_CALIBRATION_SPAN) return false;
  for (uint8_t i = 1; i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
    const int difference = static_cast<int>(positions[i]) -
                           static_cast<int>(positions[i - 1]);
    if ((ascending && difference < Config::MUSIC_KNOB_MIN_POINT_SPACING) ||
        (!ascending && difference > -static_cast<int>(Config::MUSIC_KNOB_MIN_POINT_SPACING))) {
      return false;
    }
  }

  uint16_t previous[Config::MUSIC_KNOB_POSITION_COUNT];
  memcpy(previous, musicKnobPositions_, sizeof(previous));
  const bool wasCalibrated = musicKnobCalibrated_;
  memcpy(musicKnobPositions_, positions, sizeof(musicKnobPositions_));
  musicKnobCalibrated_ = true;
  if (save()) return true;
  memcpy(musicKnobPositions_, previous, sizeof(musicKnobPositions_));
  musicKnobCalibrated_ = wasCalibrated;
  return false;
}

uint16_t SettingsStore::musicKnobPosition(uint8_t index) const {
  return index < Config::MUSIC_KNOB_POSITION_COUNT ? musicKnobPositions_[index] : 0;
}

uint8_t SettingsStore::nearestMusicPosition(uint16_t raw) const {
  if (!musicKnobCalibrated_) return raw >= Config::MUSIC_KNOB_ON_RAW ? 1 : 0;
  uint8_t nearest = 0;
  uint16_t nearestDistance = static_cast<uint16_t>(
      abs(static_cast<int>(raw) - static_cast<int>(musicKnobPositions_[0])));
  for (uint8_t i = 1; i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
    const uint16_t distance = static_cast<uint16_t>(
        abs(static_cast<int>(raw) - static_cast<int>(musicKnobPositions_[i])));
    if (distance < nearestDistance) {
      nearest = i;
      nearestDistance = distance;
    }
  }
  return nearest;
}

bool SettingsStore::setCalibration(float pulsesPerGallon) {
  if (!healthy_ || pulsesPerGallon <= 0.0F || !isfinite(pulsesPerGallon)) {
    return false;
  }
  const float previous = pulsesPerGallon_;
  pulsesPerGallon_ = pulsesPerGallon;
  if (save()) return true;
  pulsesPerGallon_ = previous;
  return false;
}

bool SettingsStore::setSpeakerVolumePercent(uint8_t percent) {
  if (!healthy_ || percent > 100) return false;
  const uint8_t previous = speakerVolumePercent_;
  speakerVolumePercent_ = percent;
  if (save()) return true;
  speakerVolumePercent_ = previous;
  return false;
}

const SettingsStore::RoleLimits& SettingsStore::roleLimits(uint8_t role) const {
  static RoleLimits fallback;
  return role < CampNet::ROLE_COUNT ? roleLimits_[role] : fallback;
}

bool SettingsStore::limitsValid(const RoleLimits limits[CampNet::ROLE_COUNT]) {
  if (limits == nullptr) return false;
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    if (!isfinite(limits[role].gallons) ||
        limits[role].gallons < Config::MIN_LIMIT_GALLONS ||
        limits[role].gallons > Config::MAX_LIMIT_GALLONS ||
        limits[role].minutes < Config::MIN_LIMIT_MINUTES ||
        limits[role].minutes > Config::MAX_LIMIT_MINUTES) {
      return false;
    }
  }
  return true;
}

bool SettingsStore::setRoleLimits(const RoleLimits limits[CampNet::ROLE_COUNT]) {
  if (!healthy_ || !limitsValid(limits)) return false;
  RoleLimits previous[CampNet::ROLE_COUNT];
  memcpy(previous, roleLimits_, sizeof(previous));
  const uint32_t previousVersion = limitsVersion_;
  memcpy(roleLimits_, limits, sizeof(roleLimits_));
  limitsVersion_ = max(limitsVersion_, highestSeenLimitsVersion_) + 1;
  highestSeenLimitsVersion_ = limitsVersion_;
  if (save()) return true;
  memcpy(roleLimits_, previous, sizeof(roleLimits_));
  limitsVersion_ = previousVersion;
  return false;
}

bool SettingsStore::applyRemoteLimits(uint32_t version, uint8_t fromStationId,
                                      const RoleLimits limits[CampNet::ROLE_COUNT]) {
  if (!limitsValid(limits)) return false;
  if (version > highestSeenLimitsVersion_) highestSeenLimitsVersion_ = version;
  const bool same = memcmp(limits, roleLimits_, sizeof(roleLimits_)) == 0;
  const bool newer = version > limitsVersion_;
  const bool tieBreak = version == limitsVersion_ && !same &&
                        fromStationId < Config::STATION_ID_VALUE;
  if (!newer && !tieBreak) return false;
  if (!healthy_) return false;
  RoleLimits previous[CampNet::ROLE_COUNT];
  memcpy(previous, roleLimits_, sizeof(previous));
  const uint32_t previousVersion = limitsVersion_;
  memcpy(roleLimits_, limits, sizeof(roleLimits_));
  limitsVersion_ = version;
  if (save()) return true;
  memcpy(roleLimits_, previous, sizeof(roleLimits_));
  limitsVersion_ = previousVersion;
  return false;
}

bool SettingsStore::setPassword(const String& password) {
  if (!healthy_ || password.length() < 8 || password.length() > 64) return false;
  uint8_t previousSalt[sizeof(salt_)];
  uint8_t previousHash[sizeof(passwordHash_)];
  memcpy(previousSalt, salt_, sizeof(salt_));
  memcpy(previousHash, passwordHash_, sizeof(passwordHash_));
  for (uint8_t& value : salt_) value = static_cast<uint8_t>(esp_random());
  computeHash(password, passwordHash_);
  if (save()) return true;
  memcpy(salt_, previousSalt, sizeof(salt_));
  memcpy(passwordHash_, previousHash, sizeof(passwordHash_));
  return false;
}

bool SettingsStore::verifyPassword(const String& password) const {
  if (!hasPassword_) return false;
  uint8_t candidate[32];
  computeHash(password, candidate);
  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(candidate); ++i) {
    difference |= candidate[i] ^ passwordHash_[i];
  }
  return difference == 0;
}

bool SettingsStore::save() {
  constexpr char tempPath[] = "/SETTINGS.TMP";
  constexpr char backupPath[] = "/SETTINGS.BAK";
  SD.remove(tempPath);
  SD.remove(backupPath);
  File file = SD.open(tempPath, FILE_WRITE);
  if (!file) return false;
  file.println("key,value");
  file.printf("calibration_ppg,%.6f\n", pulsesPerGallon_);
  file.printf("speaker_volume_percent,%u\n", speakerVolumePercent_);
  if (musicKnobCalibrated_) {
    for (uint8_t i = 0; i < Config::MUSIC_KNOB_POSITION_COUNT; ++i) {
      file.printf("music_knob_%u,%u\n", i, musicKnobPositions_[i]);
    }
  }
  static const char* const roleKeys[CampNet::ROLE_COUNT] = {"shower", "water", "rv"};
  for (uint8_t role = 0; role < CampNet::ROLE_COUNT; ++role) {
    file.printf("limit_%s_gal,%.3f\n", roleKeys[role], roleLimits_[role].gallons);
    file.printf("limit_%s_min,%u\n", roleKeys[role], roleLimits_[role].minutes);
  }
  file.printf("limits_version,%lu\n", static_cast<unsigned long>(limitsVersion_));
  file.printf("admin_salt,%s\n", toHex(salt_, sizeof(salt_)).c_str());
  file.printf("admin_hash,%s\n", toHex(passwordHash_, sizeof(passwordHash_)).c_str());
  file.flush();
  file.close();
  if (SD.exists(Config::SETTINGS_PATH) &&
      !SD.rename(Config::SETTINGS_PATH, backupPath)) {
    SD.remove(tempPath);
    return false;
  }
  if (!SD.rename(tempPath, Config::SETTINGS_PATH)) {
    if (SD.exists(backupPath)) SD.rename(backupPath, Config::SETTINGS_PATH);
    return false;
  }
  SD.remove(backupPath);
  return true;
}

void SettingsStore::computeHash(const String& password, uint8_t output[32]) const {
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts_ret(&context, 0);
  mbedtls_sha256_update_ret(&context, salt_, sizeof(salt_));
  mbedtls_sha256_update_ret(&context,
                            reinterpret_cast<const unsigned char*>(password.c_str()),
                            password.length());
  mbedtls_sha256_finish_ret(&context, output);
  mbedtls_sha256_free(&context);
}

String SettingsStore::toHex(const uint8_t* bytes, size_t length) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  String result;
  result.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    result += HEX_DIGITS[bytes[i] >> 4];
    result += HEX_DIGITS[bytes[i] & 0x0F];
  }
  return result;
}

bool SettingsStore::fromHex(const String& hex, uint8_t* bytes, size_t length) {
  if (hex.length() != length * 2) return false;
  for (size_t i = 0; i < length; ++i) {
    char pair[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* end = nullptr;
    const long value = strtol(pair, &end, 16);
    if (end == nullptr || *end != 0) return false;
    bytes[i] = static_cast<uint8_t>(value);
  }
  return true;
}
