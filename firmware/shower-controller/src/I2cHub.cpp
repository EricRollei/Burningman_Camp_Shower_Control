#include "I2cHub.h"

bool I2cHub::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  selectedChannel_ = -1;
  wire_->beginTransmission(address_);
  healthy_ = wire_->endTransmission() == 0;
  if (healthy_) select(0);
  return healthy_;
}

bool I2cHub::select(int8_t channel) {
  if (wire_ == nullptr || channel < 0 || channel > 7) return false;
  if (selectedChannel_ == channel) return true;
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(1U << channel));
  healthy_ = wire_->endTransmission() == 0;
  if (healthy_) selectedChannel_ = channel;
  return healthy_;
}

int8_t I2cHub::findDevice(uint8_t address) {
  if (!healthy_) return -1;
  for (int8_t channel = 0; channel < 6; ++channel) {
    if (!select(channel)) continue;
    wire_->beginTransmission(address);
    if (wire_->endTransmission() == 0) return channel;
  }
  return -1;
}
