#include "transport.h"

namespace {

// Angle units per second. 65536 is one revolution.
constexpr int32_t kOmegaPlay = 28000;
constexpr int32_t kOmegaSeek = 190000;
constexpr int32_t kWhip = 640000;
constexpr int32_t kTauMs = 170;
constexpr int32_t kSeekRate = 8;

void approach(int32_t *value, int32_t target, uint32_t dtMs) {
  const int32_t diff = target - *value;
  if (diff == 0 || dtMs == 0) {
    return;
  }
  int32_t step = (int32_t)((int64_t)diff * (int32_t)dtMs / kTauMs);
  if (step == 0) {
    step = diff > 0 ? 1 : -1;
  }
  if ((diff > 0 && step > diff) || (diff < 0 && step < diff)) {
    step = diff;
  }
  *value += step;
}

int32_t tape_rate(const Transport *deck) {
  if (deck->seek > 0) {
    return kSeekRate;
  }
  if (deck->seek < 0) {
    return -kSeekRate;
  }
  if (deck->playing) {
    return 1;
  }
  return 0;
}

}  // namespace

void transport_init(Transport *deck) {
  deck->playing = false;
  deck->seek = 0;
  deck->omega = 0;
  deck->angle = 0;
  deck->tape = 0;
  deck->tapeAcc = 0;
  deck->volumeFrom = 0;
  deck->volumeUntil = 0;
}

void transport_toggle_play(Transport *deck) {
  deck->playing = !deck->playing;
}

void transport_seek(Transport *deck, int8_t direction) {
  if (direction > 0) {
    deck->seek = 1;
  } else if (direction < 0) {
    deck->seek = -1;
  } else {
    deck->seek = 0;
  }
}

void transport_track(Transport *deck, int direction) {
  deck->tape = 0;
  deck->tapeAcc = 0;
  // A whip is just extra speed. The ease in transport_tick pulls it back to
  // the play or stop target, which is the deceleration.
  deck->omega += direction < 0 ? -kWhip : kWhip;
}

void transport_note_knob(Transport *deck, uint32_t nowMs) {
  const bool expired = (int32_t)(deck->volumeUntil - nowMs) <= 0;
  if (expired) {
    deck->volumeFrom = nowMs;
  }
  deck->volumeUntil = nowMs + kVolumePopupMs;
}

void transport_tick(Transport *deck, uint32_t dtMs) {
  if (dtMs == 0) {
    return;
  }
  if (dtMs > 80) {
    dtMs = 80;
  }

  int32_t target = 0;
  if (deck->seek > 0) {
    target = kOmegaSeek;
  } else if (deck->seek < 0) {
    target = -kOmegaSeek;
  } else if (deck->playing) {
    target = kOmegaPlay;
  }
  approach(&deck->omega, target, dtMs);
  deck->angle += (uint32_t)((int64_t)deck->omega * (int32_t)dtMs / 1000);

  const int32_t rate = tape_rate(deck);
  if (rate == 0) {
    return;
  }
  const int64_t acc = (int64_t)deck->tapeAcc + (int64_t)kTapeMax * rate * (int32_t)dtMs;
  deck->tape += (int32_t)(acc / kTrackMs);
  deck->tapeAcc = (int32_t)(acc % kTrackMs);
  if (deck->tape < 0) {
    deck->tape = 0;
    deck->tapeAcc = 0;
  } else if (deck->tape > kTapeMax) {
    deck->tape = kTapeMax;
    deck->tapeAcc = 0;
  }
}

bool transport_volume_visible(const Transport *deck, uint32_t nowMs, int *pop) {
  const int32_t remain = (int32_t)(deck->volumeUntil - nowMs);
  if (remain <= 0) {
    *pop = 0;
    return false;
  }
  const int32_t age = (int32_t)(nowMs - deck->volumeFrom);
  int level = 255;
  if (age < 120) {
    level = (int)(age * 255 / 120);
  }
  if (remain < 140) {
    const int out = (int)(remain * 255 / 140);
    if (out < level) {
      level = out;
    }
  }
  if (level < 0) {
    level = 0;
  }
  *pop = level;
  return level > 0;
}
