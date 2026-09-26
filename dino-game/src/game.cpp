#include "game.h"

#include <math.h>

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>

namespace {

Preferences prefs;
constexpr const char *kNs = "dino";
constexpr const char *kHiKey = "hi";

// Chrome runner units are pixels per 60 fps frame. Partial refresh on this
// panel is about 0.3 s, so scroll and the jump arc are stretched by
// kSpeedScale. A full hop still covers the same ground, relative to a cactus,
// as it does in Chrome.
constexpr float kSpeedScale = 0.20f;
constexpr float kMsPerFrame = 1000.f / 60.f;
constexpr float kAccel = 0.001f;
constexpr float kSpeed0 = 6.f;
constexpr float kMaxSpeed = 12.f;
constexpr float kGravity = 0.6f;
constexpr float kJumpV = -10.f;
constexpr float kDropV = -5.f;
constexpr float kMaxJumpY = 30.f;
constexpr float kMinJump = 30.f;
constexpr float kGroundY = 93.f;
constexpr float kTrexX = 18.f;
constexpr float kTrexW = 44.f;
constexpr float kTrexH = 47.f;
constexpr float kGapCoeff = 0.6f;
constexpr float kMaxGapCoeff = 1.5f;
constexpr float kClearMs = 3000.f;
constexpr float kScoreCoeff = 0.025f;
constexpr float kHorizonW = 600.f;
constexpr float kBirdMinSpeed = 8.5f;
constexpr float kBirdOffset = 0.8f;
constexpr uint32_t kNightEvery = 700;
constexpr uint32_t kRestartLockMs = 750;
constexpr float kPurgeEveryMs = 40000.f;
constexpr float kLegMs = (1000.f / 12.f) / kSpeedScale;
constexpr float kWingMs = 420.f;

struct Col {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

constexpr Col kTrexCols[] = {
    {1, -1, 30, 26}, {32, 0, 8, 16}, {10, 35, 14, 8},
    {1, 24, 29, 5},  {5, 30, 21, 4}, {9, 34, 15, 4},
};

constexpr Col kSmallCols[] = {
    {0, 7, 5, 27},
    {4, 0, 6, 34},
    {10, 4, 7, 14},
};

constexpr Col kLargeCols[] = {
    {0, 12, 7, 38},
    {8, 0, 7, 49},
    {13, 10, 10, 38},
};

constexpr Col kBirdCols[] = {
    {15, 15, 16, 5}, {18, 21, 24, 6}, {2, 14, 4, 3},
    {6, 10, 4, 3},   {10, 8, 6, 6},
};

int rand_incl(int lo, int hi) {
  if (hi <= lo) {
    return lo;
  }
  return lo + static_cast<int>(esp_random() % static_cast<uint32_t>(hi - lo + 1));
}

float base_width(ObstacleKind kind) {
  switch (kind) {
    case ObstacleKind::Small:
      return 17.f;
    case ObstacleKind::Large:
      return 25.f;
    case ObstacleKind::Bird:
      return 46.f;
  }
  return 17.f;
}

float base_height(ObstacleKind kind) {
  switch (kind) {
    case ObstacleKind::Small:
      return 35.f;
    case ObstacleKind::Large:
      return 50.f;
    case ObstacleKind::Bird:
      return 40.f;
  }
  return 35.f;
}

float obstacle_width(const Obstacle *obstacle) {
  return base_width(obstacle->kind) * static_cast<float>(obstacle->size);
}

void persist_best(GameState *state) {
  if (state->score <= state->best) {
    return;
  }
  state->best = state->score;
  if (prefs.begin(kNs, false)) {
    prefs.putUInt(kHiKey, state->best);
    prefs.end();
  }
  Serial.printf("HI %u\n", state->best);
}

void reset_horizon(GameState *state) {
  state->horizon_x[0] = 0.f;
  state->horizon_x[1] = kHorizonW;
  state->horizon_src[0] = 0;
  state->horizon_src[1] = 1;
}

void clear_actors(GameState *state) {
  state->obstacle_count = 0;
  for (uint8_t i = 0; i < kMaxClouds; i++) {
    state->clouds[i].active = false;
  }
}

void place_trex_ground(GameState *state) {
  state->trex_x = kTrexX;
  state->trex_y = kGroundY;
  state->jump_v = 0.f;
  state->jumping = false;
  state->reached_min = false;
  state->trex_pose = 0;
  state->run_frame = 0;
  state->leg_ms = 0.f;
}

void go_waiting(GameState *state) {
  clear_actors(state);
  reset_horizon(state);
  place_trex_ground(state);
  state->screen = Screen::Waiting;
  state->speed = kSpeed0;
  state->distance = 0.f;
  state->running_ms = 0.f;
  state->score = 0;
  state->night = false;
  state->night_phase = 0;
  state->flashing = false;
  state->show_score = true;
  state->flash_hundred = 0;
  state->flash_ms = 0.f;
  state->flash_iters = 0;
  state->eyes_closed = false;
  state->next_blink_at = millis() + 1600;
  state->last_purge_ms = 0.f;
  state->dirty = true;
  state->full_refresh = true;
  state->base_refresh = false;
  state->last_ms = 0;
}

void add_cloud(GameState *state, float x) {
  for (uint8_t i = 0; i < kMaxClouds; i++) {
    if (state->clouds[i].active) {
      continue;
    }
    Cloud *cloud = &state->clouds[i];
    cloud->active = true;
    cloud->followed = false;
    cloud->x = x;
    cloud->y = static_cast<float>(rand_incl(30, 71));
    cloud->gap = static_cast<float>(rand_incl(70, 150));
    return;
  }
}

void start_jump(GameState *state) {
  if (state->jumping) {
    return;
  }
  state->jumping = true;
  state->jump_v = kJumpV;
  state->reached_min = false;
  state->trex_pose = 0;
}

void start_run(GameState *state, bool jump_now) {
  clear_actors(state);
  reset_horizon(state);
  place_trex_ground(state);
  state->screen = Screen::Playing;
  state->speed = kSpeed0;
  state->distance = 0.f;
  state->running_ms = 0.f;
  state->score = 0;
  state->night = false;
  state->night_phase = 0;
  state->flashing = false;
  state->show_score = true;
  state->flash_hundred = 0;
  state->flash_ms = 0.f;
  state->flash_iters = 0;
  state->last_purge_ms = 0.f;
  state->dirty = true;
  state->full_refresh = false;
  state->base_refresh = true;
  state->last_ms = 0;
  add_cloud(state, 110.f);
  if (jump_now) {
    start_jump(state);
  }
  Serial.println("run");
}

void end_jump(GameState *state) {
  if (state->reached_min && state->jump_v < kDropV) {
    state->jump_v = kDropV;
  }
}

bool boxes_hit(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

bool hits_obstacle(const GameState *state, const Obstacle *obstacle) {
  const float ow = obstacle_width(obstacle);
  const float oh = base_height(obstacle->kind);
  if (!boxes_hit(state->trex_x + 1.f, state->trex_y + 1.f, kTrexW - 2.f, kTrexH - 2.f,
                 obstacle->x + 1.f, obstacle->y + 1.f, ow - 2.f, oh - 2.f)) {
    return false;
  }

  Col cols[5];
  uint8_t n = 0;
  const Col *src = kSmallCols;
  if (obstacle->kind == ObstacleKind::Large) {
    src = kLargeCols;
    n = 3;
  } else if (obstacle->kind == ObstacleKind::Bird) {
    src = kBirdCols;
    n = 5;
  } else {
    n = 3;
  }
  for (uint8_t i = 0; i < n; i++) {
    cols[i] = src[i];
  }
  if (obstacle->size > 1 && n >= 3) {
    const int16_t total = static_cast<int16_t>(ow);
    cols[1].w = static_cast<int16_t>(total - cols[0].w - cols[2].w);
    cols[2].x = static_cast<int16_t>(total - cols[2].w);
  }

  for (uint8_t t = 0; t < 6; t++) {
    const float tx = state->trex_x + 1.f + static_cast<float>(kTrexCols[t].x);
    const float ty = state->trex_y + 1.f + static_cast<float>(kTrexCols[t].y);
    for (uint8_t i = 0; i < n; i++) {
      const float ox = obstacle->x + 1.f + static_cast<float>(cols[i].x);
      const float oy = obstacle->y + 1.f + static_cast<float>(cols[i].y);
      if (boxes_hit(tx, ty, kTrexCols[t].w, kTrexCols[t].h, ox, oy, cols[i].w, cols[i].h)) {
        return true;
      }
    }
  }
  return false;
}

void spawn_obstacle(GameState *state) {
  if (state->obstacle_count >= kMaxObstacles) {
    return;
  }
  Obstacle *obstacle = &state->obstacles[state->obstacle_count];
  *obstacle = Obstacle{};

  const int kinds = state->speed >= kBirdMinSpeed ? 3 : 2;
  const int pick = rand_incl(0, kinds - 1);
  obstacle->kind = static_cast<ObstacleKind>(pick);

  const float multiple = obstacle->kind == ObstacleKind::Small   ? 3.f
                         : obstacle->kind == ObstacleKind::Large ? 6.f
                                                                 : 999.f;
  obstacle->size = static_cast<uint8_t>(rand_incl(1, 3));
  if (obstacle->size > 1 && multiple > state->speed) {
    obstacle->size = 1;
  }
  if (obstacle->kind == ObstacleKind::Bird) {
    obstacle->size = 1;
    static const float kBirdY[] = {100.f, 75.f, 50.f};
    obstacle->y = kBirdY[rand_incl(0, 2)];
  } else if (obstacle->kind == ObstacleKind::Large) {
    obstacle->y = 90.f;
  } else {
    obstacle->y = 105.f;
  }

  const float width = obstacle_width(obstacle);
  const float min_gap = obstacle->kind == ObstacleKind::Bird ? 150.f : 120.f;
  const float gap_min = roundf(width * state->speed + min_gap * kGapCoeff);
  const float gap_max = roundf(gap_min * kMaxGapCoeff);
  obstacle->gap = static_cast<float>(rand_incl(static_cast<int>(gap_min), static_cast<int>(gap_max)));
  obstacle->x = static_cast<float>(kPlayWidth);
  state->obstacle_count++;
}

void update_jump(GameState *state, float frames) {
  state->trex_y += state->jump_v * frames;
  state->jump_v += kGravity * frames;
  if (state->trex_y < kGroundY - kMinJump) {
    state->reached_min = true;
  }
  if (state->trex_y < kMaxJumpY) {
    end_jump(state);
  }
  if (state->trex_y > kGroundY) {
    state->trex_y = kGroundY;
    state->jump_v = 0.f;
    state->jumping = false;
    state->reached_min = false;
  }
}

void update_horizon(GameState *state, float dx) {
  for (uint8_t i = 0; i < 2; i++) {
    state->horizon_x[i] -= dx;
    if (state->horizon_x[i] <= -kHorizonW) {
      state->horizon_x[i] += kHorizonW * 2.f;
      state->horizon_src[i] = static_cast<uint8_t>(rand_incl(0, 1));
    }
  }
}

void update_clouds(GameState *state, float dt_ms) {
  const float dx = (0.2f / 1000.f) * dt_ms * state->speed * 4.f;
  uint8_t alive = 0;
  for (uint8_t i = 0; i < kMaxClouds; i++) {
    Cloud *cloud = &state->clouds[i];
    if (!cloud->active) {
      continue;
    }
    cloud->x -= dx;
    if (cloud->x + 46.f < 0.f) {
      cloud->active = false;
      continue;
    }
    alive++;
    if (!cloud->followed && (static_cast<float>(kPlayWidth) - cloud->x) > cloud->gap && alive < kMaxClouds) {
      if ((esp_random() & 1u) != 0) {
        add_cloud(state, static_cast<float>(kPlayWidth));
        cloud->followed = true;
      } else {
        cloud->gap += 28.f;
      }
    }
  }
  if (alive == 0) {
    add_cloud(state, static_cast<float>(kPlayWidth));
  }
}

void compact_obstacles(GameState *state) {
  uint8_t write = 0;
  for (uint8_t i = 0; i < state->obstacle_count; i++) {
    if (!state->obstacles[i].remove) {
      if (write != i) {
        state->obstacles[write] = state->obstacles[i];
      }
      write++;
    }
  }
  state->obstacle_count = write;
}

void update_score(GameState *state, float dt_ms) {
  uint32_t shown = static_cast<uint32_t>(state->distance * kScoreCoeff + 0.5f);
  if (shown > 99999u) {
    shown = 99999u;
  }
  state->score = shown;

  const uint32_t hundred = shown / 100u;
  if (hundred > state->flash_hundred) {
    state->flash_hundred = hundred;
    state->flashing = true;
    state->flash_ms = 0.f;
    state->flash_iters = 0;
    state->show_score = false;
  }
  if (state->flashing) {
    state->flash_ms += dt_ms;
    constexpr float kFlash = 300.f;
    if (state->flash_iters < 3) {
      if (state->flash_ms < kFlash) {
        state->show_score = false;
      } else if (state->flash_ms < kFlash * 2.f) {
        state->show_score = true;
      } else {
        state->flash_ms = 0.f;
        state->flash_iters++;
        state->show_score = false;
      }
    } else {
      state->flashing = false;
      state->show_score = true;
    }
  }

  const uint32_t phase = shown / kNightEvery;
  if (shown > 0 && phase != state->night_phase) {
    state->night_phase = phase;
    state->night = (phase & 1u) != 0;
    state->base_refresh = true;
    state->flashing = false;
    state->show_score = true;
    Serial.printf("night %u\n", state->night ? 1u : 0u);
  }
}

void update_legs(GameState *state, float dt_ms) {
  if (state->jumping) {
    state->trex_pose = 0;
    return;
  }
  state->leg_ms += dt_ms;
  if (state->leg_ms >= kLegMs) {
    state->leg_ms = 0.f;
    state->run_frame ^= 1u;
  }
  state->trex_pose = state->run_frame ? 3 : 2;
}

bool obstacle_near(const GameState *state) {
  if (state->jumping) {
    return true;
  }
  for (uint8_t i = 0; i < state->obstacle_count; i++) {
    const Obstacle *obstacle = &state->obstacles[i];
    const float right = obstacle->x + obstacle_width(obstacle);
    if (obstacle->x < state->trex_x + 96.f && right > state->trex_x - 16.f) {
      return true;
    }
  }
  return false;
}

void game_over(GameState *state, uint32_t now_ms) {
  state->flashing = false;
  state->show_score = true;
  state->trex_pose = 4;
  state->jumping = false;
  persist_best(state);
  state->screen = Screen::GameOver;
  state->crash_ms = now_ms;
  state->dirty = true;
  state->full_refresh = true;
  state->base_refresh = false;
  Serial.printf("crash score %u best %u\n", state->score, state->best);
}

void update_blink(GameState *state, uint32_t now_ms) {
  if (state->eyes_closed) {
    if (now_ms - state->blink_closed_at > 420) {
      state->eyes_closed = false;
      state->trex_pose = 0;
      state->next_blink_at = now_ms + static_cast<uint32_t>(rand_incl(2200, 6200));
      state->dirty = true;
    }
    return;
  }
  if (now_ms >= state->next_blink_at) {
    state->eyes_closed = true;
    state->trex_pose = 1;
    state->blink_closed_at = now_ms;
    state->dirty = true;
  }
}

void update_playing(GameState *state, float dt_ms, uint32_t now_ms) {
  const float frames = dt_ms / kMsPerFrame;
  const float visual = frames * kSpeedScale;

  if (state->jumping) {
    update_jump(state, visual);
  }
  update_legs(state, dt_ms);

  if (state->speed < kMaxSpeed) {
    state->speed += kAccel * frames;
    if (state->speed > kMaxSpeed) {
      state->speed = kMaxSpeed;
    }
  }

  const float scroll = state->speed * visual;
  state->running_ms += dt_ms;
  state->distance += scroll;
  update_horizon(state, scroll);
  update_clouds(state, dt_ms);
  update_score(state, dt_ms);

  const bool spawn_ok = state->running_ms > kClearMs;
  for (uint8_t i = 0; i < state->obstacle_count; i++) {
    Obstacle *obstacle = &state->obstacles[i];
    float step = scroll;
    if (obstacle->kind == ObstacleKind::Bird) {
      step = (state->speed + kBirdOffset) * visual;
      obstacle->frame_ms += dt_ms;
      if (obstacle->frame_ms >= kWingMs) {
        obstacle->frame_ms = 0.f;
        obstacle->frame ^= 1u;
      }
    }
    obstacle->x -= step;
    if (obstacle->x + obstacle_width(obstacle) < 0.f) {
      obstacle->remove = true;
    }
  }
  compact_obstacles(state);

  if (spawn_ok) {
    bool spawn = state->obstacle_count == 0;
    if (!spawn && state->obstacle_count < kMaxObstacles) {
      Obstacle *last = &state->obstacles[state->obstacle_count - 1];
      if (!last->spawned_next &&
          last->x + obstacle_width(last) + last->gap < static_cast<float>(kPlayWidth)) {
        last->spawned_next = true;
        spawn = true;
      }
    }
    if (spawn) {
      spawn_obstacle(state);
    }
  }

  if (spawn_ok) {
    for (uint8_t i = 0; i < state->obstacle_count; i++) {
      if (hits_obstacle(state, &state->obstacles[i])) {
        game_over(state, now_ms);
        return;
      }
    }
  }

  if (!obstacle_near(state) && state->running_ms - state->last_purge_ms > kPurgeEveryMs) {
    state->last_purge_ms = state->running_ms;
    state->base_refresh = true;
  }
}

}  // namespace

void game_init(GameState *state) {
  *state = GameState{};
  if (prefs.begin(kNs, true)) {
    state->best = prefs.getUInt(kHiKey, 0);
    prefs.end();
  }
  go_waiting(state);
  Serial.printf("HI loaded %u\n", state->best);
}

void game_press(GameState *state) {
  if (state->screen == Screen::Paused) {
    state->screen = Screen::Playing;
    state->dirty = true;
    state->base_refresh = true;
    state->last_ms = 0;
    Serial.println("resume");
    return;
  }
  if (state->screen == Screen::GameOver) {
    return;
  }
  if (state->screen == Screen::Waiting) {
    start_run(state, true);
    return;
  }
  if (state->screen == Screen::Playing) {
    start_jump(state);
  }
}

void game_release(GameState *state) {
  if (state->ignore_release) {
    state->ignore_release = false;
    return;
  }
  if (state->screen == Screen::GameOver && millis() - state->crash_ms >= kRestartLockMs) {
    start_run(state, false);
  }
}

void game_pause(GameState *state) {
  if (state->screen != Screen::Playing) {
    return;
  }
  state->screen = Screen::Paused;
  state->ignore_release = true;
  state->dirty = true;
  state->full_refresh = true;
  state->base_refresh = false;
  Serial.println("pause");
}

void game_reset(GameState *state) {
  state->ignore_release = true;
  go_waiting(state);
  Serial.println("reset");
}

void game_tick(GameState *state, uint32_t now_ms) {
  if (state->last_ms == 0) {
    state->last_ms = now_ms;
    if (state->screen == Screen::Waiting && state->next_blink_at == 0) {
      state->next_blink_at = now_ms + 1600;
    }
    return;
  }
  uint32_t dt = now_ms - state->last_ms;
  state->last_ms = now_ms;
  if (dt > 40u) {
    dt = 40u;
  }
  if (dt == 0) {
    return;
  }
  if (state->screen == Screen::Waiting) {
    update_blink(state, now_ms);
    return;
  }
  if (state->screen != Screen::Playing) {
    return;
  }
  update_playing(state, static_cast<float>(dt), now_ms);
}

void game_sync_clock(GameState *state, uint32_t now_ms) {
  state->last_ms = now_ms;
}
