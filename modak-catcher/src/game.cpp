#include "game.h"

#include <Preferences.h>
#include <esp_random.h>

#include "epaper_config.h"
#include "sprites.h"

namespace {
Preferences prefs;
constexpr const char *kNs = "modak";
constexpr const char *kBestKey = "best";
constexpr uint8_t kMaxMisses = 3;
constexpr uint8_t kFullEvery = 8;
constexpr int32_t kQ = 256;
constexpr int kJoyCenter = 2048;
constexpr int kJoyDead = 600;
constexpr int kJoySpan = 1400;
constexpr int kCatcherSpeed = 220;  // px/s at full tilt
constexpr int kFallSpeed = 36;      // px/s, constant
constexpr int16_t kHudH = 22;
constexpr int16_t kBottomPad = 2;

int16_t catcher_y_px() {
  return static_cast<int16_t>(EPD_HEIGHT - sprites_ganesha_height() - kBottomPad);
}

int32_t px_to_q(int16_t px) {
  return static_cast<int32_t>(px) * kQ;
}

int16_t q_to_px(int32_t q) {
  return static_cast<int16_t>(q / kQ);
}

void persist_best(GameState *state) {
  if (state->score > state->best) {
    state->best = state->score;
    if (prefs.begin(kNs, false)) {
      prefs.putUShort(kBestKey, state->best);
      prefs.end();
    }
  }
}

void spawn_modak(GameState *state) {
  const int16_t max_x = static_cast<int16_t>(EPD_WIDTH - sprites_modak_width());
  const int16_t x = static_cast<int16_t>(esp_random() % (max_x + 1));
  state->modak_x_q = px_to_q(x);
  state->modak_y_q = px_to_q(kHudH);
}

int stick_velocity_px_s(int joy_x) {
  int dx = joy_x - kJoyCenter;
  const int ax = dx < 0 ? -dx : dx;
  if (ax <= kJoyDead) {
    return 0;
  }
  int mag = ax - kJoyDead;
  if (mag > kJoySpan) {
    mag = kJoySpan;
  }
  int vel = mag * kCatcherSpeed / kJoySpan;
  return dx < 0 ? -vel : vel;
}

bool aabb_overlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah, int16_t bx, int16_t by,
                  int16_t bw, int16_t bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void game_over(GameState *state) {
  persist_best(state);
  state->screen = Screen::GameOver;
  state->dirty = true;
  state->full_refresh = true;
  state->base_refresh = false;
}
}  // namespace

void game_init(GameState *state) {
  *state = GameState{};
  if (prefs.begin(kNs, true)) {
    state->best = prefs.getUShort(kBestKey, 0);
    prefs.end();
  }
  state->screen = Screen::Title;
  state->dirty = true;
  state->full_refresh = true;
}

void game_to_title(GameState *state) {
  persist_best(state);
  state->screen = Screen::Title;
  state->score = 0;
  state->misses = 0;
  state->dirty = true;
  state->full_refresh = true;
  state->base_refresh = false;
}

void game_start(GameState *state) {
  state->screen = Screen::Playing;
  state->score = 0;
  state->misses = 0;
  state->catches_since_full = 0;
  state->catcher_x_q = px_to_q(static_cast<int16_t>((EPD_WIDTH - sprites_ganesha_width()) / 2));
  spawn_modak(state);
  state->last_ms = 0;
  state->dirty = true;
  state->full_refresh = false;
  state->base_refresh = true;
  state->hud_dirty = true;
}

void game_tick(GameState *state, int joy_x, uint32_t now_ms) {
  if (state->last_ms == 0) {
    state->last_ms = now_ms;
    return;
  }
  uint32_t dt = now_ms - state->last_ms;
  state->last_ms = now_ms;
  if (dt == 0 || dt > 100) {
    dt = 16;
  }
  if (state->screen != Screen::Playing) {
    return;
  }

  const int vel = stick_velocity_px_s(joy_x);
  state->catcher_x_q += static_cast<int32_t>(vel) * static_cast<int32_t>(dt) * kQ / 1000;
  const int32_t max_x_q = px_to_q(static_cast<int16_t>(EPD_WIDTH - sprites_ganesha_width()));
  if (state->catcher_x_q < 0) {
    state->catcher_x_q = 0;
  } else if (state->catcher_x_q > max_x_q) {
    state->catcher_x_q = max_x_q;
  }

  const int fall = kFallSpeed;
  state->modak_y_q += static_cast<int32_t>(fall) * static_cast<int32_t>(dt) * kQ / 1000;

  const int16_t mx = game_modak_x(state);
  const int16_t my = game_modak_y(state);
  const int16_t mw = game_modak_w();
  const int16_t mh = game_modak_h();
  int16_t cx = game_catcher_x(state);
  int16_t cy = game_catcher_y();
  int16_t cw = game_catcher_w();
  int16_t ch = game_catcher_h();
  // Catch pad: torso / arms, a little generous
  cx = static_cast<int16_t>(cx + 4);
  cw = static_cast<int16_t>(cw - 8);
  cy = static_cast<int16_t>(cy + 4);
  ch = static_cast<int16_t>(ch - 6);

  if (aabb_overlap(mx, my, mw, mh, cx, cy, cw, ch)) {
    state->score++;
    state->catches_since_full++;
    state->hud_dirty = true;
    spawn_modak(state);
    if (state->catches_since_full >= kFullEvery) {
      state->catches_since_full = 0;
      state->base_refresh = true;
    }
    return;
  }

  if (my + mh >= EPD_HEIGHT - 1) {
    state->misses++;
    state->hud_dirty = true;
    if (state->misses >= kMaxMisses) {
      game_over(state);
      return;
    }
    spawn_modak(state);
    state->base_refresh = true;  // clear ghost trail after a miss
  }
}

int16_t game_catcher_x(const GameState *state) {
  return q_to_px(state->catcher_x_q);
}

int16_t game_catcher_y() {
  return catcher_y_px();
}

int16_t game_modak_x(const GameState *state) {
  return q_to_px(state->modak_x_q);
}

int16_t game_modak_y(const GameState *state) {
  return q_to_px(state->modak_y_q);
}

int16_t game_catcher_w() {
  return static_cast<int16_t>(sprites_ganesha_width());
}

int16_t game_catcher_h() {
  return static_cast<int16_t>(sprites_ganesha_height());
}

int16_t game_modak_w() {
  return static_cast<int16_t>(sprites_modak_width());
}

int16_t game_modak_h() {
  return static_cast<int16_t>(sprites_modak_height());
}

uint8_t game_lives_left(const GameState *state) {
  if (state->misses >= kMaxMisses) {
    return 0;
  }
  return static_cast<uint8_t>(kMaxMisses - state->misses);
}
