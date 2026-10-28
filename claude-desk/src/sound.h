#pragma once

#include <stdint.h>

enum class Tone : uint8_t {
  None = 0,
  Question,
  Error,
  Done,
};

bool sound_init(void);
void sound_play(Tone tone);
void sound_stop(void);
