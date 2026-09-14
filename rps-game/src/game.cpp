#include "game.h"

#include <Preferences.h>
#include <esp_random.h>

namespace {
Preferences prefs;
constexpr const char *kNs = "rps";
constexpr const char *kBestKey = "best";
}  // namespace

void game_init(GameState *state) {
  *state = GameState{};
  if (prefs.begin(kNs, false)) {
    state->best_streak = prefs.getUShort(kBestKey, 0);
    prefs.end();
  }
  state->screen = Screen::Title;
  state->dirty = true;
  state->full_refresh = true;
  state->sfx = Sfx::Title;
}

RoundResult game_judge(Move player, Move cpu) {
  if (player == cpu) {
    return RoundResult::Draw;
  }
  if ((player == Move::Rock && cpu == Move::Scissors) ||
      (player == Move::Paper && cpu == Move::Rock) ||
      (player == Move::Scissors && cpu == Move::Paper)) {
    return RoundResult::Win;
  }
  return RoundResult::Lose;
}

const char *move_name(Move move) {
  switch (move) {
    case Move::Rock:
      return "ROCK";
    case Move::Paper:
      return "PAPER";
    case Move::Scissors:
      return "SCISSORS";
  }
  return "?";
}

const char *result_name(RoundResult result) {
  switch (result) {
    case RoundResult::Win:
      return "YOU WIN";
    case RoundResult::Lose:
      return "YOU LOSE";
    case RoundResult::Draw:
      return "DRAW";
  }
  return "?";
}

static void persist_best(GameState *state) {
  if (state->streak > state->best_streak) {
    state->best_streak = state->streak;
    prefs.begin(kNs, false);
    prefs.putUShort(kBestKey, state->best_streak);
    prefs.end();
  }
}

static void play_round(GameState *state) {
  state->cpu = static_cast<Move>(esp_random() % 3);
  state->last_result = game_judge(state->player, state->cpu);
  switch (state->last_result) {
    case RoundResult::Win:
      state->wins++;
      state->streak++;
      persist_best(state);
      state->sfx = Sfx::Win;
      break;
    case RoundResult::Lose:
      state->losses++;
      state->streak = 0;
      state->sfx = Sfx::Lose;
      break;
    case RoundResult::Draw:
      state->draws++;
      state->sfx = Sfx::Draw;
      break;
  }
  state->screen = Screen::Reveal;
  state->dirty = true;
  state->full_refresh = true;
}

void game_on_boot_click(GameState *state) {
  switch (state->screen) {
    case Screen::Title:
      state->screen = Screen::Choose;
      state->dirty = true;
      state->full_refresh = true;
      state->sfx = Sfx::Confirm;
      break;
    case Screen::Choose:
      state->player = static_cast<Move>((static_cast<uint8_t>(state->player) + 1) % 3);
      state->dirty = true;
      state->full_refresh = false;
      state->sfx = Sfx::Click;
      break;
    case Screen::Reveal:
      sound_stop();
      state->screen = Screen::Score;
      state->dirty = true;
      state->full_refresh = true;
      state->sfx = Sfx::Click;
      break;
    case Screen::Score:
      state->screen = Screen::Choose;
      state->dirty = true;
      state->full_refresh = true;
      state->sfx = Sfx::Click;
      break;
  }
}

void game_on_pwr_click(GameState *state) {
  switch (state->screen) {
    case Screen::Title:
      state->screen = Screen::Choose;
      state->dirty = true;
      state->full_refresh = true;
      state->sfx = Sfx::Confirm;
      break;
    case Screen::Choose:
      play_round(state);
      break;
    case Screen::Reveal:
      sound_stop();
      state->screen = Screen::Score;
      state->dirty = true;
      state->full_refresh = true;
      state->sfx = Sfx::Click;
      break;
    case Screen::Score:
      state->screen = Screen::Choose;
      state->dirty = true;
      state->full_refresh = true;
      state->sfx = Sfx::Click;
      break;
  }
}

void game_on_pwr_long(GameState *state) {
  sound_stop();
  state->wins = 0;
  state->losses = 0;
  state->draws = 0;
  state->streak = 0;
  state->screen = Screen::Title;
  state->dirty = true;
  state->full_refresh = true;
  state->sfx = Sfx::Reset;
}

void game_on_move(GameState *state, Move move) {
  if (state->screen != Screen::Choose) {
    return;
  }
  if (state->player == move) {
    return;
  }
  state->player = move;
  state->dirty = true;
  state->full_refresh = false;
  state->sfx = Sfx::Click;
}

void game_on_select(GameState *state) {
  game_on_pwr_click(state);
}

void game_on_home(GameState *state) {
  if (state->screen == Screen::Title) {
    return;
  }
  sound_stop();
  state->screen = Screen::Title;
  state->dirty = true;
  state->full_refresh = true;
  state->sfx = Sfx::Click;
}

void game_tick(GameState *state) {
  (void)state;
}
