#include "oled.h"

#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

static bool device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool oled_begin() {
  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(400000);

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
  oled.setFont(u8g2_font_5x7_tr);
  oled.setFontPosTop();
  oled.setDrawColor(1);
  return true;
}

void oled_set_inverted(bool inverted) {
  u8x8_t *bus = oled.getU8x8();
  u8x8_cad_StartTransfer(bus);
  u8x8_cad_SendCmd(bus, inverted ? 0xA7 : 0xA6);
  u8x8_cad_EndTransfer(bus);
}
