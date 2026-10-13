#pragma once

#include <stdint.h>

// Short tones on the onboard ES8311 speaker. The game keeps running if the
// codec never ACKs.
enum class Sfx : uint8_t {
  None = 0,
  Click,
  Confirm,
  Shoot,
  Explode,
  Hurt,
  Power,
  Wave,
  Boss,
  Over,
};

bool sound_init();
void sound_set_enabled(bool enabled);
void sound_play(Sfx sfx);
