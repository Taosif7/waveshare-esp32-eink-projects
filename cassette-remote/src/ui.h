#pragma once

#include <stdint.h>

struct u8g2_struct;
typedef struct u8g2_struct u8g2_t;

// Snapshot the OLED draws. `tape` and `volumePos` are 0..1000.
// `seek` is -1, 0, or +1. `angle` is the low 16 bits of a turn (modulus 65536).
struct UiView {
  bool connected;
  bool playing;
  int8_t seek;
  uint16_t angle;
  int tape;
  bool volume;
  int volumePos;
  int volumePop;
  uint32_t nowMs;
};

void ui_render(u8g2_t *u8g2, const UiView *view);

// The parts that never move: outlines, rail, nodes, tape head, segment frames.
void ui_draw_static(u8g2_t *u8g2);
