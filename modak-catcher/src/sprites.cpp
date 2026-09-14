#include "sprites.h"

#include "epaper_config.h"
#include "port_display.h"
#include "sprites_data.h"

uint16_t sprites_ganesha_width() {
  return SPRITE_GANESHA_W;
}

uint16_t sprites_ganesha_height() {
  return SPRITE_GANESHA_H;
}

uint16_t sprites_modak_width() {
  return SPRITE_MODAK_W;
}

uint16_t sprites_modak_height() {
  return SPRITE_MODAK_H;
}

void sprites_draw_ganesha(int16_t x, int16_t y) {
  EPD_BlitSprite(x, y, SPRITE_GANESHA_W, SPRITE_GANESHA_H, SPRITE_GANESHA_BITS);
}

void sprites_draw_modak(int16_t x, int16_t y) {
  EPD_BlitSprite(x, y, SPRITE_MODAK_W, SPRITE_MODAK_H, SPRITE_MODAK_BITS);
}

static void vline_clipped(int16_t x, int16_t y, int16_t h) {
  constexpr int16_t kClipTop = 22;  // keep streaks out of the HUD
  if (x < 0 || x >= EPD_WIDTH || h <= 0) {
    return;
  }
  if (y < kClipTop) {
    h = static_cast<int16_t>(h - (kClipTop - y));
    y = kClipTop;
  }
  if (h <= 0 || y >= EPD_HEIGHT) {
    return;
  }
  if (y + h > EPD_HEIGHT) {
    h = static_cast<int16_t>(EPD_HEIGHT - y);
  }
  EPD_DrawVLine(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(h),
                DRIVER_COLOR_BLACK);
}

void sprites_draw_fall_streaks(int16_t modak_x, int16_t modak_y) {
  const int16_t cx = static_cast<int16_t>(modak_x + SPRITE_MODAK_W / 2);
  // Staggered vertical dashes above the dumpling.
  vline_clipped(cx - 6, modak_y - 16, 8);
  vline_clipped(cx - 5, modak_y - 16, 8);
  vline_clipped(cx, modak_y - 12, 6);
  vline_clipped(cx + 1, modak_y - 12, 6);
  vline_clipped(cx + 6, modak_y - 18, 10);
  vline_clipped(cx - 2, modak_y - 8, 4);
}
