#pragma once

#include <stdint.h>

// Logical glass after U8G2_R1: 64 wide, 128 tall. Layout numbers are shared
// with tools/render_previews.py, which parses these constexpr values.
constexpr int16_t kScreenW = 64;
constexpr int16_t kScreenH = 128;
constexpr int16_t kHudH = 10;
constexpr int kHudTextY = 2;
constexpr int kHudLineY = 9;
constexpr int kHeartX = 46;
constexpr int kLivesX = 54;
constexpr int kWaveX = 27;

constexpr int kCols = 4;
constexpr int kStrideX = 14;
constexpr int kStrideY = 12;

constexpr int kTitleY = 2;
constexpr int kTitle2Y = 14;
constexpr int kMenuSaucerY = 28;
constexpr int kItemStartY = 50;
constexpr int kItemDiffY = 60;
constexpr int kItemDiffValueY = 68;
constexpr int kItemScoresY = 78;
constexpr int kItemSoundY = 88;
constexpr int kMenuTextX = 6;

constexpr int kOverSaucerY = 14;
constexpr int kOverHiY = 40;
constexpr int kOverScoreY = 52;
constexpr int kOverChevronY = 74;
constexpr int kOverTitleY = 82;
constexpr int kOverChevron2Y = 90;

constexpr int kPauseBoxX = 4;
constexpr int kPauseBoxY = 42;
constexpr int kPauseBoxW = 56;
constexpr int kPauseBoxH = 50;
constexpr int kPauseTitleY = 46;
constexpr int kPauseItemY0 = 58;
constexpr int kPauseItemDy = 10;
constexpr int kPauseTextX = 14;

constexpr int kWaveWipe = 32;
constexpr int kWaveBannerY = 52;

constexpr int kMaxInvaders = 16;
constexpr int kMaxPlayerShots = 3;
constexpr int kMaxEnemyShots = 6;
constexpr int kMaxRocks = 3;
constexpr int kMaxBits = 24;
constexpr int kMaxStars = 16;
constexpr int kMaxGhosts = 144;

enum class Screen : uint8_t { Menu, Scores, Playing, Paused, GameOver };
enum class Difficulty : uint8_t { Easy, Normal, Hard };
enum class InvState : uint8_t { Empty, Formed, Diving, Returning, Fleeing };

struct Invader {
  InvState state;
  int16_t x;
  int16_t y;
  int16_t base_x;  // home x while diving; sideways speed while fleeing
  uint8_t phase;
  uint8_t burst;
  uint8_t col;
  uint8_t row;
};

struct Shot {
  bool alive;
  int16_t x;
  int16_t y;
  int16_t vx;
  int16_t vy;
};

struct Rock {
  bool alive;
  int16_t x;
  int16_t y;
  int16_t vx;
  int16_t vy;
};

struct Bit {
  bool alive;
  int16_t x;
  int16_t y;
  int16_t vx;
  int16_t vy;
  uint8_t life;
};

struct Star {
  uint8_t x;
  uint8_t y;
  uint8_t kind;
  uint8_t twinkle;
};

struct Ghost {
  uint8_t x;
  uint8_t y;
  uint8_t life;
};

struct Input {
  int16_t axis_x;
  int16_t axis_y;
  bool fire_held;
  bool fire_edge;
  bool click_edge;
  int16_t tilt_x;
  int16_t tilt_y;
  int16_t tilt_z;
  bool tilt_ok;
};

struct Game {
  Screen screen;
  Difficulty diff;
  bool sound_on;
  uint32_t score;
  uint32_t top[3];
  int8_t rank;
  bool score_saved;
  uint8_t lives;
  uint8_t wave;
  uint8_t menu_i;
  uint8_t pause_i;
  uint16_t frame;
  uint8_t wave_banner;
  uint8_t clear_wait;
  bool wave_awarded;
  uint8_t rows;
  int16_t form_x;
  int16_t form_y;
  int8_t form_dir;
  int8_t nav_dir;
  uint8_t nav_hold;
  uint8_t enemy_cd;
  uint8_t dive_cd;
  uint8_t rock_cd;
  int16_t px;
  int16_t py;
  uint8_t iframes;
  uint8_t fire_cd;
  uint8_t spread_t;
  bool shield;
  bool boss_on;
  bool boss_parked;
  int16_t boss_x;
  int16_t boss_y;
  int16_t boss_vx;
  uint8_t boss_hp;
  uint8_t boss_max;
  uint8_t boss_cd;
  int16_t lean_x;
  int16_t lean_y;
  int8_t bank;
  bool tilt_seen;
  uint8_t tilt_miss;
  uint8_t shake;
  int8_t shake_amp;
  int8_t shake_x;
  int8_t shake_y;
  int16_t grav_x;
  int16_t grav_y;
  int16_t grav_z;
  bool grav_set;
  bool shake_hot;
  uint8_t scatter_cd;
  uint8_t flash;
  uint8_t ghost_i;
  bool power_on;
  uint8_t power_kind;
  int16_t power_x;
  int16_t power_y;
  Invader inv[kMaxInvaders];
  Shot player_shots[kMaxPlayerShots];
  Shot enemy_shots[kMaxEnemyShots];
  Rock rocks[kMaxRocks];
  Bit bits[kMaxBits];
  Star stars[kMaxStars];
  Ghost ghosts[kMaxGhosts];
};

void game_init(Game *game);
void game_step(Game *game, const Input *input);
