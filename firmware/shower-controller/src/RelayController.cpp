#include "RelayController.h"

namespace {
constexpr uint8_t MODE_REGISTER = 0x10;
constexpr uint8_t CONTROL_REGISTER = 0x11;
constexpr uint8_t AUTO_LED_MODE = 0x01;

uint8_t channelMask(uint8_t channel) {
  return channel >= 1 && channel <= 4 ? (1U << (4U - channel)) : 0;
}
}  // namespace

bool RelayController::begin(TwoWire& wire, uint8_t address, I2cHub* hub, int8_t channel) {
  wire_ = &wire;
  address_ = address;
  hub_ = hub;
  channel_ = channel;
  state_ = 0;

  if (hub_ != nullptr && !hub_->select(channel_)) return false;
  wire_->beginTransmission(address_);
  healthy_ = wire_->endTransmission() == 0;
  if (!healthy_) return false;

  // Capture what the module was still driving before the forced all-off; a
  // controller crash leaves the last commanded state latched here.
  preClearKnown_ = readRegister(CONTROL_REGISTER, preClearState_);

  // Auto mode makes each indicator LED follow its relay.
  healthy_ = writeRegister(MODE_REGISTER, AUTO_LED_MODE) && allOff();
  return healthy_;
}

bool RelayController::set(uint8_t channel, bool on) {
  const uint8_t mask = channelMask(channel);
  if (mask == 0 || wire_ == nullptr) return false;

  const uint8_t next = on ? (state_ | mask) : (state_ & ~mask);
  if (!writeRegister(CONTROL_REGISTER, next)) {
    healthy_ = false;
    return false;
  }
  state_ = next;
  healthy_ = true;
  return true;
}

bool RelayController::toggle(uint8_t channel) {
  return set(channel, !isOn(channel));
}

bool RelayController::allOff() {
  if (wire_ == nullptr) return false;
  if (!writeRegister(CONTROL_REGISTER, 0)) {
    healthy_ = false;
    return false;
  }
  state_ = 0;
  return true;
}

bool RelayController::isOn(uint8_t channel) const {
  return (state_ & channelMask(channel)) != 0;
}

bool RelayController::writeRegister(uint8_t reg, uint8_t value) {
  if (hub_ != nullptr && !hub_->select(channel_)) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool RelayController::readRegister(uint8_t reg, uint8_t& value) {
  if (hub_ != nullptr && !hub_->select(channel_)) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, static_cast<uint8_t>(1)) != 1) return false;
  const int data = wire_->read();
  if (data < 0) return false;
  value = static_cast<uint8_t>(data);
  return true;
}
