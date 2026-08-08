#pragma once

#include <Arduino.h>

class FlowMeter {
 public:
  void begin(uint8_t pin);
  uint32_t totalPulses() const;

 private:
  static void IRAM_ATTR pulseISR();

  static FlowMeter* instance_;
  volatile uint32_t pulses_ = 0;
  uint8_t pin_ = 0;
};

