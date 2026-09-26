#pragma once

#include <stdint.h>

enum class Screen : uint8_t {
  Waiting = 0,
  Playing = 1,
  Paused = 2,
  GameOver = 3,
};

enum class ObstacleKind : uint8_t {
  Small = 0,
  Large = 1,
  Bird = 2,
};

constexpr uint8_t kMaxObstacles = 4;
constexpr uint8_t kMaxClouds = 2;
// Sim width that fills a 128px OLED at the draw scale in oled.h.
constexpr int16_t kPlayWidth = 356;

struct Obstacle {
  ObstacleKind kind = ObstacleKind::Small;
  uint8_t size = 1;
  uint8_t frame = 0;
  bool spawned_next = false;
  bool remove = false;
  float x = 0;
  float y = 0;
  float gap = 0;
  float frame_ms = 0;
};

struct Cloud {
  float x = 0;
  float y = 0;
  float gap = 0;
  bool active = false;
  bool followed = false;
};

struct GameState {
  Screen screen = Screen::Waiting;
  Obstacle obstacles[kMaxObstacles]{};
  uint8_t obstacle_count = 0;
  Cloud clouds[kMaxClouds]{};
  float horizon_x[2] = {0.f, 600.f};
  uint8_t horizon_src[2] = {0, 1};
  float trex_x = 18.f;
  float trex_y = 93.f;
  float jump_v = 0.f;
  float speed = 6.f;
  float distance = 0.f;
  float running_ms = 0.f;
  float leg_ms = 0.f;
  uint32_t score = 0;
  uint32_t best = 0;
  uint32_t last_ms = 0;
  uint32_t crash_ms = 0;
  uint32_t flash_hundred = 0;
  uint32_t night_phase = 0;
  uint32_t blink_closed_at = 0;
  uint32_t next_blink_at = 0;
  float flash_ms = 0.f;
  float last_purge_ms = 0.f;
  uint8_t trex_pose = 0;
  uint8_t run_frame = 0;
  uint8_t flash_iters = 0;
  bool jumping = false;
  bool reached_min = false;
  bool night = false;
  bool show_score = true;
  bool flashing = false;
  bool eyes_closed = false;
  bool ignore_release = false;
  bool dirty = true;
  bool full_refresh = true;
  bool base_refresh = false;
};

void game_init(GameState *state);
void game_press(GameState *state);
void game_release(GameState *state);
void game_pause(GameState *state);
void game_reset(GameState *state);
void game_tick(GameState *state, uint32_t now_ms);
void game_sync_clock(GameState *state, uint32_t now_ms);
