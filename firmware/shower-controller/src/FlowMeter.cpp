#include "FlowMeter.h"

FlowMeter* FlowMeter::instance_ = nullptr;

void FlowMeter::begin(uint8_t pin) {
  pin_ = pin;
  instance_ = this;
  pinMode(pin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin_), pulseISR, FALLING);
}

uint32_t FlowMeter::totalPulses() const {
  noInterrupts();
  const uint32_t snapshot = pulses_;
  interrupts();
  return snapshot;
}

void IRAM_ATTR FlowMeter::pulseISR() {
  if (instance_ != nullptr) {
    ++instance_->pulses_;
  }
}
