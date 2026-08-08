#include "RfidReader.h"

namespace {
enum : uint8_t {
  CommandReg = 0x01,
  ComIrqReg = 0x04,
  DivIrqReg = 0x05,
  ErrorReg = 0x06,
  FIFODataReg = 0x09,
  FIFOLevelReg = 0x0A,
  ControlReg = 0x0C,
  BitFramingReg = 0x0D,
  CollReg = 0x0E,
  ModeReg = 0x11,
  TxModeReg = 0x12,
  RxModeReg = 0x13,
  TxControlReg = 0x14,
  TxASKReg = 0x15,
  CRCResultRegH = 0x21,
  CRCResultRegL = 0x22,
  ModWidthReg = 0x24,
  RFCfgReg = 0x26,
  TModeReg = 0x2A,
  TPrescalerReg = 0x2B,
  TReloadRegH = 0x2C,
  TReloadRegL = 0x2D,
  VersionReg = 0x37,
};
}  // namespace

bool RfidReader::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  initializeChip();
  return healthy();
}

int RfidReader::readUid(uint8_t* uid, int uidMax) {
  writeRegister(CollReg, readRegister(CollReg) & 0x7F);

  uint8_t atqa[2] = {0, 0};
  const uint8_t wupa = 0x52;
  const int rc = transceive(&wupa, 1, atqa, sizeof(atqa), 7);
  if (rc < 0) {
    lastError_ = rc;
    return 0;
  }

  const uint8_t cascade[3] = {0x93, 0x95, 0x97};
  int uidIndex = 0;
  for (int level = 0; level < 3; ++level) {
    uint8_t buffer[9];
    uint8_t response[5];

    buffer[0] = cascade[level];
    buffer[1] = 0x20;
    if (transceive(buffer, 2, response, sizeof(response), 0) != 5) return 0;
    if ((response[0] ^ response[1] ^ response[2] ^ response[3]) !=
        response[4]) {
      return 0;
    }

    buffer[0] = cascade[level];
    buffer[1] = 0x70;
    memcpy(buffer + 2, response, 5);
    if (!calculateCrc(buffer, 7, buffer + 7)) return 0;

    uint8_t sak[3];
    if (transceive(buffer, 9, sak, sizeof(sak), 0) < 1) return 0;

    const int first = response[0] == 0x88 ? 1 : 0;
    for (int i = first; i <= 3 && uidIndex < uidMax; ++i) {
      uid[uidIndex++] = response[i];
    }
    if (!(sak[0] & 0x04)) return uidIndex;
  }
  return 0;
}

void RfidReader::haltTag() {
  uint8_t buffer[4] = {0x50, 0x00, 0, 0};
  if (!calculateCrc(buffer, 2, buffer + 2)) return;
  transceive(buffer, 4, nullptr, 0, 0);
}

bool RfidReader::healthy() const {
  if (wire_ == nullptr) return false;
  RfidReader* self = const_cast<RfidReader*>(this);
  const uint8_t chipVersion = self->readRegister(VersionReg);
  return chipVersion != 0x00 && chipVersion != 0xFF;
}

uint8_t RfidReader::version() { return readRegister(VersionReg); }

uint8_t RfidReader::readRegister(uint8_t reg) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->endTransmission(false);
  wire_->requestFrom(address_, static_cast<uint8_t>(1));
  return wire_->available() ? wire_->read() : 0xFF;
}

void RfidReader::writeRegister(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  wire_->endTransmission();
}

void RfidReader::initializeChip() {
  writeRegister(CommandReg, 0x0F);
  delay(50);
  writeRegister(TModeReg, 0x80);
  writeRegister(TPrescalerReg, 0xA9);
  writeRegister(TReloadRegH, 0x03);
  writeRegister(TReloadRegL, 0xE8);
  writeRegister(TxASKReg, 0x40);
  writeRegister(ModeReg, 0x3D);
  writeRegister(TxModeReg, 0x00);
  writeRegister(RxModeReg, 0x00);
  writeRegister(ModWidthReg, 0x26);
  writeRegister(RFCfgReg, 0x70);
  writeRegister(TxControlReg, readRegister(TxControlReg) | 0x03);
}

bool RfidReader::calculateCrc(const uint8_t* data, int len, uint8_t* out) {
  writeRegister(CommandReg, 0x00);
  writeRegister(DivIrqReg, 0x04);
  writeRegister(FIFOLevelReg, 0x80);
  for (int i = 0; i < len; ++i) writeRegister(FIFODataReg, data[i]);
  writeRegister(CommandReg, 0x03);

  const uint32_t started = millis();
  while (!(readRegister(DivIrqReg) & 0x04)) {
    if (millis() - started > 50) return false;
  }
  writeRegister(CommandReg, 0x00);
  out[0] = readRegister(CRCResultRegL);
  out[1] = readRegister(CRCResultRegH);
  return true;
}

int RfidReader::transceive(const uint8_t* send, int sendLen, uint8_t* back,
                           int backMax, uint8_t txLastBits,
                           uint8_t* rxLastBits) {
  writeRegister(CommandReg, 0x00);
  writeRegister(ComIrqReg, 0x7F);
  writeRegister(FIFOLevelReg, 0x80);
  for (int i = 0; i < sendLen; ++i) writeRegister(FIFODataReg, send[i]);
  writeRegister(BitFramingReg, txLastBits & 0x07);
  writeRegister(CommandReg, 0x0C);
  writeRegister(BitFramingReg, (txLastBits & 0x07) | 0x80);

  const uint32_t started = millis();
  while (true) {
    const uint8_t irq = readRegister(ComIrqReg);
    if (irq & 0x30) break;
    if (irq & 0x01) return -1;
    if (millis() - started > 60) return -2;
  }
  writeRegister(BitFramingReg, 0x00);

  if (readRegister(ErrorReg) & 0x13) return -3;
  const int count = readRegister(FIFOLevelReg);
  if (count > backMax) return -4;
  for (int i = 0; i < count; ++i) back[i] = readRegister(FIFODataReg);
  if (rxLastBits != nullptr) *rxLastBits = readRegister(ControlReg) & 0x07;
  return count;
}

