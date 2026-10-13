#include "oled.h"

#include <Arduino.h>
#include <Wire.h>

#if defined(OLED_SH1106)
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R1, U8X8_PIN_NONE, kOledScl, kOledSda);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R1, U8X8_PIN_NONE, kOledScl, kOledSda);
#endif

static bool device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void oled_scan_i2c() {
  Serial.println("I2C scan");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
    }
  }
}

bool oled_begin() {
  uint8_t addr = 0;
  if (device_present(0x3C)) {
    addr = 0x3C;
  } else if (device_present(0x3D)) {
    addr = 0x3D;
  }
  if (addr == 0) {
    return false;
  }

  oled.setI2CAddress(static_cast<uint8_t>(addr << 1));
  oled.begin();
  Wire.setClock(400000);
  oled.setDrawColor(1);
  Serial.printf("OLED at 0x%02X, portrait %dx%d\n", addr, oled.getDisplayWidth(),
                oled.getDisplayHeight());
  return true;
}
