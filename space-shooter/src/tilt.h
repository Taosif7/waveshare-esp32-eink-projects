#pragma once

#include <stdint.h>

// ADXL345 on the same I2C bus as the OLED. tilt_begin() is safe when the
// chip is missing: the game then skips the starfield warp.
bool tilt_begin();
bool tilt_present();
uint8_t tilt_address();

// Roll and pitch in raw counts, 256 per g. Right-wing-down is positive roll
// before the sign flip in main.cpp. False when the read does not complete.
bool tilt_sample(int16_t *roll, int16_t *pitch);
