#include <Arduino.h>

#include "draw.h"
#include "game.h"

namespace {

Game game;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("sling-bird boot");
  if (!display_begin()) {
    Serial.println("OLED missing. Halt.");
    for (;;) {
      delay(1000);
    }
  }
  game_init(&game);
  Serial.println("Pot GPIO3 angle. Swipe pads right to left, release to fire.");
}

void loop() {
  static uint32_t nextMs = 0;
  const uint32_t now = millis();
  game_step(&game, now);
  draw_game(&game);
  if (nextMs == 0) {
    nextMs = now;
  }
  nextMs += 33;
  if (nextMs < now) {
    nextMs = now;
  } else {
    delay(nextMs - now);
  }
}
