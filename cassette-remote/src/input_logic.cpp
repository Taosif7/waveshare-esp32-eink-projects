#include "input_logic.h"

namespace {

constexpr int kEmaShift = 3;
constexpr int kShowDeadband = 56;
constexpr int kStepHyst = 40;
constexpr int kEmaFrac = 8;

int clamp_sample(int sample) {
  if (sample < 0) {
    return 0;
  }
  if (sample > kPotSpan - 1) {
    return kPotSpan - 1;
  }
  return sample;
}

// Nearest of the 17 positions (0..16). Used once, at reset, so boot does not
// emit a burst of volume keys to "catch up" to wherever the knob was left.
int nearest_step(int shown) {
  shown = clamp_sample(shown);
  int step = (shown * kVolumeSteps + (kPotSpan - 1) / 2) / (kPotSpan - 1);
  if (step > kVolumeSteps) {
    step = kVolumeSteps;
  }
  return step;
}

// Schmitt thresholds on the committed sample. The last step up and the last
// step down drop the extra margin so the rails are actually reachable: an
// integer EMA can sit a few counts inside 0 or 4095, and hysteresis past the
// rail would stick the knob one key short of the end.
int track_step(int shown, int step) {
  shown = clamp_sample(shown);
  if (step < 0) {
    step = 0;
  }
  if (step > kVolumeSteps) {
    step = kVolumeSteps;
  }
  for (int guard = 0; guard < kVolumeSteps; ++guard) {
    if (step >= kVolumeSteps) {
      break;
    }
    int edge = (step + 1) * (kPotSpan - 1) / kVolumeSteps + kStepHyst;
    // The deadband can leave `shown` ~50 counts short of a pegged wiper, so
    // the last gate has to sit inside that gap or the top key never fires.
    if (step == kVolumeSteps - 1) {
      edge = (kPotSpan - 1) - (kShowDeadband - 1);
    }
    if (shown < edge) {
      break;
    }
    step += 1;
  }
  for (int guard = 0; guard < kVolumeSteps; ++guard) {
    if (step <= 0) {
      break;
    }
    const int hyst = (step == 1) ? 0 : kStepHyst;
    const int edge = step * (kPotSpan - 1) / kVolumeSteps - hyst;
    if (shown > edge) {
      break;
    }
    step -= 1;
  }
  return step;
}

}  // namespace

void pad_debounce_init(PadDebounce *pad) {
  pad->stable = false;
  pad->lastRaw = false;
  pad->changeMs = 0;
}

void pad_debounce_update(PadDebounce *pad, bool raw, uint32_t nowMs, PadEdges *out) {
  if (raw != pad->lastRaw) {
    pad->lastRaw = raw;
    pad->changeMs = nowMs;
  }
  out->pressed = false;
  out->released = false;
  if (raw != pad->stable && (uint32_t)(nowMs - pad->changeMs) >= kDebounceMs) {
    pad->stable = raw;
    out->pressed = raw;
    out->released = !raw;
  }
  out->down = pad->stable;
}

void pot_filter_reset(PotFilter *pot, int sample) {
  sample = clamp_sample(sample);
  pot->emaQ = sample << kEmaFrac;
  pot->shown = sample;
  pot->step = nearest_step(sample);
  pot->queued = 0;
  pot->moved = false;
}

void pot_filter_push(PotFilter *pot, int sample) {
  sample = clamp_sample(sample);
  pot->moved = false;
  const int target = sample << kEmaFrac;
  const int diff = target - pot->emaQ;
  int step = diff >> kEmaShift;
  // The shift form stalls a few counts short of the rail. Nudge by one so a
  // knob parked at either end still settles, without turning ±1 ADC noise
  // into a new shown sample (the deadband below does that job).
  if (step == 0 && diff != 0) {
    step = diff > 0 ? 1 : -1;
  }
  pot->emaQ += step;
  const int ema = pot->emaQ >> kEmaFrac;
  const int delta = ema - pot->shown;
  if (delta > -kShowDeadband && delta < kShowDeadband) {
    return;
  }
  pot->shown = ema;
  pot->moved = true;
  const int next = track_step(pot->shown, pot->step);
  pot->queued += next - pot->step;
  if (pot->queued > kVolumeSteps) {
    pot->queued = kVolumeSteps;
  } else if (pot->queued < -kVolumeSteps) {
    pot->queued = -kVolumeSteps;
  }
  pot->step = next;
}

int pot_filter_take_step(PotFilter *pot) {
  if (pot->queued > 0) {
    pot->queued -= 1;
    return 1;
  }
  if (pot->queued < 0) {
    pot->queued += 1;
    return -1;
  }
  return 0;
}
