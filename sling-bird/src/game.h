#pragma once

#include <stdint.h>

#include "physics.h"

constexpr int kWorldW = 220;
constexpr int kGroundY = 58;
constexpr int kForkX = 36;
constexpr int kForkY = 34;
constexpr int kScreenW = 128;
constexpr int kScreenH = 64;
constexpr int kMaxBits = 24;
constexpr int kMaxFlash = 3;

enum class Phase : uint8_t { Aim, Flying, Result };

enum class BitKind : uint8_t { Wood, Stone, Glass, Feather };

enum class FlashKind : uint8_t { Blast, Star };

struct Bit {
  bool alive;
  int32_t x;
  int32_t y;
  int32_t vx;
  int32_t vy;
  uint8_t life;
  BitKind kind;
};

struct Puff {
  bool alive;
  int32_t x;
  int32_t y;
  int32_t vx;
  int32_t vy;
  uint8_t frame;
  uint8_t tick;
};

struct Flash {
  bool alive;
  int16_t x;
  int16_t y;
  FlashKind kind;
  uint8_t frame;
  uint8_t tick;
};

struct Game {
  World world;
  Phase phase;
  int level;
  int birdsLeft;
  int score;
  int checkpoint;
  int best;
  bool won;
  bool cleared;
  int bird;
  int32_t angle;
  int32_t power;
  bool drawing;
  bool shake;
  bool pullPose;
  int32_t launchAng;
  uint8_t launchT;
  uint8_t hitT;
  int camX;
  int shakeT;
  int shakeMag;
  int settle;
  uint32_t shotMs;
  uint16_t frame;
  Bit bits[kMaxBits];
  Puff puff;
  Flash flashes[kMaxFlash];
};

struct Pose {
  int birdX;
  int birdY;
  int forkX;
  int forkY;
  int tipX[2];
  int tipY[2];
  int slingFrame;
  int pullFrame;
};

void game_init(Game *game);
void game_step(Game *game, uint32_t nowMs);
void game_pose(const Game *game, Pose *pose);