#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>

#include "board_pins.h"
#include "eink_warn.h"
#include "lyrics.h"
#include "oled.h"
#include "player.h"

namespace {

#if BOARD_HAS_RAILS
constexpr uint8_t kTca9554Address = 0x20;
constexpr uint8_t kTcaOutputReg = 0x01;
constexpr uint8_t kTcaConfigReg = 0x03;
constexpr uint8_t kBatteryHoldBit = 1 << 5;
constexpr uint8_t kEpdPowerBit = 1 << 0;
constexpr uint8_t kRailMask = kBatteryHoldBit | kEpdPowerBit;

bool write_dev_reg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Absolute write. A read-modify-write can put the battery latch back if the
// read fails, which is how the vendor shutdown drops the rail.
bool write_rails() {
  const uint8_t output = kBatteryHoldBit | kEpdPowerBit;
  const uint8_t config = static_cast<uint8_t>(~kRailMask);
  return write_dev_reg(kTca9554Address, kTcaOutputReg, output) &&
         write_dev_reg(kTca9554Address, kTcaConfigReg, config);
}

void hold_battery_power() {
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (write_rails()) {
      Serial.println("Battery hold on (TCA9554 EXIO5). E-paper power on (EXIO0).");
      return;
    }
    delay(30);
  }
  Serial.println("TCA9554 0x20 did not latch. Battery drops when PWR is released.");
}
#endif

LyricScript gScript;
bool gBootRaw = false;
bool gBootStable = false;
uint32_t gBootChange = 0;
bool gBootReady = false;

bool boot_down() {
  return digitalRead(kBootPin) == LOW;
}

void poll_boot(uint32_t now) {
  if (!gBootReady) {
    gBootRaw = boot_down();
    gBootStable = gBootRaw;
    gBootChange = now;
    gBootReady = true;
    return;
  }
  const bool raw = boot_down();
  if (raw != gBootRaw) {
    gBootRaw = raw;
    gBootChange = now;
  }
  if ((now - gBootChange) >= 25 && raw != gBootStable) {
    gBootStable = raw;
    if (gBootStable) {
      player_restart(now);
      Serial.println("BOOT: playback restarted.");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Lyric player boot");

  gpio_reset_pin(static_cast<gpio_num_t>(kBootPin));
  pinMode(kBootPin, INPUT_PULLUP);

  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(100000);
#if BOARD_HAS_RAILS
  hold_battery_power();
#endif
#if BOARD_HAS_EINK
  eink_begin();
#endif
  Wire.setClock(400000);

  while (!oled_begin()) {
    oled_scan_i2c();
    Serial.printf("No OLED at 0x3C or 0x3D. VCC -> 3V3, GND -> GND, SDA GPIO%d, SCL GPIO%d.\n",
                  kOledSda, kOledScl);
    delay(1000);
  }

  lyrics_load(&gScript);
  player_begin(&gScript);
  player_restart(millis());
  Serial.println("Landscape 128x64. BOOT restarts playback.");
}

void loop() {
  static uint32_t next_ms = 0;
  const uint32_t now = millis();
  poll_boot(now);
  player_render(now);

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
