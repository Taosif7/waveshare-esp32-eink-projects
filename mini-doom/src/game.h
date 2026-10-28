#pragma once

#include <stdint.h>

// Shaped stick axes are about -1400..1400. On this module low Y counts are
// forward, so a negative axis_y means walk forward. Touch (or the optional
// button) is fire. Stick click is use.

struct Input {
  int16_t axis_x;
  int16_t axis_y;
  bool fire_held;
  bool fire_edge;
  bool use_edge;
};

void game_setup();
void game_step(const Input& input);
void game_draw();
