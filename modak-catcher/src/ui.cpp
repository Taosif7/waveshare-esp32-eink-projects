#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "epaper_config.h"
#include "port_display.h"
#include "sprites.h"

static uint8_t play_bg[EPD_WIDTH * EPD_HEIGHT / 8];
static bool play_bg_valid = false;

static const uint8_t FONT5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x14, 0x08, 0x3E, 0x08, 0x14},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x00, 0x41, 0x22, 0x14, 0x08},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
};

static int glyph_index(char c) {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  if (c < ' ' || c > 'Z') {
    return 0;
  }
  return c - ' ';
}

static void draw_char(int16_t x, int16_t y, char c, uint8_t scale) {
  const uint8_t *g = FONT5X7[glyph_index(c)];
  for (int col = 0; col < 5; col++) {
    uint8_t bits = g[col];
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) {
        EPD_FillRect(x + col * scale, y + row * scale, scale, scale, DRIVER_COLOR_BLACK);
      }
    }
  }
}

static void draw_text(int16_t x, int16_t y, const char *text, uint8_t scale) {
  while (*text) {
    draw_char(x, y, *text, scale);
    x += (5 + 1) * scale;
    text++;
  }
}

static int16_t text_width(const char *text, uint8_t scale) {
  return static_cast<int16_t>(strlen(text) * (5 + 1) * scale);
}

static void draw_text_centered(int16_t y, const char *text, uint8_t scale) {
  int16_t x = static_cast<int16_t>((EPD_WIDTH - text_width(text, scale)) / 2);
  if (x < 0) {
    x = 0;
  }
  draw_text(x, y, text, scale);
}

static void draw_frame() {
  EPD_DrawRect(0, 0, EPD_WIDTH, EPD_HEIGHT, DRIVER_COLOR_BLACK);
  EPD_DrawRect(2, 2, EPD_WIDTH - 4, EPD_HEIGHT - 4, DRIVER_COLOR_BLACK);
}

static void draw_lives(int16_t x, int16_t y, uint8_t lives) {
  for (uint8_t i = 0; i < 3; i++) {
    const int16_t cx = static_cast<int16_t>(x + i * 14);
    if (i < lives) {
      EPD_FillCircle(cx, y, 5, DRIVER_COLOR_BLACK);
    } else {
      EPD_DrawCircle(cx, y, 5, DRIVER_COLOR_BLACK);
    }
  }
}

static void draw_hud(const GameState *state) {
  char line[16];
  snprintf(line, sizeof(line), "%u", state->score);
  draw_text(6, 6, line, 2);
  draw_lives(158, 11, game_lives_left(state));
  EPD_DrawHLine(0, 21, EPD_WIDTH, DRIVER_COLOR_BLACK);
}

static void draw_title(const GameState *state) {
  (void)state;
  draw_frame();
  draw_text_centered(14, "MODAK", 3);
  draw_text_centered(40, "CATCHER", 2);
  const int16_t gx = static_cast<int16_t>((EPD_WIDTH - sprites_ganesha_width()) / 2);
  sprites_draw_ganesha(gx, 68);
  draw_text_centered(148, "TILT LEFT / RIGHT", 1);
  draw_text_centered(164, "CLICK OR BOOT", 1);
  draw_text_centered(178, "TO START", 1);
}

static void draw_game_over(const GameState *state) {
  draw_frame();
  draw_text_centered(24, "GAME OVER", 2);
  EPD_DrawHLine(24, 46, EPD_WIDTH - 48, DRIVER_COLOR_BLACK);

  char line[24];
  snprintf(line, sizeof(line), "SCORE %u", state->score);
  draw_text_centered(64, line, 2);
  snprintf(line, sizeof(line), "BEST %u", state->best);
  draw_text_centered(92, line, 2);
  draw_text_centered(124, "3 MISSES", 1);

  sprites_draw_modak(static_cast<int16_t>((EPD_WIDTH - sprites_modak_width()) / 2), 140);
  draw_text_centered(168, "CLICK TO RETRY", 1);
  draw_text_centered(182, "HOLD PWR: TITLE", 1);
}

static void draw_play(const GameState *state) {
  if (!play_bg_valid || state->hud_dirty || state->base_refresh || state->full_refresh) {
    EPD_Clear();
    draw_hud(state);
    EPD_CopyFramebuffer(play_bg);
    play_bg_valid = true;
  } else {
    EPD_LoadFramebuffer(play_bg);
  }
  sprites_draw_fall_streaks(game_modak_x(state), game_modak_y(state));
  sprites_draw_modak(game_modak_x(state), game_modak_y(state));
  sprites_draw_ganesha(game_catcher_x(state), game_catcher_y());
}

void ui_compose(const GameState *state) {
  switch (state->screen) {
    case Screen::Title:
      play_bg_valid = false;
      EPD_Clear();
      draw_title(state);
      break;
    case Screen::Playing:
      draw_play(state);
      break;
    case Screen::GameOver:
      play_bg_valid = false;
      EPD_Clear();
      draw_game_over(state);
      break;
  }
}

static void present_blocking(const GameState *state) {
  if (state->full_refresh) {
    EPD_FlushFull();
  } else if (state->base_refresh) {
    EPD_FlushBaseThenPartial();
  } else {
    EPD_FlushPartial();
  }
}

void ui_present(const GameState *state) {
  present_blocking(state);
}

void ui_present_begin(const GameState *state) {
  if (state->full_refresh || state->base_refresh) {
    present_blocking(state);
    return;
  }
  EPD_FlushPartialBegin();
}
