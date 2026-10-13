#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_pins.h"
#include "eink_warn.h"
#include "game.h"
#include "oled.h"
#include "sound.h"
#include "tilt.h"
#include "ui.h"

// Flip a sign if that axis leans the wrong way. Roll is wing-down,
// pitch is nose-down. Positive roll banks the ship and shifts stars right.
static constexpr int kRollSign = 1;
static constexpr int kPitchSign = 1;

namespace {

constexpr int kJoyCenter = 2048;
constexpr int kJoyDead = 600;
constexpr int kJoySpan = 1400;

#if BOARD_HAS_RAILS
constexpr uint8_t kTca9554Address = 0x20;
constexpr uint8_t kTcaOutputReg = 0x01;
constexpr uint8_t kTcaConfigReg = 0x03;
constexpr uint8_t kBatteryHoldBit = 1 << 5;
constexpr uint8_t kEpdPowerBit = 1 << 0;
constexpr uint8_t kAudioPowerBit = 1 << 1;
constexpr uint8_t kAmpBit = 1 << 3;
constexpr uint8_t kRailMask = kBatteryHoldBit | kEpdPowerBit | kAudioPowerBit | kAmpBit;
#endif

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

#if BOARD_HAS_RAILS
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
#endif

bool fire_down() {
  bool down = digitalRead(kFirePin) == HIGH;
#if BOARD_HAS_BOOT_FIRE
  down = down || digitalRead(kBootPin) == LOW;
#endif
  return down;
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
#if BOARD_HAS_BOOT_FIRE
  gpio_reset_pin(static_cast<gpio_num_t>(kBootPin));
  pinMode(kBootPin, INPUT_PULLUP);
#endif
#if BOARD_HAS_PWR
  gpio_reset_pin(static_cast<gpio_num_t>(kPwrPin));
  pinMode(kPwrPin, INPUT_PULLUP);
#endif
  gpio_reset_pin(static_cast<gpio_num_t>(kFirePin));
  gpio_reset_pin(static_cast<gpio_num_t>(kJoyXPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kJoyYPin));
  gpio_reset_pin(static_cast<gpio_num_t>(kJoySwPin));
  pinMode(kFirePin, INPUT_PULLDOWN);
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
  int click_raw_last = digitalRead(kJoySwPin);
  int click_stable = click_raw_last;
  uint32_t click_change = millis();

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

    const int click_raw = digitalRead(kJoySwPin);
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
  int16_t roll = 0;
  int16_t pitch = 0;
  int16_t heave = 0;
  if (tilt_present() && tilt_sample(&roll, &pitch, &heave)) {
    input.tilt_ok = true;
    input.tilt_x = static_cast<int16_t>(roll * kRollSign);
    input.tilt_y = static_cast<int16_t>(pitch * kPitchSign);
    input.tilt_z = heave;
  }
  return input;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Space shooter boot");

#if BOARD_HAS_BOOT_FIRE
  pinMode(kBootPin, INPUT_PULLUP);
#endif
#if BOARD_HAS_PWR
  pinMode(kPwrPin, INPUT_PULLUP);
#endif

  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(100000);
#if BOARD_HAS_RAILS
  holdBatteryPower();
#endif
  sound_init();
#if BOARD_HAS_EINK
  eink_begin();
#endif
  Wire.setClock(400000);

  if (tilt_begin()) {
    Serial.printf("ADXL345 at 0x%02X. Tilt leans the stars. A hard shake scatters enemy shots.\n",
                  tilt_address());
  } else {
    Serial.printf("ADXL345 not found. SDA GPIO%d, SCL GPIO%d, CS -> 3V3, SDO -> GND, VCC -> 3V3.\n",
                  kOledSda, kOledScl);
    Serial.println("Stars will not warp. The ship still banks from the stick.");
  }

  configure_controls();
  xTaskCreatePinnedToCore(input_task, "input", 3072, nullptr, 4, nullptr, 0);

  while (!oled_begin()) {
    oled_scan_i2c();
    Serial.printf("No OLED at 0x3C or 0x3D. VCC -> 3V3, GND -> GND, SDA GPIO%d, SCL GPIO%d.\n",
                  kOledSda, kOledScl);
    delay(1000);
  }

  game_init(&game);
  ui_draw(&game);
#if BOARD_HAS_BOOT_FIRE
  Serial.println("Portrait 64x128. Stick moves, fire button or BOOT shoots, stick click pauses.");
#else
  Serial.println("Portrait 64x128. Stick moves, fire button shoots, stick click pauses.");
#endif
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
