#pragma once

#include <Arduino.h>
#include <Wire.h>

class RelayController {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  bool set(uint8_t channel, bool on);
  bool toggle(uint8_t channel);
  bool allOff();

  bool isOn(uint8_t channel) const;
  bool healthy() const { return healthy_; }
  uint8_t state() const { return state_; }

 private:
  bool writeRegister(uint8_t reg, uint8_t value);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  uint8_t state_ = 0;
  bool healthy_ = false;
};

