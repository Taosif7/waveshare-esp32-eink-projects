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
#include "sound.h"
#include "ui.h"

enum class BtnEvent : uint8_t {
  Cycle = 0,
  Confirm = 1,
  Reset = 2,
  Rock = 3,
  Paper = 4,
  Scissors = 5,
  Select = 6,
  Home = 7,
};

static I2cMasterBus *i2c_bus = nullptr;
static esp_io_expander_handle_t io_expander = nullptr;
static OneButton boot_button;
static OneButton power_button;
static QueueHandle_t btn_queue = nullptr;
static GameState game;

static constexpr int kJoyCenter = 2048;
static constexpr int kJoyDeadzone = 600;
static constexpr uint32_t kJoyCooldownMs = 250;

static void post_btn(BtnEvent event) {
  if (btn_queue == nullptr) {
    return;
  }
  xQueueOverwrite(btn_queue, &event);
}

static void on_boot_click() {
  post_btn(BtnEvent::Cycle);
}

static void on_pwr_click() {
  post_btn(BtnEvent::Confirm);
}

static void on_pwr_long() {
  post_btn(BtnEvent::Reset);
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

enum class JoyDir : uint8_t { None = 0, Left, Right, Up, Down };

static JoyDir read_joy_dir() {
  const int x = analogRead(JOY_VRX_PIN);
  const int y = analogRead(JOY_VRY_PIN);
  const int dx = x - kJoyCenter;
  const int dy = y - kJoyCenter;
  const int ax = dx < 0 ? -dx : dx;
  const int ay = dy < 0 ? -dy : dy;

  if (ax < kJoyDeadzone && ay < kJoyDeadzone) {
    return JoyDir::None;
  }

  if (ax >= ay) {
    return dx < 0 ? JoyDir::Left : JoyDir::Right;
  }
  // Probe saw low Y as one extreme and high as the other; map low = Up (Top).
  return dy < 0 ? JoyDir::Up : JoyDir::Down;
}

static void poll_joystick() {
  static JoyDir last_dir = JoyDir::None;
  static uint32_t last_dir_event_ms = 0;
  static int sw_stable = 1;
  static int sw_last_raw = 1;
  static uint32_t sw_change_ms = 0;

  const uint32_t now = millis();
  const JoyDir dir = read_joy_dir();

  if (dir == JoyDir::None) {
    last_dir = JoyDir::None;
  } else if (dir != last_dir && (now - last_dir_event_ms) >= kJoyCooldownMs) {
    last_dir = dir;
    last_dir_event_ms = now;
    switch (dir) {
      case JoyDir::Left:
        Serial.println("JOY Left -> Rock");
        post_btn(BtnEvent::Rock);
        break;
      case JoyDir::Right:
        Serial.println("JOY Right -> Paper");
        post_btn(BtnEvent::Paper);
        break;
      case JoyDir::Up:
        Serial.println("JOY Up -> Scissors");
        post_btn(BtnEvent::Scissors);
        break;
      case JoyDir::Down:
        Serial.println("JOY Down -> Select");
        post_btn(BtnEvent::Select);
        break;
      case JoyDir::None:
        break;
    }
  }

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
    Serial.println("JOY SW -> Home");
    post_btn(BtnEvent::Home);
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
    case BtnEvent::Cycle:
      game_on_boot_click(&game);
      break;
    case BtnEvent::Confirm:
      game_on_pwr_click(&game);
      break;
    case BtnEvent::Reset:
      game_on_pwr_long(&game);
      break;
    case BtnEvent::Rock:
      game_on_move(&game, Move::Rock);
      break;
    case BtnEvent::Paper:
      game_on_move(&game, Move::Paper);
      break;
    case BtnEvent::Scissors:
      game_on_move(&game, Move::Scissors);
      break;
    case BtnEvent::Select:
      game_on_select(&game);
      break;
    case BtnEvent::Home:
      game_on_home(&game);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("RPS Ink Duel boot (joystick)");

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
  Serial.printf("Joystick X=GP%d Y=GP%d SW=GP%d (TXD)\n",
                (int)JOY_VRX_PIN, (int)JOY_VRY_PIN, (int)JOY_SW_PIN);

  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);

  if (!sound_init()) {
    Serial.println("Audio init failed — continuing muted");
  }

  game_init(&game);
  if (game.sfx != Sfx::None) {
    sound_play(game.sfx);
    game.sfx = Sfx::None;
  }
  ui_render(&game);
  game.dirty = false;

  Serial.printf("BOOT=%d PWR=%d JOY X=%d Y=%d SW=%d\n",
                gpio_get_level(BOOT_BUTTON_PIN), gpio_get_level(PWR_BUTTON_PIN),
                analogRead(JOY_VRX_PIN), analogRead(JOY_VRY_PIN), digitalRead(JOY_SW_PIN));
}

void loop() {
  BtnEvent event;
  while (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
    apply_btn_event(event);
  }

  game_tick(&game);

  if (game.dirty) {
    if (game.sfx != Sfx::None) {
      sound_play(game.sfx);
      game.sfx = Sfx::None;
    }
    ui_render(&game);
    game.dirty = false;
  }
  delay(10);
}
