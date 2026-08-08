#pragma once

#include <Arduino.h>
#include <Wire.h>

class RfidReader {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  int readUid(uint8_t* uid, int uidMax);
  void haltTag();

  bool healthy() const;
  uint8_t version();
  int lastError() const { return lastError_; }
  void clearError() { lastError_ = 0; }

 private:
  uint8_t readRegister(uint8_t reg);
  void writeRegister(uint8_t reg, uint8_t value);
  void initializeChip();
  bool calculateCrc(const uint8_t* data, int len, uint8_t* out);
  int transceive(const uint8_t* send, int sendLen, uint8_t* back,
                 int backMax, uint8_t txLastBits,
                 uint8_t* rxLastBits = nullptr);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  int lastError_ = 0;
};

