#include <Arduino.h>

#include "ble_hid.h"
#include "input.h"
#include "oled.h"
#include "pins.h"
#include "transport.h"

namespace {

Transport deck;
uint32_t lastTickMs = 0;
uint32_t fpsWindowMs = 0;
int fpsFrames = 0;
bool wasConnected = false;

// Pads 2 and 4 repeat Rewind / Fast Forward while the finger stays down.
// The first pulse is also the tap: a short press gets one press of that usage.
constexpr uint32_t kPulseMs = 70;
constexpr uint32_t kRepeatMs = 150;

void pulse_while_down(bool down, uint8_t bit, uint32_t nowMs, uint32_t *nextMs) {
  if (!down) {
    *nextMs = nowMs;
    return;
  }
  if ((int32_t)(nowMs - *nextMs) < 0) {
    return;
  }
  ble_hid_pulse(bit, nowMs, kPulseMs);
  *nextMs = nowMs + kRepeatMs;
}

UiView make_view(const InputFrame *input, uint32_t nowMs) {
  UiView view{};
  view.connected = ble_hid_connected();
  view.playing = deck.playing;
  view.seek = deck.seek;
  view.angle = static_cast<uint16_t>(deck.angle);
  view.tape = deck.tape;
  if (view.tape < 0) {
    view.tape = 0;
  }
  if (view.tape > 1000) {
    view.tape = 1000;
  }
  int pop = 0;
  view.volume = transport_volume_visible(&deck, nowMs, &pop);
  view.volumePop = pop;
  int pos = input->knob;
  if (pos < 0) {
    pos = 0;
  }
  if (pos > kKnobMax) {
    pos = kKnobMax;
  }
  view.volumePos = pos * 1000 / kKnobMax;
  view.nowMs = nowMs;
  return view;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("cassette boot");
  if (!oled_begin()) {
    Serial.println("OLED missing. Halt.");
    for (;;) {
      delay(1000);
    }
  }
  input_begin();
  transport_init(&deck);
  UiView splash{};
  splash.nowMs = millis();
  oled_draw(&splash);
  ble_hid_begin();
  lastTickMs = millis();
  fpsWindowMs = lastTickMs;
  Serial.println("pads: prev, rewind, play, forward, next. pot is volume. serial 'c' clears bonds.");
}

void loop() {
  const uint32_t frameStart = millis();
  uint32_t dt = frameStart - lastTickMs;
  lastTickMs = frameStart;
  if (dt > 80) {
    dt = 80;
  }

  while (Serial.available() > 0) {
    const char key = static_cast<char>(Serial.read());
    if (key == 'c' || key == 'C') {
      ble_hid_forget();
    }
  }

  InputFrame input{};
  input_poll(&input, frameStart);

  if (input.tap[kPadPlay]) {
    // Local toggle: the phone does not report play state back. Phase 2 would
    // replace this, and the tape position, with Apple Media Service updates.
    transport_toggle_play(&deck);
    ble_hid_pulse(kHidPlay, frameStart, kPulseMs);
    Serial.println(deck.playing ? "play" : "pause");
  }
  if (input.tap[kPadPrev]) {
    transport_track(&deck, -1);
    ble_hid_pulse(kHidPrev, frameStart, kPulseMs);
    Serial.println("previous");
  }
  if (input.tap[kPadNext]) {
    transport_track(&deck, 1);
    ble_hid_pulse(kHidNext, frameStart, kPulseMs);
    Serial.println("next");
  }

  const bool rewind = input.down[kPadRewind] && !input.down[kPadForward];
  const bool forward = input.down[kPadForward] && !input.down[kPadRewind];
  static uint32_t nextRewindMs = 0;
  static uint32_t nextForwardMs = 0;
  pulse_while_down(rewind, kHidRewind, frameStart, &nextRewindMs);
  pulse_while_down(forward, kHidForward, frameStart, &nextForwardMs);
  if (rewind) {
    transport_seek(&deck, -1);
  } else if (forward) {
    transport_seek(&deck, 1);
  } else {
    transport_seek(&deck, 0);
  }

  if (input.volumeStep > 0) {
    ble_hid_pulse(kHidVolumeUp, frameStart, 50);
  } else if (input.volumeStep < 0) {
    ble_hid_pulse(kHidVolumeDown, frameStart, 50);
  }
  // Ignore the first fraction of a second. The ADC often walks a few counts
  // while the rail settles, and that should not pop the meter at boot.
  if (input.knobMoved && frameStart > 400) {
    transport_note_knob(&deck, frameStart);
  }

  const bool connected = ble_hid_connected();
  if (connected && !wasConnected) {
    deck.angle = static_cast<uint32_t>(frameStart) * 14;
  }
  wasConnected = connected;

  ble_hid_service(frameStart);
  transport_tick(&deck, dt);

  const UiView view = make_view(&input, frameStart);
  oled_draw(&view);

  fpsFrames += 1;
  if (frameStart - fpsWindowMs >= 2000) {
    Serial.printf("fps=%d\n", fpsFrames * 1000 / (int)(frameStart - fpsWindowMs));
    fpsWindowMs = frameStart;
    fpsFrames = 0;
  }

  const uint32_t spent = millis() - frameStart;
  if (spent < 33) {
    delay(33 - spent);
  }
}
