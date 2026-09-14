#pragma once

#include <stdint.h>

#include "sound.h"

enum class Move : uint8_t {
  Rock = 0,
  Paper = 1,
  Scissors = 2,
};

enum class RoundResult : uint8_t {
  Draw = 0,
  Win = 1,
  Lose = 2,
};

enum class Screen : uint8_t {
  Title = 0,
  Choose = 1,
  Reveal = 2,
  Score = 3,
};

struct GameState {
  Screen screen = Screen::Title;
  Move player = Move::Rock;
  Move cpu = Move::Rock;
  RoundResult last_result = RoundResult::Draw;
  uint16_t wins = 0;
  uint16_t losses = 0;
  uint16_t draws = 0;
  uint16_t best_streak = 0;
  uint16_t streak = 0;
  bool dirty = true;
  bool full_refresh = true;
  Sfx sfx = Sfx::None;
};

void game_init(GameState *state);
void game_on_boot_click(GameState *state);
void game_on_pwr_click(GameState *state);
void game_on_pwr_long(GameState *state);
void game_on_move(GameState *state, Move move);
void game_on_select(GameState *state);
void game_on_home(GameState *state);
void game_tick(GameState *state);
RoundResult game_judge(Move player, Move cpu);
const char *move_name(Move move);
const char *result_name(RoundResult result);
