#pragma once

#include <stdint.h>

// Debounce and the volume quantizer. No GPIO here, so the host test can run them.

constexpr int kPotSpan = 4096;
constexpr int kVolumeSteps = 16;
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kVolumeStepMs = 55;

struct PadDebounce {
  bool stable;
  bool lastRaw;
  uint32_t changeMs;
};

struct PadEdges {
  bool down;
  bool pressed;
  bool released;
};

struct PotFilter {
  int emaQ;
  int shown;
  int step;
  int queued;
  bool moved;
};

void pad_debounce_init(PadDebounce *pad);
void pad_debounce_update(PadDebounce *pad, bool raw, uint32_t nowMs, PadEdges *out);

void pot_filter_reset(PotFilter *pot, int sample);
void pot_filter_push(PotFilter *pot, int sample);
int pot_filter_take_step(PotFilter *pot);
