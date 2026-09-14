#include <Arduino.h>
#include <OneButton.h>
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
  Start = 0,
  Title = 1,
};

static I2cMasterBus *i2c_bus = nullptr;
static esp_io_expander_handle_t io_expander = nullptr;
static OneButton boot_button;
static OneButton power_button;
static QueueHandle_t btn_queue = nullptr;
static GameState game;
static volatile int joy_x_raw = 2048;

static void post_btn(BtnEvent event) {
  if (btn_queue == nullptr) {
    return;
  }
  xQueueOverwrite(btn_queue, &event);
}

static void on_boot_click() {
  post_btn(BtnEvent::Start);
}

static void on_pwr_click() {
  post_btn(BtnEvent::Start);
}

static void on_pwr_long() {
  post_btn(BtnEvent::Title);
}

static void configure_button_gpio(gpio_num_t pin) {
  gpio_reset_pin(pin);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(pin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
}

static void configure_joystick() {
  gpio_reset_pin(JOY_VRX_PIN);
  gpio_reset_pin(JOY_VRY_PIN);
  gpio_reset_pin(JOY_SW_PIN);
  pinMode(JOY_VRX_PIN, INPUT);
  pinMode(JOY_VRY_PIN, INPUT);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  analogReadResolution(12);
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

static void poll_joystick() {
  static int sw_stable = 1;
  static int sw_last_raw = 1;
  static uint32_t sw_change_ms = 0;

  joy_x_raw = analogRead(JOY_VRX_PIN);

  const uint32_t now = millis();
  const int sw_raw = digitalRead(JOY_SW_PIN);
  if (sw_raw != sw_last_raw) {
    sw_last_raw = sw_raw;
    sw_change_ms = now;
  }
  if ((now - sw_change_ms) < 25 || sw_raw == sw_stable) {
    return;
  }
  sw_stable = sw_raw;
  if (sw_stable == 0) {
    Serial.println("JOY SW -> Start");
    post_btn(BtnEvent::Start);
  }
}

static void button_task(void *arg) {
  (void)arg;
  while (true) {
    boot_button.tick();
    power_button.tick();
    poll_joystick();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void apply_btn_event(BtnEvent event) {
  switch (event) {
    case BtnEvent::Start:
      if (game.screen != Screen::Playing) {
        game_start(&game);
      }
      break;
    case BtnEvent::Title:
      game_to_title(&game);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Modak Catcher boot");

  board_power_on();

  configure_button_gpio(BOOT_BUTTON_PIN);
  configure_button_gpio(PWR_BUTTON_PIN);

  power_button.setup(PWR_BUTTON_PIN, INPUT_PULLUP, true);
  boot_button.setup(BOOT_BUTTON_PIN, INPUT_PULLUP, true);
  boot_button.setDebounceMs(30);
  power_button.setDebounceMs(30);
  boot_button.setClickMs(400);
  power_button.setClickMs(400);
  power_button.setPressMs(900);

  while (gpio_get_level(PWR_BUTTON_PIN) == 0) {
    delay(50);
  }

  boot_button.attachClick(on_boot_click);
  power_button.attachClick(on_pwr_click);
  power_button.attachLongPressStart(on_pwr_long);

  btn_queue = xQueueCreate(1, sizeof(BtnEvent));
  assert(btn_queue);

  PortLvgl_Start_Init();

  // Reclaim GP3/GP4 after SPI/display init (shared with SD)
  configure_joystick();
  Serial.printf("Joystick X=GP%d Y=GP%d SW=GP%d (TXD)\n", (int)JOY_VRX_PIN, (int)JOY_VRY_PIN,
                (int)JOY_SW_PIN);

  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);

  game_init(&game);
  ui_compose(&game);
  ui_present(&game);
  game.dirty = false;
  game.full_refresh = false;
  game.base_refresh = false;

  Serial.printf("BOOT=%d PWR=%d JOY X=%d Y=%d SW=%d\n", gpio_get_level(BOOT_BUTTON_PIN),
                gpio_get_level(PWR_BUTTON_PIN), analogRead(JOY_VRX_PIN), analogRead(JOY_VRY_PIN),
                digitalRead(JOY_SW_PIN));
}

void loop() {
  BtnEvent event;
  while (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
    apply_btn_event(event);
  }

  const int joy_x = joy_x_raw;
  game_tick(&game, joy_x, millis());

  const bool playing = game.screen == Screen::Playing;
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
    if (game.full_refresh || game.base_refresh) {
      ui_present(&game);
    } else {
      ui_present_begin(&game);
      epd_updating = true;
      saw_busy = false;
      update_started_ms = millis();
    }
    game.dirty = false;
    game.full_refresh = false;
    game.base_refresh = false;
    game.hud_dirty = false;
  }

  delay(playing ? 2 : 10);
}
