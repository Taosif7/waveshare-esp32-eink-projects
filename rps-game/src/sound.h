#pragma once

#include <stdint.h>

enum class Sfx : uint8_t {
  None = 0,
  Click,
  Confirm,
  Win,
  Lose,
  Draw,
  Title,
  Reset,
};

bool sound_init(void);
void sound_play(Sfx sfx);
/** Abort any in-progress clip (e.g. leaving the reveal screen). */
void sound_stop(void);
