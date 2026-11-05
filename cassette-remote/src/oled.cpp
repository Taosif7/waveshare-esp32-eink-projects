#include "oled.h"

#include <Arduino.h>
#include <Wire.h>

#include <U8g2lib.h>

#include "pins.h"

namespace {

#ifndef OLED_I2C_HZ
#define OLED_I2C_HZ 800000
#endif

// Clock pin is passed before data. GPIO9 stays the I2C idle level (high)
// except during a transfer, which is also the level the boot strap wants.
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool oled_begin() {
  // Probe slowly. A miss at 800 kHz would look like a missing panel.
  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(100000);
  uint8_t addr = 0;
  for (int attempt = 0; attempt < 20 && addr == 0; ++attempt) {
    if (present(0x3C)) {
      addr = 0x3C;
    } else if (present(0x3D)) {
      addr = 0x3D;
    } else {
      Serial.println("No OLED at 0x3C/0x3D. VCC->3.3 GND->G SDA->8 SCL->9");
      delay(200);
    }
  }
  if (addr == 0) {
    return false;
  }
  display.setBusClock(OLED_I2C_HZ);
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  display.setBusClock(OLED_I2C_HZ);
  Serial.printf("OLED at 0x%02X, I2C %lu Hz\n", addr, static_cast<unsigned long>(OLED_I2C_HZ));
  return true;
}

void oled_draw(const UiView *view) {
  ui_render(display.getU8g2(), view);
  display.sendBuffer();
}
