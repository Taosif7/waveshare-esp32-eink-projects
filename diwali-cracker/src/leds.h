#pragma once

#include <stdint.h>

void leds_begin();
void leds_clear();
// Single 3-dot fuse tip; head_index is the brightest leading LED.
void leds_show_train(uint16_t head_index);
// Composite several independent fuse tips (parallel crackers).
void leds_show_trains(const uint16_t *heads, uint8_t count);
void leds_flash_finale();
