#include <stdio.h>
#include <stdlib.h>

#include "input_logic.h"
#include "transport.h"

namespace {

int gFailures = 0;

void expect(bool ok, const char *name) {
  if (ok) {
    printf("  ok  %s\n", name);
    return;
  }
  printf("FAIL  %s\n", name);
  gFailures += 1;
}

int drain(PotFilter *pot) {
  int sent = 0;
  for (;;) {
    const int step = pot_filter_take_step(pot);
    if (step == 0) {
      break;
    }
    sent += step;
  }
  return sent;
}

void test_debounce() {
  PadDebounce pad;
  pad_debounce_init(&pad);
  PadEdges edges;
  pad_debounce_update(&pad, true, 0, &edges);
  expect(!edges.pressed && !edges.down, "press shorter than debounce is ignored");
  pad_debounce_update(&pad, true, 24, &edges);
  expect(!edges.pressed, "24 ms is still inside the debounce window");
  pad_debounce_update(&pad, true, 25, &edges);
  expect(edges.pressed && edges.down, "stable press produces one edge");
  pad_debounce_update(&pad, true, 80, &edges);
  expect(!edges.pressed && edges.down, "holding does not repeat the edge");
  pad_debounce_update(&pad, false, 90, &edges);
  expect(edges.down && !edges.released, "release is debounced too");
  pad_debounce_update(&pad, false, 115, &edges);
  expect(edges.released && !edges.down, "stable release produces one edge");

  pad_debounce_init(&pad);
  pad_debounce_update(&pad, true, 0, &edges);
  pad_debounce_update(&pad, false, 5, &edges);
  pad_debounce_update(&pad, true, 10, &edges);
  pad_debounce_update(&pad, false, 40, &edges);
  expect(!edges.pressed && !edges.down, "a bounce never becomes a press");
}

void test_pot() {
  PotFilter pot;
  pot_filter_reset(&pot, 0);
  expect(pot.step == 0 && pot.queued == 0, "reset at the bottom queues nothing");

  int sent = 0;
  for (int sample = 0; sample <= 4095; ++sample) {
    for (int repeat = 0; repeat < 8; ++repeat) {
      pot_filter_push(&pot, sample);
      sent += drain(&pot);
    }
  }
  expect(sent == 16, "full turn emits 16 volume steps");
  expect(pot.step == 16, "full turn lands on the top step");

  sent = 0;
  for (int sample = 4095; sample >= 0; --sample) {
    for (int repeat = 0; repeat < 8; ++repeat) {
      pot_filter_push(&pot, sample);
      sent += drain(&pot);
    }
  }
  expect(sent == -16, "full turn back emits 16 volume-down steps");
  expect(pot.step == 0, "the return trip lands on zero");

  pot_filter_reset(&pot, 2000);
  sent = 0;
  bool moved = false;
  for (int i = 0; i < 500; ++i) {
    pot_filter_push(&pot, 2000 + (i % 11) - 5);
    moved = moved || pot.moved;
    sent += drain(&pot);
  }
  expect(sent == 0 && !moved, "a few counts of ADC noise do not move the bar or the keys");

  pot_filter_reset(&pot, 0);
  for (int i = 0; i < 40; ++i) {
    pot_filter_push(&pot, 270);
  }
  expect(drain(&pot) == 0 && pot.step == 0, "short of the first gate there is no key");
  for (int i = 0; i < 40; ++i) {
    pot_filter_push(&pot, 360);
  }
  expect(drain(&pot) == 1 && pot.step == 1, "crossing the first gate emits one step");
  const int shown = pot.shown;
  for (int i = 0; i < 40; ++i) {
    pot_filter_push(&pot, shown + ((i % 5) - 2));
  }
  expect(drain(&pot) == 0 && pot.step == 1, "noise on the new step does not chatter");
}

void test_transport() {
  Transport deck;
  transport_init(&deck);
  transport_toggle_play(&deck);
  for (int i = 0; i < 4000; ++i) {
    transport_tick(&deck, 33);
  }
  expect(deck.tape == kTapeMax, "play runs the tape to the end and stops there");
  expect(deck.omega > 20000, "play eases up to a steady spin");

  transport_init(&deck);
  transport_toggle_play(&deck);
  for (int i = 0; i < 60; ++i) {
    transport_tick(&deck, 33);
  }
  const int32_t played = deck.tape;
  const int32_t playOmega = deck.omega;

  transport_init(&deck);
  transport_seek(&deck, 1);
  for (int i = 0; i < 60; ++i) {
    transport_tick(&deck, 33);
  }
  expect(deck.tape > played * 3, "fast forward moves tape much faster than play");
  expect(deck.omega > playOmega * 3, "fast forward spins much faster than play");

  const int32_t midSpin = deck.omega;
  transport_seek(&deck, 0);
  transport_tick(&deck, 33);
  expect(deck.omega < midSpin, "releasing seek starts to decelerate");

  transport_init(&deck);
  transport_seek(&deck, -1);
  for (int i = 0; i < 200; ++i) {
    transport_tick(&deck, 33);
  }
  expect(deck.tape == 0, "rewind at the start stays clamped");

  transport_init(&deck);
  transport_toggle_play(&deck);
  for (int i = 0; i < 200; ++i) {
    transport_tick(&deck, 33);
  }
  const int32_t mid = deck.tape;
  transport_toggle_play(&deck);
  for (int i = 0; i < 80; ++i) {
    transport_tick(&deck, 33);
  }
  expect(deck.tape == mid, "pause stops the tape");
  expect(deck.omega < 800 && deck.omega > -800, "pause eases the reels to a stop");

  transport_init(&deck);
  transport_toggle_play(&deck);
  for (int i = 0; i < 30; ++i) {
    transport_tick(&deck, 33);
  }
  transport_track(&deck, 1);
  expect(deck.tape == 0, "next track resets the timer");
  const int32_t whipped = deck.omega;
  expect(whipped > playOmega, "next track whips the reels");
  for (int i = 0; i < 50; ++i) {
    transport_tick(&deck, 33);
  }
  expect(deck.omega < whipped, "the whip decelerates");

  transport_init(&deck);
  transport_track(&deck, -1);
  expect(deck.omega < 0, "previous track whips backward");

  transport_init(&deck);
  int pop = 0;
  expect(!transport_volume_visible(&deck, 1000, &pop), "the meter is hidden until the knob moves");
  transport_note_knob(&deck, 1000);
  expect(transport_volume_visible(&deck, 1120, &pop) && pop > 200, "the meter pops open");
  expect(transport_volume_visible(&deck, 1800, &pop) && pop == 255, "the meter stays up");
  expect(!transport_volume_visible(&deck, 2600, &pop), "the meter hides after about 1.5 s");
}

}  // namespace

int main() {
  test_debounce();
  test_pot();
  test_transport();
  if (gFailures != 0) {
    printf("%d failed\n", gFailures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
