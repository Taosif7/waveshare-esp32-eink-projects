#pragma once

#include <stdint.h>

#include "fx.h"

constexpr int kMaxBodies = 16;

enum class Shape : uint8_t { Circle, Box };

enum class Mat : uint8_t { Wood, Stone, Glass, Pig, Bird };

enum class Look : uint8_t {
  Bird,
  Pig,
  WoodPlank,
  WoodCrate,
  WoodChip,
  Stone,
  StoneChip,
  Glass,
  GlassChip
};

struct Body {
  bool alive;
  bool asleep;
  bool supported;
  bool struck;
  Shape shape;
  Mat mat;
  Look look;
  int32_t x;
  int32_t y;
  int32_t vx;
  int32_t vy;
  int32_t ang;
  int32_t angVel;
  int16_t hw;
  int16_t hh;
  uint8_t hp;
  uint8_t quiet;
  int32_t invMass;
  int32_t invI;
  int impact;
};

struct World {
  Body bodies[kMaxBodies];
  int32_t ground;
};

struct Spec {
  Shape shape;
  Mat mat;
  Look look;
  int x;
  int y;
  int hw;
  int hh;
  int hp;
  int32_t ang;
  bool asleep;
};

void world_clear(World *world, int groundY);
int world_add(World *world, const Spec &spec);
void world_step(World *world);
bool world_asleep(const World *world);
void world_wake(World *world);
