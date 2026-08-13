#include "SettingsStore.h"

#include <SD.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include "Config.h"

bool SettingsStore::begin() {
  if (SD.cardType() == CARD_NONE) return false;

  bool loadedCalibration = false;
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
      } else if (key == "admin_salt") {
        hasPassword_ = fromHex(value, salt_, sizeof(salt_));
      } else if (key == "admin_hash") {
        hasPassword_ = hasPassword_ &&
                       fromHex(value, passwordHash_, sizeof(passwordHash_));
      }
    }
    file.close();
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
