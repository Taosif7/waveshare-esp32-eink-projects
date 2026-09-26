#include "ui.h"

#include <math.h>
#include <stdio.h>

#include "oled.h"
#include "sprites.h"

static int16_t world_x(float sim_x) {
  return static_cast<int16_t>(floorf(sim_x * kDrawScale));
}

static int16_t world_y(float sim_y) {
  return static_cast<int16_t>(floorf((sim_y - kFeetSimY) * kDrawScale + kFeetScreenY));
}

static constexpr int16_t kGlyphPitch = static_cast<int16_t>(10 * kGlyphScale + 1.5f);

static void draw_digits(int16_t x, int16_t y, uint32_t value) {
  char buf[6];
  if (value > 99999u) {
    value = 99999u;
  }
  snprintf(buf, sizeof(buf), "%05u", value);
  for (int i = 0; i < 5; i++) {
    draw_glyph(static_cast<int16_t>(x + i * kGlyphPitch), y, static_cast<uint8_t>(buf[i] - '0'));
  }
}

static void draw_hud(const GameState *state) {
  if (state->best > 0) {
    draw_glyph(0, 0, 10);
    draw_glyph(kGlyphPitch, 0, 11);
    draw_digits(static_cast<int16_t>(kGlyphPitch * 2 + 2), 0, state->best);
  }
  if (state->show_score) {
    draw_digits(static_cast<int16_t>(kOledW - kGlyphPitch * 5), 0, state->score);
  }
}

static int16_t horizon_top() {
  const int16_t top = world_y(127.f);
  const int16_t old_h = static_cast<int16_t>(12 * kDrawScale + 0.5f);
  const int16_t new_h = static_cast<int16_t>(12 * kHorizonScaleY + 0.5f);
  return static_cast<int16_t>(top + old_h - new_h);
}

static int16_t cloud_top(float sim_y) {
  const int16_t top = world_y(sim_y);
  const int16_t old_h = static_cast<int16_t>(14 * kDrawScale + 0.5f);
  const int16_t new_h = static_cast<int16_t>(14 * kCloudScale + 0.5f);
  int16_t y = static_cast<int16_t>(top - (new_h - old_h));
  const int16_t hud = static_cast<int16_t>(13 * kGlyphScale + 0.5f);
  if (y < hud) {
    y = hud;
  }
  return y;
}

static void draw_label(int16_t y, const char *text) {
  oled.setDrawColor(1);
  const int16_t width = static_cast<int16_t>(oled.getStrWidth(text));
  int16_t x = static_cast<int16_t>((kOledW - width) / 2);
  if (x < 0) {
    x = 0;
  }
  oled.drawStr(x, y, text);
}

static void draw_world(const GameState *state) {
  for (uint8_t i = 0; i < kMaxClouds; i++) {
    if (!state->clouds[i].active) {
      continue;
    }
    draw_cloud(world_x(state->clouds[i].x), cloud_top(state->clouds[i].y));
  }
  for (uint8_t i = 0; i < 2; i++) {
    draw_horizon(world_x(state->horizon_x[i]), horizon_top(), state->horizon_src[i]);
  }
  for (uint8_t i = 0; i < state->obstacle_count; i++) {
    const Obstacle *obstacle = &state->obstacles[i];
    const int16_t x = world_x(obstacle->x);
    const int16_t y = world_y(obstacle->y);
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
  draw_trex(world_x(state->trex_x), world_y(state->trex_y), state->trex_pose);
  draw_hud(state);
}

static void draw_night_sky() {
  oled.setDrawColor(1);
  oled.drawDisc(108, 22, 5);
  oled.setDrawColor(0);
  oled.drawDisc(105, 20, 4);
  oled.setDrawColor(1);
  static const int16_t kStars[][2] = {{6, 20}, {36, 18}, {62, 24}, {20, 30}};
  for (uint8_t i = 0; i < 4; i++) {
    oled.drawPixel(kStars[i][0], kStars[i][1]);
    oled.drawPixel(static_cast<u8g2_uint_t>(kStars[i][0] + 1), kStars[i][1]);
  }
}

static void draw_waiting(const GameState *state) {
  draw_horizon(0, horizon_top(), 0);
  draw_trex(world_x(state->trex_x), world_y(state->trex_y), state->trex_pose);
  draw_hud(state);
  draw_label(15, "TAP JUMP");
  draw_label(24, "HOLD PAUSE");
  draw_label(33, "LONG RESET");
}

static void draw_paused() {
  oled.setDrawColor(0);
  oled.drawBox(38, 16, 52, 12);
  draw_label(18, "PAUSED");
}

static void draw_game_over_panel() {
  const int16_t text_w = static_cast<int16_t>(191 * kGameOverScale + 0.5f);
  const int16_t icon_w = static_cast<int16_t>(36 * kRestartScale + 0.5f);
  draw_game_over(static_cast<int16_t>((kOledW - text_w) / 2), 15);
  draw_restart(static_cast<int16_t>((kOledW - icon_w) / 2), 26);
}

void ui_draw(const GameState *state) {
  oled.clearBuffer();
  oled.setDrawColor(1);
  if (state->screen == Screen::Waiting) {
    draw_waiting(state);
  } else {
    draw_world(state);
    if (state->screen == Screen::Paused) {
      draw_paused();
    } else if (state->screen == Screen::GameOver) {
      draw_game_over_panel();
    }
    if (state->night) {
      draw_night_sky();
    }
  }
  // Drawn lit-on-dark. Invert makes day look like the browser: black on white.
  oled_set_inverted(!state->night);
  oled.sendBuffer();
}
