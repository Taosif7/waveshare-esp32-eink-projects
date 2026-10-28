#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "game.h"
#include "oled.h"
#include "pins.h"

#ifndef TOUCH_ACTIVE_HIGH
#define TOUCH_ACTIVE_HIGH 1
#endif

namespace {

constexpr int kJoyCenter = 2048;
constexpr int kJoyDead = 600;
constexpr int kJoySpan = 1400;

struct Controls {
  volatile int joy_x;
  volatile int joy_y;
  volatile bool fire_held;
  volatile uint32_t fire_presses;
  volatile uint32_t use_presses;
};

Controls controls = {kJoyCenter, kJoyCenter, false, 0, 0};
uint32_t seen_fire = 0;
uint32_t seen_use = 0;

bool touch_down() {
#if TOUCH_ACTIVE_HIGH
  return digitalRead(kTouchPin) == HIGH;
#else
  return digitalRead(kTouchPin) == LOW;
#endif
}

bool fire_down() {
  return touch_down() || digitalRead(kFirePin) == HIGH;
}

int shape_axis(int raw) {
  int delta = raw - kJoyCenter;
  if (delta > -kJoyDead && delta < kJoyDead) {
    return 0;
  }
  if (delta > 0) {
    delta -= kJoyDead;
  } else {
    delta += kJoyDead;
  }
  if (delta > kJoySpan) {
    delta = kJoySpan;
  } else if (delta < -kJoySpan) {
    delta = -kJoySpan;
  }
  return delta;
}

void configure_controls() {
  gpio_reset_pin(static_cast<gpio_num_t>(kFirePin));
  gpio_reset_pin(static_cast<gpio_num_t>(kTouchPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kJoyXPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kJoyYPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kJoySwPin));
  pinMode(kFirePin, INPUT_PULLDOWN);
#if TOUCH_ACTIVE_HIGH
  pinMode(kTouchPin, INPUT_PULLDOWN);
#else
  pinMode(kTouchPin, INPUT_PULLUP);
#endif
  pinMode(kJoyXPin, INPUT);
  pinMode(kJoyYPin, INPUT);
  pinMode(kJoySwPin, INPUT_PULLUP);
  analogReadResolution(12);
}

void input_task(void *) {
  bool fire_raw_last = fire_down();
  bool fire_stable = fire_raw_last;
  uint32_t fire_change = millis();
  controls.fire_held = fire_stable;
  int use_raw_last = digitalRead(kJoySwPin);
  int use_stable = use_raw_last;
  uint32_t use_change = millis();

  while (true) {
    controls.joy_x = analogRead(kJoyXPin);
    controls.joy_y = analogRead(kJoyYPin);

    const bool fire_raw = fire_down();
    const uint32_t now = millis();
    if (fire_raw != fire_raw_last) {
      fire_raw_last = fire_raw;
      fire_change = now;
    }
    if ((now - fire_change) >= 25 && fire_raw != fire_stable) {
      fire_stable = fire_raw;
      controls.fire_held = fire_stable;
      if (fire_stable) {
        controls.fire_presses = controls.fire_presses + 1;
      }
    }

    const int use_raw = digitalRead(kJoySwPin);
    if (use_raw != use_raw_last) {
      use_raw_last = use_raw;
      use_change = now;
    }
    if ((now - use_change) >= 25 && use_raw != use_stable) {
      use_stable = use_raw;
      if (use_stable == 0) {
        controls.use_presses = controls.use_presses + 1;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

Input read_input() {
  Input input = {};
  input.axis_x = static_cast<int16_t>(shape_axis(controls.joy_x));
  input.axis_y = static_cast<int16_t>(shape_axis(controls.joy_y));
  input.fire_held = controls.fire_held;
  const uint32_t fires = controls.fire_presses;
  const uint32_t uses = controls.use_presses;
  input.fire_edge = fires != seen_fire;
  input.use_edge = uses != seen_use;
  seen_fire = fires;
  seen_use = uses;
  return input;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Mini Doom boot");

  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(400000);
  configure_controls();
  xTaskCreatePinnedToCore(input_task, "input", 3072, nullptr, 4, nullptr, 0);

  while (!oled_begin()) {
    Serial.printf("No OLED at 0x3C or 0x3D. VCC -> 3.3, GND -> G, SDA GPIO%d, SCL GPIO%d.\n",
                  kOledSda, kOledScl);
    delay(1000);
  }

  game_setup();
  Serial.println("Stick moves (low Y is forward). Touch on GPIO4 fires. Click uses doors, or the map.");
}

void loop() {
  static uint32_t next_ms = 0;
  const Input input = read_input();
  game_step(input);
  game_draw();

  const uint32_t now = millis();
  if (next_ms == 0) {
    next_ms = now;
  }
  next_ms += 33;
  if (next_ms < now) {
    next_ms = now;
  } else {
    delay(next_ms - now);
  }
}
