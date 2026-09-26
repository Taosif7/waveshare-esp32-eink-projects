#pragma once

#include <U8g2lib.h>

constexpr int16_t kOledW = 128;
constexpr int16_t kOledH = 64;
constexpr int kOledSda = 18;
constexpr int kOledScl = 8;

// Nearest-neighbor scale from Chrome sim pixels onto the 128x64 panel.
constexpr float kDrawScale = 0.36f;
// Outline art (clouds, ground bumps) keeps every source pixel that lands in a
// destination pixel, and clouds are drawn larger so the puffs stay readable.
constexpr float kCloudScale = 0.70f;
constexpr float kHorizonScaleY = 0.58f;
constexpr float kGlyphScale = 0.60f;
constexpr float kGameOverScale = 0.66f;
constexpr float kRestartScale = 0.50f;
constexpr float kFeetSimY = 140.f;
constexpr int16_t kFeetScreenY = 63;

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C oled;

bool oled_begin();
void oled_set_inverted(bool inverted);
