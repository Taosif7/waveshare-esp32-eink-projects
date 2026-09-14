#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "epaper_config.h"
#include "icons.h"
#include "port_display.h"
#include "result_images.h"

// Compact 5x7 ASCII font (subset A-Z 0-9 space and a few symbols)
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
  int16_t x = (EPD_WIDTH - text_width(text, scale)) / 2;
  if (x < 0) {
    x = 0;
  }
  draw_text(x, y, text, scale);
}

static void draw_frame() {
  EPD_DrawRect(0, 0, EPD_WIDTH, EPD_HEIGHT, DRIVER_COLOR_BLACK);
  EPD_DrawRect(2, 2, EPD_WIDTH - 4, EPD_HEIGHT - 4, DRIVER_COLOR_BLACK);
  EPD_DrawRect(4, 4, EPD_WIDTH - 8, EPD_HEIGHT - 8, DRIVER_COLOR_BLACK);
}

static void draw_title(const GameState *state) {
  (void)state;
  draw_frame();
  draw_text_centered(18, "RPS", 4);
  draw_text_centered(52, "INK DUEL", 2);

  icons_draw_move(Move::Rock, 40, 110, 8);
  icons_draw_move(Move::Paper, 100, 110, 8);
  icons_draw_move(Move::Scissors, 160, 110, 8);

  draw_text_centered(156, "DOWN TO BEGIN", 1);
  draw_text_centered(170, "CLICK=HOME", 1);
  draw_text_centered(184, "HOLD PWR:RESET", 1);
}

static void draw_choose(const GameState *state) {
  draw_frame();
  draw_text_centered(12, "YOUR MOVE", 2);
  EPD_DrawHLine(20, 32, EPD_WIDTH - 40, DRIVER_COLOR_BLACK);

  icons_draw_move(state->player, 100, 90, 12);
  draw_text_centered(140, move_name(state->player), 2);

  draw_text_centered(160, "L:ROCK  R:PAPER", 1);
  draw_text_centered(172, "U:SCIS  D:PLAY", 1);
  draw_text_centered(184, "CLICK=HOME", 1);
}

static void draw_reveal(const GameState *state) {
  const uint8_t *img = IMG_DRAW;
  switch (state->last_result) {
    case RoundResult::Win:
      img = IMG_WIN;
      break;
    case RoundResult::Lose:
      img = IMG_LOSE;
      break;
    case RoundResult::Draw:
      img = IMG_DRAW;
      break;
  }
  EPD_BlitFullscreen(img);

  // Readable control strip over the bottom of the image
  EPD_FillRect(0, 184, EPD_WIDTH, 16, DRIVER_COLOR_WHITE);
  EPD_DrawHLine(0, 184, EPD_WIDTH, DRIVER_COLOR_BLACK);
  draw_text_centered(188, "DOWN:SCORE  CLICK:HOME", 1);
}

static void draw_score(const GameState *state) {
  draw_frame();
  draw_text_centered(14, "SCOREBOARD", 2);
  EPD_DrawHLine(20, 34, EPD_WIDTH - 40, DRIVER_COLOR_BLACK);

  char line[32];
  snprintf(line, sizeof(line), "W %u", state->wins);
  draw_text_centered(48, line, 2);
  snprintf(line, sizeof(line), "L %u", state->losses);
  draw_text_centered(72, line, 2);
  snprintf(line, sizeof(line), "D %u", state->draws);
  draw_text_centered(96, line, 2);
  snprintf(line, sizeof(line), "STREAK %u", state->streak);
  draw_text_centered(122, line, 2);
  snprintf(line, sizeof(line), "BEST %u", state->best_streak);
  draw_text_centered(146, line, 2);

  draw_text_centered(168, "DOWN:NEXT", 1);
  draw_text_centered(182, "CLICK=HOME HOLD PWR:RST", 1);
}

void ui_render(const GameState *state) {
  EPD_Clear();
  switch (state->screen) {
    case Screen::Title:
      draw_title(state);
      break;
    case Screen::Choose:
      draw_choose(state);
      break;
    case Screen::Reveal:
      draw_reveal(state);
      break;
    case Screen::Score:
      draw_score(state);
      break;
  }

  if (state->full_refresh) {
    EPD_FlushFull();
  } else {
    EPD_FlushPartial();
  }
}
