#pragma once

#include <stdint.h>

struct PadInput {
  int32_t angle;
  int32_t power;
  int potRaw;
  bool drawing;
  bool launch;
  bool shake;
  bool padEdge;
};

void input_begin();
void input_poll(PadInput *out, bool aim, uint32_t nowMs);
