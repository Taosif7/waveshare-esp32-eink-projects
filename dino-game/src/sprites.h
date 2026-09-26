#pragma once

#include <stdint.h>

// 1-bit sprites. Bit 1 = black ink, bit 0 = transparent.
// T-Rex, cacti, horizon, cloud, digits, and restart are the Chromium
// offline runner 1x assets (BSD-3-Clause), thresholded to the panel.
// Pterodactyl frames are drawn to the runner's 46x40 collision box.

void draw_trex(int16_t x, int16_t y, uint8_t pose);
void draw_cactus_small(int16_t x, int16_t y, uint8_t size);
void draw_cactus_large(int16_t x, int16_t y, uint8_t size);
void draw_bird(int16_t x, int16_t y, uint8_t frame);
void draw_cloud(int16_t x, int16_t y);
void draw_horizon(int16_t x, int16_t y, uint8_t variant);
void draw_glyph(int16_t x, int16_t y, uint8_t glyph);
void draw_game_over(int16_t x, int16_t y);
void draw_restart(int16_t x, int16_t y);
