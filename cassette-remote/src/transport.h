#pragma once

#include <stdint.h>

// Local deck model. The host's transport is unknown, so play, tape position,
// and reel speed all live here. Phase 2 would overwrite `playing` and `tape`
// from Apple Media Service instead of the pad toggle and this timer.

constexpr int32_t kTapeMax = 1000;
constexpr int32_t kTrackMs = 90000;
constexpr uint32_t kVolumePopupMs = 1500;

struct Transport {
  bool playing;
  int8_t seek;
  int32_t omega;
  uint32_t angle;
  int32_t tape;
  int32_t tapeAcc;
  uint32_t volumeFrom;
  uint32_t volumeUntil;
};

void transport_init(Transport *deck);
void transport_toggle_play(Transport *deck);
void transport_seek(Transport *deck, int8_t direction);
void transport_track(Transport *deck, int direction);
void transport_note_knob(Transport *deck, uint32_t nowMs);
void transport_tick(Transport *deck, uint32_t dtMs);

bool transport_volume_visible(const Transport *deck, uint32_t nowMs, int *pop);
