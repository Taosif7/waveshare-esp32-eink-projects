#include "tilt.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t kAdxlAddressLow = 0x53;
constexpr uint8_t kAdxlAddressHigh = 0x1D;
constexpr uint8_t kAdxlDeviceId = 0xE5;

constexpr uint8_t kRegDeviceId = 0x00;
constexpr uint8_t kRegBwRate = 0x2C;
constexpr uint8_t kRegPowerCtl = 0x2D;
constexpr uint8_t kRegDataFormat = 0x31;
constexpr uint8_t kRegDataX0 = 0x32;

constexpr uint8_t kDataFormat4gFullRes = 0x09;
constexpr uint8_t kBwRate100Hz = 0x0A;
constexpr uint8_t kPowerMeasure = 0x08;

uint8_t g_addr = 0;

bool device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool write_reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Full stop between the pointer and the read. Some modules never answer a
// repeated start. Bit 7 is the multi-byte flag; without it a 6-byte sample
// repeats the first byte and never finishes. The sample itself is at 100 kHz.
int read_regs(uint8_t reg, uint8_t *buf, size_t len) {
  if (len > 1) {
    reg = static_cast<uint8_t>(reg | 0x80);
  }
  Wire.setClock(100000);
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) {
    Wire.setClock(400000);
    return -1;
  }
  const uint8_t got = Wire.requestFrom(static_cast<uint8_t>(g_addr), static_cast<uint8_t>(len),
                                       static_cast<uint8_t>(true));
  size_t i = 0;
  while (i < got && i < len && Wire.available()) {
    buf[i++] = static_cast<uint8_t>(Wire.read());
  }
  Wire.setClock(400000);
  return static_cast<int>(i);
}

bool probe(uint8_t addr) {
  g_addr = addr;
  if (!device_present(addr)) {
    return false;
  }
  uint8_t id = 0;
  if (read_regs(kRegDeviceId, &id, 1) != 1 || id != kAdxlDeviceId) {
    Serial.printf("0x%02X is not an ADXL345\n", addr);
    return false;
  }
  return true;
}

}  // namespace

bool tilt_begin() {
  g_addr = 0;
  if (!probe(kAdxlAddressLow) && !probe(kAdxlAddressHigh)) {
    g_addr = 0;
    return false;
  }
  if (!write_reg(kRegPowerCtl, 0x00) || !write_reg(kRegDataFormat, kDataFormat4gFullRes) ||
      !write_reg(kRegBwRate, kBwRate100Hz) || !write_reg(kRegPowerCtl, kPowerMeasure)) {
    Serial.printf("ADXL345 at 0x%02X did not accept setup\n", g_addr);
    g_addr = 0;
    return false;
  }
  delay(2);
  return true;
}

bool tilt_present() {
  return g_addr != 0;
}

uint8_t tilt_address() {
  return g_addr;
}

bool tilt_sample(int16_t *roll, int16_t *pitch) {
  if (g_addr == 0) {
    return false;
  }
  uint8_t raw[6];
  if (read_regs(kRegDataX0, raw, sizeof(raw)) != static_cast<int>(sizeof(raw))) {
    return false;
  }
  *roll = static_cast<int16_t>(raw[0] | (raw[1] << 8));
  *pitch = static_cast<int16_t>(raw[2] | (raw[3] << 8));
  return true;
}
