#include <Arduino.h>
#include <OneButton.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "app.h"
#include "epaper_config.h"
#include "exio/esp_io_expander_tca9554.h"
#include "port_display.h"
#include "port_i2c.h"
#include "stocks.h"
#include "ui.h"
#include "wifi_config.h"
#include "yahoo.h"

enum class BtnEvent : uint8_t {
  Next = 0,
  Confirm = 1,
  FunBuy = 2,
  FunSell = 3,
};

static constexpr bool kDoubleClickOrders = FUN_TRADING_ENABLED && FUN_GESTURE_DOUBLE_CLICK;
// A tap can only be final once the double-click window closes.
static constexpr unsigned int kClickMs = kDoubleClickOrders ? FUN_DOUBLE_CLICK_MS : 400;

static I2cMasterBus *i2c_bus = nullptr;
static esp_io_expander_handle_t io_expander = nullptr;
static OneButton boot_button;
static OneButton power_button;
static OneButton red_button;
static OneButton green_button;
static QueueHandle_t btn_queue = nullptr;
static AppState app;

static void post_btn(BtnEvent event) {
  if (btn_queue == nullptr) {
    return;
  }
  xQueueOverwrite(btn_queue, &event);
}

// Releasing a long press also reads as a click. Swallow clicks briefly after an
// order fires so letting go does not navigate away from the result splash.
static volatile uint32_t click_guard_until_ms = 0;

static bool click_guarded() {
  return static_cast<int32_t>(click_guard_until_ms - millis()) > 0;
}

static void on_next() {
  if (click_guarded()) {
    return;
  }
  post_btn(BtnEvent::Next);
}

static void on_confirm() {
  if (click_guarded()) {
    return;
  }
  post_btn(BtnEvent::Confirm);
}

static void on_fun_buy() {
  post_btn(BtnEvent::FunBuy);
}

static void on_fun_sell() {
  post_btn(BtnEvent::FunSell);
}

// GP3/GP4 double as EPD/SD SPI lines, so a panel refresh can make a held button
// read as released for a few ms. OneButton's own long press loses the hold that
// way, so track the raw level here and ignore dropouts shorter than a release.
struct HoldWatch {
  gpio_num_t pin;
  bool active_low;
  BtnEvent event;
  uint32_t down_since_ms;
  uint32_t last_active_ms;
  bool fired;
};

static constexpr uint32_t kHoldReleaseMs = 80;

static HoldWatch holds[] = {
    {GREEN_BUTTON_PIN, false, BtnEvent::FunBuy, 0, 0, false},
    {RED_BUTTON_PIN, false, BtnEvent::FunSell, 0, 0, false},
    {PWR_BUTTON_PIN, true, BtnEvent::FunBuy, 0, 0, false},
    {BOOT_BUTTON_PIN, true, BtnEvent::FunSell, 0, 0, false},
};

