#pragma once

#include <stdint.h>

#include "lyrics.h"

void player_begin(const LyricScript *script);
void player_restart(uint32_t now_ms);
void player_render(uint32_t now_ms);
