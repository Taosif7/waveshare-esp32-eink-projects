#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "game.h"
#include "oled.h"
#include "ui.h"

constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr gpio_num_t kExtPin = GPIO_NUM_3;

enum class BtnEvent : uint8_t {
  Press = 1,
  Release = 2,
  Pause = 3,
  Reset = 4,
};

static QueueHandle_t btn_queue = nullptr;
static GameState game;

static void post_btn(BtnEvent event) {
  if (btn_queue == nullptr) {
    return;
  }
  xQueueSend(btn_queue, &event, 0);
}

static bool button_down() {
  const bool ext = gpio_get_level(kExtPin) == 1;
  const bool boot = gpio_get_level(kBootPin) == 0;
  return ext || boot;
}

static void configure_pins() {
  gpio_reset_pin(kBootPin);
  gpio_reset_pin(kExtPin);
  pinMode(kBootPin, INPUT_PULLUP);
  pinMode(kExtPin, INPUT_PULLDOWN);
}

static void scan_i2c() {
  Serial.println("I2C scan");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
    }
  }
}

static void button_task(void *arg) {
  (void)arg;
  bool stable = false;
  bool last_raw = false;
  uint32_t change_ms = 0;
  uint32_t down_at = 0;
  bool pause_sent = false;
  bool reset_sent = false;

  while (true) {
    const uint32_t now = millis();
    const bool raw = button_down();
    if (raw != last_raw) {
      last_raw = raw;
      change_ms = now;
    }
    if ((now - change_ms) >= 25 && raw != stable) {
      stable = raw;
      if (stable) {
        down_at = now;
        pause_sent = false;
        reset_sent = false;
        post_btn(BtnEvent::Press);
      } else {
        post_btn(BtnEvent::Release);
      }
    }
    if (stable && !pause_sent && (now - down_at) >= 1000) {
      pause_sent = true;
      post_btn(BtnEvent::Pause);
    }
    if (stable && !reset_sent && (now - down_at) >= 2500) {
      reset_sent = true;
      post_btn(BtnEvent::Reset);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void apply_btn(BtnEvent event) {
  switch (event) {
    case BtnEvent::Press:
      game_press(&game);
      break;
    case BtnEvent::Release:
      game_release(&game);
      break;
    case BtnEvent::Pause:
      game_pause(&game);
      break;
    case BtnEvent::Reset:
      game_reset(&game);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Dino OLED boot");

  if (!oled_begin()) {
    scan_i2c();
    Serial.println("SH1106 not found at 0x3C or 0x3D");
    while (true) {
      delay(1000);
    }
  }
  oled.clearBuffer();
  oled.sendBuffer();

  configure_pins();
  const uint32_t wait_start = millis();
  while (button_down() && (millis() - wait_start) < 600) {
    delay(20);
  }

  btn_queue = xQueueCreate(8, sizeof(BtnEvent));
  assert(btn_queue);
  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);

  game_init(&game);
  ui_draw(&game);
  game_sync_clock(&game, millis());
  Serial.println("OLED 128x64 on SDA 18 SCL 8, button GP3");
}

void loop() {
  BtnEvent event;
  while (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
    apply_btn(event);
  }

  game_tick(&game, millis());
  ui_draw(&game);
}
