#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "eink_warn.h"
#include "game.h"
#include "oled.h"
#include "sound.h"
#include "ui.h"

namespace {

constexpr int kSdaPin = 18;
constexpr int kSclPin = 8;
constexpr int kBootPin = 9;
constexpr int kPwrPin = 2;
constexpr int kFirePin = 17;
constexpr int kJoyCenter = 2048;
constexpr int kJoyDead = 600;
constexpr int kJoySpan = 1400;

constexpr uint8_t kTca9554Address = 0x20;
constexpr uint8_t kTcaOutputReg = 0x01;
constexpr uint8_t kTcaConfigReg = 0x03;
constexpr uint8_t kBatteryHoldBit = 1 << 5;
constexpr uint8_t kEpdPowerBit = 1 << 0;
constexpr uint8_t kAudioPowerBit = 1 << 1;
constexpr uint8_t kAmpBit = 1 << 3;
constexpr uint8_t kRailMask = kBatteryHoldBit | kEpdPowerBit | kAudioPowerBit | kAmpBit;

struct Controls {
  volatile int joy_x;
  volatile int joy_y;
  volatile bool fire_held;
  volatile uint32_t fire_presses;
  volatile uint32_t click_presses;
};

Controls controls = {kJoyCenter, kJoyCenter, false, 0, 0};
Game game;
uint32_t seen_fire = 0;
uint32_t seen_click = 0;

bool writeDevReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Absolute write. A read-modify-write can put the battery latch back if the
// read fails, which is how the vendor shutdown drops the rail.
bool writeRails(bool battery_on, bool epd_on, bool audio_on) {
  uint8_t output = 0;
  if (battery_on) {
    output |= kBatteryHoldBit;
  }
  if (epd_on) {
    output |= kEpdPowerBit;
  }
  if (audio_on) {
    output |= kAudioPowerBit | kAmpBit;
  }
  const uint8_t config = static_cast<uint8_t>(~kRailMask);
  return writeDevReg(kTca9554Address, kTcaOutputReg, output) &&
         writeDevReg(kTca9554Address, kTcaConfigReg, config);
}

void holdBatteryPower() {
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (writeRails(true, true, true)) {
      Serial.println("Battery hold on (TCA9554 EXIO5). E-paper and amp powered.");
      return;
    }
    delay(30);
  }
  Serial.println("TCA9554 0x20 did not latch. Battery drops when PWR is released.");
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
  gpio_reset_pin(static_cast<gpio_num_t>(kBootPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kPwrPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kFirePin));
  gpio_reset_pin(GPIO_NUM_4);
  gpio_reset_pin(GPIO_NUM_3);
  gpio_reset_pin(GPIO_NUM_16);
  pinMode(kBootPin, INPUT_PULLUP);
  pinMode(kPwrPin, INPUT_PULLUP);
  pinMode(kFirePin, INPUT_PULLDOWN);
  pinMode(4, INPUT);
  pinMode(3, INPUT);
  pinMode(16, INPUT_PULLUP);
  analogReadResolution(12);
}

void input_task(void *) {
  bool fire_raw_last = digitalRead(kFirePin) == HIGH || digitalRead(kBootPin) == LOW;
  bool fire_stable = fire_raw_last;
  uint32_t fire_change = millis();
  controls.fire_held = fire_stable;
  int click_raw_last = digitalRead(16);
  int click_stable = click_raw_last;
  uint32_t click_change = millis();

  while (true) {
    controls.joy_x = analogRead(4);
    controls.joy_y = analogRead(3);

    const bool fire_raw = digitalRead(kFirePin) == HIGH || digitalRead(kBootPin) == LOW;
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

    const int click_raw = digitalRead(16);
    if (click_raw != click_raw_last) {
      click_raw_last = click_raw;
      click_change = now;
    }
    if ((now - click_change) >= 25 && click_raw != click_stable) {
      click_stable = click_raw;
      if (click_stable == 0) {
        controls.click_presses = controls.click_presses + 1;
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
  const uint32_t clicks = controls.click_presses;
  input.fire_edge = fires != seen_fire;
  input.click_edge = clicks != seen_click;
  seen_fire = fires;
  seen_click = clicks;
  return input;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Space shooter boot");

  pinMode(kBootPin, INPUT_PULLUP);
  pinMode(kPwrPin, INPUT_PULLUP);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);
  holdBatteryPower();
  sound_init();
  eink_begin();
  Wire.setClock(400000);

  configure_controls();
  xTaskCreatePinnedToCore(input_task, "input", 3072, nullptr, 4, nullptr, 0);

  while (!oled_begin()) {
    oled_scan_i2c();
    Serial.println("No OLED at 0x3C or 0x3D. VCC -> 3V3, GND -> GND, SDA -> SDA, SCL -> SCL.");
    delay(1000);
  }

  game_init(&game);
  ui_draw(&game);
  Serial.println("Portrait 64x128. Stick moves, RXD/BOOT fires, TXD click pauses.");
}

void loop() {
  static uint32_t next_ms = 0;
  const Input input = read_input();
  game_step(&game, &input);
  ui_draw(&game);

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
