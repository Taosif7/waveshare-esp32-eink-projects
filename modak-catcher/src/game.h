#pragma once

#include <stdint.h>

enum class Screen : uint8_t {
  Title = 0,
  Playing = 1,
  GameOver = 2,
};

struct GameState {
  Screen screen = Screen::Title;
  int32_t catcher_x_q = 0;  // Q8 pixels, sprite top-left
  int32_t modak_x_q = 0;
  int32_t modak_y_q = 0;
  uint16_t score = 0;
  uint16_t best = 0;
  uint8_t misses = 0;
  uint32_t last_ms = 0;
  uint8_t catches_since_full = 0;
  bool dirty = true;
  bool full_refresh = true;
  bool base_refresh = false;
  bool hud_dirty = true;
};

void game_init(GameState *state);
void game_start(GameState *state);
void game_to_title(GameState *state);
void game_tick(GameState *state, int joy_x, uint32_t now_ms);

int16_t game_catcher_x(const GameState *state);
int16_t game_catcher_y();
int16_t game_modak_x(const GameState *state);
int16_t game_modak_y(const GameState *state);
int16_t game_catcher_w();
int16_t game_catcher_h();
int16_t game_modak_w();
int16_t game_modak_h();
uint8_t game_lives_left(const GameState *state);
