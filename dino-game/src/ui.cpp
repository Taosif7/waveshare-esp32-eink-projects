#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "epaper_config.h"
#include "port_display.h"
#include "sprites.h"

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
    x = static_cast<int16_t>(x + (5 + 1) * scale);
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

static int16_t screen_y(float sim_y) {
  return static_cast<int16_t>(floorf(sim_y) + kWorldOffsetY);
}

static int16_t screen_x(float sim_x) {
  return static_cast<int16_t>(floorf(sim_x));
}

static void draw_digits(int16_t x, int16_t y, uint32_t value) {
  char buf[6];
  if (value > 99999u) {
    value = 99999u;
  }
  snprintf(buf, sizeof(buf), "%05u", value);
  for (int i = 0; i < 5; i++) {
    draw_glyph(static_cast<int16_t>(x + i * 11), y, static_cast<uint8_t>(buf[i] - '0'));
  }
}

static void draw_hud(const GameState *state) {
  if (state->best > 0) {
    draw_glyph(6, 6, 10);
    draw_glyph(17, 6, 11);
    draw_digits(34, 6, state->best);
  }
  if (state->show_score) {
    draw_digits(139, 6, state->score);
  }
}

static void draw_world(const GameState *state) {
  for (uint8_t i = 0; i < kMaxClouds; i++) {
    if (!state->clouds[i].active) {
      continue;
    }
    draw_cloud(screen_x(state->clouds[i].x), screen_y(state->clouds[i].y));
  }
  for (uint8_t i = 0; i < 2; i++) {
    draw_horizon(screen_x(state->horizon_x[i]), screen_y(127.f), state->horizon_src[i]);
  }
  for (uint8_t i = 0; i < state->obstacle_count; i++) {
    const Obstacle *obstacle = &state->obstacles[i];
    const int16_t x = screen_x(obstacle->x);
    const int16_t y = screen_y(obstacle->y);
    switch (obstacle->kind) {
      case ObstacleKind::Small:
        draw_cactus_small(x, y, obstacle->size);
        break;
      case ObstacleKind::Large:
        draw_cactus_large(x, y, obstacle->size);
        break;
      case ObstacleKind::Bird:
        draw_bird(x, y, obstacle->frame);
        break;
    }
  }
  draw_trex(screen_x(state->trex_x), screen_y(state->trex_y), state->trex_pose);
  draw_hud(state);
}

static void draw_night_sky() {
  EPD_FillCircle(158, 34, 8, DRIVER_COLOR_BLACK);
  EPD_FillCircle(154, 32, 7, DRIVER_COLOR_WHITE);
  static const int16_t kStars[][2] = {
      {18, 30}, {48, 26}, {78, 40}, {108, 28}, {28, 52}, {96, 48},
  };
  for (uint8_t i = 0; i < 6; i++) {
    EPD_FillRect(kStars[i][0], kStars[i][1], 2, 2, DRIVER_COLOR_BLACK);
  }
}

static void invert_frame() {
  static uint8_t scratch[EPD_WIDTH * EPD_HEIGHT / 8];
  EPD_CopyFramebuffer(scratch);
  for (size_t i = 0; i < sizeof(scratch); i++) {
    scratch[i] ^= 0xFF;
  }
  EPD_LoadFramebuffer(scratch);
}

static void draw_waiting(const GameState *state) {
  EPD_Clear();
  draw_horizon(0, screen_y(127.f), 0);
  draw_trex(screen_x(state->trex_x), screen_y(state->trex_y), state->trex_pose);
  draw_hud(state);
  draw_text_centered(96, "TAP JUMP", 1);
  draw_text_centered(108, "HOLD PAUSE", 1);
  draw_text_centered(120, "LONG RESET", 1);
}

static void draw_paused() {
  const char *label = "PAUSED";
  const int16_t width = text_width(label, 2);
  const int16_t x = static_cast<int16_t>((EPD_WIDTH - width) / 2);
  EPD_FillRect(static_cast<uint16_t>(x - 6), 72, static_cast<uint16_t>(width + 12), 22,
               DRIVER_COLOR_WHITE);
  draw_text(x, 76, label, 2);
}

static void draw_game_over_panel() {
  const int16_t text_y = static_cast<int16_t>(kWorldOffsetY + (150 - 25) / 3);
  const int16_t text_x = static_cast<int16_t>((EPD_WIDTH - 191) / 2);
  const int16_t icon_x = static_cast<int16_t>((EPD_WIDTH - 36) / 2);
  const int16_t icon_y = static_cast<int16_t>(kWorldOffsetY + 150 / 2);
  EPD_FillRect(0, static_cast<uint16_t>(text_y - 2), EPD_WIDTH, 15, DRIVER_COLOR_WHITE);
  draw_game_over(text_x, text_y);
  EPD_FillRect(static_cast<uint16_t>(icon_x - 2), static_cast<uint16_t>(icon_y - 2), 40, 36,
               DRIVER_COLOR_WHITE);
  draw_restart(icon_x, icon_y);
}

void ui_compose(const GameState *state) {
  if (state->screen == Screen::Waiting) {
    draw_waiting(state);
    return;
  }
  EPD_Clear();
  draw_world(state);
  if (state->screen == Screen::Paused) {
    draw_paused();
  } else if (state->screen == Screen::GameOver) {
    draw_game_over_panel();
  }
  if (state->night) {
    draw_night_sky();
    invert_frame();
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