static void poll_holds() {
  const uint32_t now = millis();
  for (HoldWatch &h : holds) {
    const int level = gpio_get_level(h.pin);
    const bool pressed = h.active_low ? level == 0 : level == 1;
    if (pressed) {
      if (h.down_since_ms == 0) {
        h.down_since_ms = now;
        h.fired = false;
      }
      h.last_active_ms = now;
      if (!h.fired && (now - h.down_since_ms) >= FUN_LONG_PRESS_MS) {
        h.fired = true;
        click_guard_until_ms = now + FUN_DOUBLE_CLICK_MS + 800;
        post_btn(h.event);
      }
    } else if (h.down_since_ms != 0 && (now - h.last_active_ms) > kHoldReleaseMs) {
      h.down_since_ms = 0;
      h.fired = false;
    }
  }
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

static void configure_color_button_gpio(gpio_num_t pin) {
  gpio_reset_pin(pin);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(pin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
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
    red_button.tick();
    green_button.tick();
    if (FUN_TRADING_ENABLED && FUN_GESTURE_LONG_PRESS) {
      poll_holds();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static bool wifi_credentials_set() {
  return strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 && WIFI_SSID[0] != '\0';
}

static bool connect_wifi(uint32_t timeout_ms) {
  if (!wifi_credentials_set()) {
    Serial.println("WiFi SSID not set — edit src/wifi_config.h");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("stock-ticker");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s\n", WIFI_SSID);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - start) > timeout_ms) {
      Serial.println("WiFi timeout");
      return false;
    }
    delay(200);
  }
  Serial.printf("WiFi OK ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

static void apply_btn(BtnEvent event) {
  if (event == BtnEvent::FunBuy) {
    Serial.println("FUN BUY");
    app_on_fun_buy(&app);
  } else if (event == BtnEvent::FunSell) {
    Serial.println("FUN SELL");
    app_on_fun_sell(&app);
  } else if (event == BtnEvent::Next) {
    Serial.println("NEXT");
    app_on_button_a(&app);
  } else {
    Serial.println("CONFIRM");
    app_on_button_b(&app);
  }
}

static void present_if_dirty() {
  if (!app.dirty) {
    return;
  }
  ui_compose(&app);
  ui_present(&app);
  app.dirty = false;
  app.full_refresh = false;
  app.base_refresh = false;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Stock ticker boot");

  board_power_on();

  configure_button_gpio(BOOT_BUTTON_PIN);
  configure_button_gpio(PWR_BUTTON_PIN);

  power_button.setup(PWR_BUTTON_PIN, INPUT_PULLUP, true);
  boot_button.setup(BOOT_BUTTON_PIN, INPUT_PULLUP, true);
  boot_button.setDebounceMs(30);
  power_button.setDebounceMs(30);
  boot_button.setClickMs(kClickMs);
  power_button.setClickMs(kClickMs);

  while (gpio_get_level(PWR_BUTTON_PIN) == 0) {
    delay(50);
  }

  boot_button.attachClick(on_next);
  power_button.attachClick(on_confirm);
  if (kDoubleClickOrders) {
    boot_button.attachDoubleClick(on_fun_sell);
    power_button.attachDoubleClick(on_fun_buy);
  }

  btn_queue = xQueueCreate(1, sizeof(BtnEvent));
  assert(btn_queue);

  PortLvgl_Start_Init();

  // GP3/GP4 are shared with SD; reclaim them for green/red after display init.
  configure_color_button_gpio(RED_BUTTON_PIN);
  configure_color_button_gpio(GREEN_BUTTON_PIN);
  red_button.setup(RED_BUTTON_PIN, INPUT_PULLDOWN, false);
  green_button.setup(GREEN_BUTTON_PIN, INPUT_PULLDOWN, false);
  red_button.setDebounceMs(30);
  green_button.setDebounceMs(30);
  red_button.setClickMs(kClickMs);
  green_button.setClickMs(kClickMs);
  delay(200);
  red_button.attachClick(on_next);
  green_button.attachClick(on_confirm);
  if (kDoubleClickOrders) {
    red_button.attachDoubleClick(on_fun_sell);
    green_button.attachDoubleClick(on_fun_buy);
  }

  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);

  app_init(&app);
  present_if_dirty();

  if (connect_wifi(20000)) {
    app_wifi_ok(&app);
  } else {
    app_wifi_fail(&app);
  }
  present_if_dirty();
}

void loop() {
  BtnEvent event;
  while (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
    apply_btn(event);
  }

  if (app.screen == Screen::Wifi) {
    if (connect_wifi(20000)) {
      app_wifi_ok(&app);
    } else {
      app_wifi_fail(&app);
    }
  }

  if (app.screen == Screen::Chart && WiFi.status() != WL_CONNECTED) {
    strncpy(app.status, "NO WIFI", sizeof(app.status) - 1);
    app.dirty = true;
  } else if (app_should_poll(&app, millis())) {
    app.fetching = true;
    app.last_poll_ms = millis();
    const bool ok = yahoo_fetch_chart(stock_at(app.stock_index), &app.quote, range_at(app.range_index));
    app_quote_loaded(&app, ok);
  }

  present_if_dirty();
  delay(20);
}
