#pragma once

#include <stdint.h>

// One timed cue from data/lyrics.txt. frame is 1..16.
struct LyricLine {
  uint32_t time_ms;
  uint8_t frame;
  char text[72];
};

constexpr int kLyricCap = 48;

struct LyricScript {
  LyricLine lines[kLyricCap];
  int count;
};

// Mount LittleFS and read /lyrics.txt. Returns false when the file is missing
// or has no cues. Placeholder text lives in lyrics.example.txt, not here.
bool lyrics_load(LyricScript *script);
