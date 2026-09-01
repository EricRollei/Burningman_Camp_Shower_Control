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

// Standard I2C bus-clear for a slave left holding SDA low by a mid-transfer
// MCU reset: pulse SCL until the slave releases SDA, then issue a STOP. Safe
// on a healthy bus (SDA already high means no pulses). Ends and restarts the
// Wire driver itself; returns true when SDA reads high afterwards.
bool i2cBusRecover(TwoWire& wire, uint8_t sda, uint8_t scl, uint32_t frequency);
