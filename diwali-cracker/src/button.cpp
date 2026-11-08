#include "button.h"

#include <Arduino.h>

#include "config.h"

static bool s_stable_down = false;
static bool s_last_raw = false;
static uint32_t s_last_change_ms = 0;
static bool s_latched_press = false;

void button_begin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  s_last_raw = digitalRead(PIN_BUTTON) == LOW;
  s_stable_down = s_last_raw;
  s_last_change_ms = millis();
}

bool button_was_pressed() {
  const bool raw_down = digitalRead(PIN_BUTTON) == LOW;
  const uint32_t now = millis();

  if (raw_down != s_last_raw) {
    s_last_raw = raw_down;
    s_last_change_ms = now;
  }

  if ((now - s_last_change_ms) >= BUTTON_DEBOUNCE_MS && raw_down != s_stable_down) {
    s_stable_down = raw_down;
    if (s_stable_down) {
      s_latched_press = true;
    }
  }

  if (!s_latched_press) {
    return false;
  }
  s_latched_press = false;
  return true;
}
