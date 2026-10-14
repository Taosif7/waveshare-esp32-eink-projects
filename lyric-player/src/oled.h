#pragma once

#include <U8g2lib.h>

#include "board_pins.h"

// Landscape 128x64. U8G2_R2 if the module is mounted the other way up.
#if defined(OLED_SH1106)
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C oled;
#else
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
#endif

bool oled_begin();
void oled_scan_i2c();
