#include <Arduino.h>
#include <OneButton.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "app.h"
#include "battery.h"
#include "ble.h"
#include "epaper_config.h"
#include "exio/esp_io_expander_tca9554.h"
#include "port_display.h"
#include "port_i2c.h"
#include "sound.h"

static I2cMasterBus *i2c_bus = nullptr;
static esp_io_expander_handle_t io_expander = nullptr;
static OneButton boot_button;
static OneButton power_button;
static QueueHandle_t btn_queue = nullptr;
static bool swallow_click = false;

static void post_btn(BtnEvent event) {
  if (btn_queue == nullptr) {
    return;
  }
  xQueueSend(btn_queue, &event, 0);
}

static void on_boot_click() {
  post_btn(BtnEvent::Next);
}

static void on_pwr_click() {
  if (swallow_click) {
    swallow_click = false;
    return;
  }
  post_btn(BtnEvent::Confirm);
}

static void on_pwr_long() {
  swallow_click = true;
  post_btn(BtnEvent::Dismiss);
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
  while (true) {
    boot_button.tick();
    power_button.tick();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
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
  btn_queue = xQueueCreate(8, sizeof(BtnEvent));
  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);

  PortLvgl_Start_Init();
  battery_init();
  if (!sound_init()) {
    Serial.println("audio init failed");
  }
  ble_init();
  app_init();
  app_set_battery(battery_percent());
}

void loop() {
  char line[200];
  while (ble_take_line(line, sizeof(line))) {
    app_on_line(line);
  }
  BtnEvent event;
  while (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
    app_on_button(event);
  }
  static uint32_t next_batt_ms = 0;
  if (static_cast<int32_t>(millis() - next_batt_ms) >= 0) {
    app_set_battery(battery_percent());
    next_batt_ms = millis() + 30000;
  }
  app_tick();
  delay(10);
}
