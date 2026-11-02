#pragma once

#include <stdint.h>

constexpr int kKnobMax = 4095;

struct InputFrame {
  bool tap[5];
  bool down[5];
  bool knobMoved;
  int knob;
  int volumeStep;
};

void input_begin();
void input_poll(InputFrame *frame, uint32_t nowMs);
