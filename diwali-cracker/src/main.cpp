#include <Arduino.h>

#include "button.h"
#include "config.h"
#include "leds.h"
#include "web.h"

static constexpr uint8_t MAX_TRAINS = 12;

struct Train {
  bool active;
  uint16_t head;
  uint32_t next_step_ms;
};

static Train s_trains[MAX_TRAINS];

static int alloc_train() {
  for (uint8_t i = 0; i < MAX_TRAINS; i++) {
    if (!s_trains[i].active) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static void start_train() {
  const int id = alloc_train();
  if (id < 0) {
    return;
  }
  s_trains[id].active = true;
  s_trains[id].head = 0;
  s_trains[id].next_step_ms = millis();
  web_broadcast("fuse");
  Serial.println("Train start");
}

static void render_trains() {
  uint16_t heads[MAX_TRAINS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_TRAINS; i++) {
    if (s_trains[i].active) {
      heads[n++] = s_trains[i].head;
    }
  }
  leds_show_trains(heads, n);
}

static void tick_trains() {
  const uint32_t now = millis();
  bool changed = false;

  for (uint8_t i = 0; i < MAX_TRAINS; i++) {
    Train &t = s_trains[i];
    if (!t.active) {
      continue;
    }
    if (now < t.next_step_ms) {
      continue;
    }
    changed = true;
    t.next_step_ms = now + CHASE_MS_PER_LED;
    if (t.head + 1 >= LED_COUNT) {
      t.active = false;
      web_broadcast("blast");
      Serial.println("Blast");
    } else {
      t.head++;
    }
  }

  if (changed) {
    render_trains();
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("diwali-cracker");

  for (uint8_t i = 0; i < MAX_TRAINS; i++) {
    s_trains[i].active = false;
  }

  leds_begin();
  button_begin();

  if (!web_begin()) {
    Serial.println("Web/Wi-Fi failed — LED chase still works offline");
  } else {
    Serial.print("Open http://");
    Serial.print(web_ip());
    Serial.println('/');
  }
}

void loop() {
  if (button_was_pressed()) {
    start_train();
    render_trains();
  }
  tick_trains();
  web_loop();
  delay(1);
}
