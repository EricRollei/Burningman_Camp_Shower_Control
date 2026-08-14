#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "I2cHub.h"

// Driver for the M5Stack 1.3-inch OLED Unit (SH1107, 64 x 128).
class StatusOled {
 public:
  bool begin(TwoWire& wire, I2cHub& hub, int8_t channel, uint8_t address);
  void showOpen(const char* message);
  void showInUse();
  void showUnavailable();
  bool healthy() const { return healthy_; }

 private:
  bool command(uint8_t value);
  bool flush();
  void clear(bool on = false);
  void pixel(int x, int y, bool on);
  void text(const char* value, int x, int y, uint8_t scale, bool on);

  TwoWire* wire_ = nullptr;
  I2cHub* hub_ = nullptr;
  int8_t channel_ = -1;
  uint8_t address_ = 0;
  bool healthy_ = false;
  uint8_t buffer_[1024] = {0};
};
