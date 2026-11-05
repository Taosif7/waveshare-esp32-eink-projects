#include "input.h"

#include <Arduino.h>

#include "input_logic.h"
#include "pins.h"

namespace {

#if !defined(TOUCH_ACTIVE_HIGH)
#define TOUCH_ACTIVE_HIGH 1
#endif

PadDebounce pads[kPadCount];
PotFilter pot;
uint32_t nextVolumeMs = 0;
bool potReady = false;

bool pad_down(int pin) {
  const int level = digitalRead(pin);
#if TOUCH_ACTIVE_HIGH
  return level == HIGH;
#else
  return level == LOW;
#endif
}

int read_knob() {
  // GPIO1 and GPIO4 are also ADC1 channels. Read the wiper before any
  // digitalRead on those pads, and discard the first conversion: the SAR
  // mux otherwise holds the previous channel and the knob looks stuck.
  analogRead(kPotPin);
  int raw = analogRead(kPotPin);
  if (raw < 0) {
    raw = 0;
  }
  if (raw > kKnobMax) {
    raw = kKnobMax;
  }
  // This module's wiper falls as the knob turns toward "more".
  return kKnobMax - raw;
}

}  // namespace

void input_begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(kPotPin, ADC_11db);
  pinMode(kPotPin, INPUT);
  for (int i = 0; i < kPadCount; ++i) {
#if TOUCH_ACTIVE_HIGH
    pinMode(kPadPins[i], INPUT_PULLDOWN);
#else
    pinMode(kPadPins[i], INPUT_PULLUP);
#endif
    pad_debounce_init(&pads[i]);
  }
  const int first = read_knob();
  pot_filter_reset(&pot, first);
  potReady = true;
  nextVolumeMs = 0;
}

void input_poll(InputFrame *frame, uint32_t nowMs) {
  const int sample = read_knob();
  for (int i = 0; i < kPadCount; ++i) {
    PadEdges edges;
    pad_debounce_update(&pads[i], pad_down(kPadPins[i]), nowMs, &edges);
    frame->down[i] = edges.down;
    frame->tap[i] = edges.pressed;
  }

  frame->knobMoved = false;
  frame->knob = pot.shown;
  frame->volumeStep = 0;
  if (!potReady) {
    return;
  }
  pot_filter_push(&pot, sample);
  frame->knob = pot.shown;
  frame->knobMoved = pot.moved;
  if ((int32_t)(nowMs - nextVolumeMs) >= 0) {
    const int step = pot_filter_take_step(&pot);
    if (step != 0) {
      frame->volumeStep = step;
      nextVolumeMs = nowMs + kVolumeStepMs;
    }
  }
}
