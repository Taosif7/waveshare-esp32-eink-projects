#include <Arduino.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "epaper_config.h"
#include "exio/esp_io_expander_tca9554.h"
#include "game.h"
#include "port_display.h"
#include "port_i2c.h"
#include "ui.h"

enum class BtnEvent : uint8_t {
  Press = 1,
  Release = 2,
  Pause = 3,
  Reset = 4,
};

static I2cMasterBus *i2c_bus = nullptr;
static esp_io_expander_handle_t io_expander = nullptr;
static QueueHandle_t btn_queue = nullptr;
static GameState game;

static void post_btn(BtnEvent event) {
  if (btn_queue == nullptr) {
    return;
  }
  xQueueSend(btn_queue, &event, 0);
}

static bool button_down() {
  const bool ext = gpio_get_level(EXT_BUTTON_PIN) == 1;
  const bool boot = gpio_get_level(BOOT_BUTTON_PIN) == 0;
  return ext || boot;
}

static void configure_pins() {
  gpio_reset_pin(BOOT_BUTTON_PIN);
  gpio_reset_pin(EXT_BUTTON_PIN);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(EXT_BUTTON_PIN, INPUT_PULLDOWN);
}

static void board_power_on() {
  i2c_bus = I2cMasterBus::requestInstance(ESP32_I2C_SCL_PIN, ESP32_I2C_SDA_PIN, ESP32_I2C_DEV_NUM);
  assert(i2c_bus);
  ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(
      i2c_bus->Get_I2cBusHandle(), ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
  ESP_ERROR_CHECK(esp_io_expander_set_dir(
      io_expander,
      IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_3 | IO_EXPANDER_PIN_NUM_5,
      IO_EXPANDER_OUTPUT));
  ESP_ERROR_CHECK(esp_io_expander_set_level(
      io_expander,
      IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_3 | IO_EXPANDER_PIN_NUM_5, 1));
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
  Serial.println("Dino boot");

  board_power_on();
  PortLvgl_Start_Init();
  configure_pins();

  const uint32_t wait_start = millis();
  while (button_down() && (millis() - wait_start) < 600) {
    delay(20);
  }

  btn_queue = xQueueCreate(8, sizeof(BtnEvent));
  assert(btn_queue);
  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);

  game_init(&game);
  ui_compose(&game);
  ui_present(&game);
  game.dirty = false;
  game.full_refresh = false;
  game.base_refresh = false;
  game_sync_clock(&game, millis());

  Serial.println("Button GP3 active-high, BOOT backup");
}

void loop() {
  BtnEvent event;
  while (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
    apply_btn(event);
  }

  const bool playing = game.screen == Screen::Playing;
  game_tick(&game, millis());

  const bool need_frame = game.dirty || playing;
  static bool epd_updating = false;
  static bool saw_busy = false;
  static uint32_t update_started_ms = 0;

  if (epd_updating) {
    if (EPD_IsBusy()) {
      saw_busy = true;
    } else if (saw_busy || (millis() - update_started_ms) > 800) {
      epd_updating = false;
      saw_busy = false;
    }
  }

  if (need_frame && !epd_updating) {
    ui_compose(&game);
    const bool blocking = game.full_refresh || game.base_refresh;
    if (blocking) {
      ui_present(&game);
      game_sync_clock(&game, millis());
    } else {
      ui_present_begin(&game);
      epd_updating = true;
      saw_busy = false;
      update_started_ms = millis();
    }
    game.dirty = false;
    game.full_refresh = false;
    game.base_refresh = false;
  }

  delay(playing ? 2 : 10);
}
