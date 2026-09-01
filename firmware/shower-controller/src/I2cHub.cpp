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

bool i2cBusRecover(TwoWire& wire, uint8_t sda, uint8_t scl, uint32_t frequency) {
  wire.end();
  delay(2);
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  if (digitalRead(sda) == LOW) {
    for (uint8_t pulse = 0; pulse < 16 && digitalRead(sda) == LOW; ++pulse) {
      digitalWrite(scl, LOW);
      delayMicroseconds(5);
      digitalWrite(scl, HIGH);
      delayMicroseconds(5);
    }
    // STOP condition (SDA rising while SCL is high) releases every slave.
    pinMode(sda, OUTPUT_OPEN_DRAIN);
    digitalWrite(sda, LOW);
    delayMicroseconds(5);
    digitalWrite(sda, HIGH);
    delayMicroseconds(5);
  }
  const bool released = digitalRead(sda) == HIGH;
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  wire.begin(static_cast<int>(sda), static_cast<int>(scl), frequency);
  if (!released) Serial.println("[I2C] bus recovery failed; SDA still held low");
  return released;
}
