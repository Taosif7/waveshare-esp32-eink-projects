#pragma once

#include <stdint.h>

uint16_t sprites_ganesha_width();
uint16_t sprites_ganesha_height();
uint16_t sprites_modak_width();
uint16_t sprites_modak_height();

void sprites_draw_ganesha(int16_t x, int16_t y);
void sprites_draw_modak(int16_t x, int16_t y);
void sprites_draw_fall_streaks(int16_t modak_x, int16_t modak_y);
