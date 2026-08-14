#pragma once

#include <Arduino.h>
#include <Wire.h>

class I2cHub {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  bool select(int8_t channel);
  int8_t findDevice(uint8_t address);

  bool healthy() const { return healthy_; }
  int8_t selectedChannel() const { return selectedChannel_; }

 private:
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  int8_t selectedChannel_ = -1;
  bool healthy_ = false;
};
