#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_system.h>
#include <math.h>

#include "eink_warn.h"

// Header I2C on the ESP32-C6-ePaper-1.54. OLED and ADXL345 share this bus.
static constexpr int kSdaPin = 18;
static constexpr int kSclPin = 8;
static constexpr int kBootPin = 9;
static constexpr int kPwrPin = 2;
static constexpr uint32_t kRestartHoldMs = 800;

static constexpr uint8_t kAdxlAddressLow = 0x53;   // SDO / ALT tied to GND
static constexpr uint8_t kAdxlAddressHigh = 0x1D;  // SDO / ALT tied to 3V3
static constexpr uint8_t kAdxlDeviceId = 0xE5;

// Onboard TCA9554. EXIO5 is the battery soft-power latch.
static constexpr uint8_t kTca9554Address = 0x20;
static constexpr uint8_t kTcaOutputReg = 0x01;
static constexpr uint8_t kTcaConfigReg = 0x03;
static constexpr uint8_t kBatteryHoldBit = 1 << 5;
static constexpr uint8_t kEpdPowerBit = 1 << 0;

static constexpr uint8_t kRegDeviceId = 0x00;
static constexpr uint8_t kRegBwRate = 0x2C;
static constexpr uint8_t kRegPowerCtl = 0x2D;
static constexpr uint8_t kRegDataFormat = 0x31;
static constexpr uint8_t kRegDataX0 = 0x32;

// Full-resolution mode is 256 counts per g at every range. ±4 g leaves
// headroom when the module is shaken, without clipping a normal tilt.
static constexpr uint8_t kDataFormat4gFullRes = 0x09;
static constexpr uint8_t kBwRate100Hz = 0x0A;
static constexpr uint8_t kPowerMeasure = 0x08;
static constexpr float kCountsPerG = 256.0f;

// Flip a sign if that axis tilts opposite the module.
static constexpr float kRollSign = 1.0f;
static constexpr float kPitchSign = 1.0f;

// Cylinder looking down +Z from inside the tube. The nearest ring is larger
// than the glass, so the grid runs off every edge. Pitch steers left/right
// and roll steers up/down, matching the cube's rotation. The clamps keep a
// hard tip from turning the tube sideways.
static constexpr int kRings = 6;
static constexpr int kSegs = 16;
static constexpr float kRadius = 1.22f;
static constexpr float kZNear = 0.92f;
static constexpr float kZFar = 7.0f;
static constexpr float kFocal = 50.0f;
static constexpr float kMaxPitch = 0.58f;
static constexpr float kMaxRoll = 0.38f;
// Ring slots per second. A ring travels from the far end to the glass in kRings / this.
static constexpr float kRingsPerSec = 2.0f;

#if defined(OLED_SH1106)
static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#endif

static uint8_t gAdxlAddress = 0;
static char gAdxlStatus[32] = "ADXL345 not found";

struct Vec3 {
  float x;
  float y;
  float z;
};

static bool writeDevReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool readDevReg(uint8_t addr, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<uint8_t>(addr), static_cast<uint8_t>(1), static_cast<uint8_t>(true)) != 1) {
    return false;
  }
  value = static_cast<uint8_t>(Wire.read());
  return true;
}

// Vendor shutdown drives EXIO5 low and leaves it there. A read-modify-write
// can put the latch back high if the read fails, so these writes are absolute.
static bool writeRails(bool batteryOn, bool epdOn) {
  uint8_t output = 0;
  if (batteryOn) {
    output |= kBatteryHoldBit;
  }
  if (epdOn) {
    output |= kEpdPowerBit;
  }
  const uint8_t config = static_cast<uint8_t>(~(kBatteryHoldBit | kEpdPowerBit));
  return writeDevReg(kTca9554Address, kTcaOutputReg, output) &&
         writeDevReg(kTca9554Address, kTcaConfigReg, config);
}

// Pressing PWR powers the board only while the button is down. EXIO5 has to
// stay high after that, or the rail drops as soon as the button is released.
static void holdBatteryPower() {
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (writeRails(true, true)) {
      Serial.println("Battery hold on (TCA9554 EXIO5).");
      return;
    }
    delay(30);
  }
  Serial.println("TCA9554 0x20 did not latch EXIO5. Battery drops when the button is released.");
}

// Drop EXIO5 only after "OFF" is on both panels. Deep sleep is not used:
// with USB plugged in it resets the chip, setup runs again, and the latch
// turns straight back on.
static void powerOff() {
  Serial.println("PWR off");
  Serial.flush();

  display.clearBuffer();
  display.setFont(u8g2_font_helvB12_tr);
  display.drawStr(46, 36, "OFF");
  display.sendBuffer();

  for (int attempt = 0; attempt < 5; ++attempt) {
    if (writeRails(false, true)) {
      break;
    }
    delay(20);
  }

  bool sawRelease = false;
  while (true) {
    const bool pressed = digitalRead(kPwrPin) == LOW;
    if (!pressed) {
      sawRelease = true;
    } else if (sawRelease) {
      esp_restart();
    }
    delay(20);
  }
}

static void pollPowerButton() {
  static bool seenRelease = false;
  static bool down = false;
  static uint32_t downAt = 0;

  const bool pressed = digitalRead(kPwrPin) == LOW;
  // The press that turned the board on is still down. Wait it out.
  if (!seenRelease) {
    if (!pressed) {
      seenRelease = true;
    }
    return;
  }
  if (pressed) {
    if (!down) {
      down = true;
      downAt = millis();
      Serial.println("PWR down");
    } else if (millis() - downAt >= 800) {
      // Still held. This is the vendor gesture: long-press, then release.
      powerOff();
    }
    return;
  }
  if (down && millis() - downAt >= 40) {
    powerOff();
  }
  down = false;
}

static bool devicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void scanI2c() {
  Serial.println("I2C scan (onboard chips show up too):");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (!devicePresent(addr)) {
      continue;
    }
    Serial.printf("  0x%02X\n", addr);
    found++;
  }
  if (found == 0) {
    Serial.println("  (none)");
    snprintf(gAdxlStatus, sizeof(gAdxlStatus), "I2C bus empty");
  } else {
    snprintf(gAdxlStatus, sizeof(gAdxlStatus), "ADXL not on I2C");
  }
}

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(gAdxlAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static int readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  // Full stop between the register pointer and the read. Some ADXL345
  // boards never answer a repeated start, so the ID read looks missing.
  // Bit 7 of the register address is the multi-byte flag. Without it the
  // chip repeats the first data byte and a 6-byte sample never completes.
  if (len > 1) {
    reg |= 0x80;
  }
  Wire.setClock(100000);
  Wire.beginTransmission(gAdxlAddress);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) {
    return -1;
  }
  const uint8_t got = Wire.requestFrom(
      static_cast<uint8_t>(gAdxlAddress), static_cast<uint8_t>(len), static_cast<uint8_t>(true));
  size_t i = 0;
  while (i < got && i < len && Wire.available()) {
    buf[i++] = static_cast<uint8_t>(Wire.read());
  }
  Wire.setClock(400000);
  return static_cast<int>(i);
}

static uint8_t waitForOled() {
  for (;;) {
    if (devicePresent(0x3C)) {
      return 0x3C;
    }
    if (devicePresent(0x3D)) {
      return 0x3D;
    }
    scanI2c();
    Serial.println("No OLED at 0x3C or 0x3D. VCC -> 3V3, GND -> GND, SDA -> SDA, SCL -> SCL.");
    pollPowerButton();
    delay(1000);
  }
}

static bool adxlProbe(uint8_t addr) {
  gAdxlAddress = addr;
  if (!devicePresent(addr)) {
    return false;
  }
  uint8_t id = 0;
  if (readRegs(kRegDeviceId, &id, 1) != 1) {
    Serial.printf("0x%02X ACKed but DEVID read failed\n", addr);
    return false;
  }
  Serial.printf("0x%02X DEVID 0x%02X\n", addr, id);
  return id == kAdxlDeviceId;
}

static bool adxlBegin() {
  if (!adxlProbe(kAdxlAddressLow) && !adxlProbe(kAdxlAddressHigh)) {
    gAdxlAddress = 0;
    return false;
  }
  // Standby, then measure. Data registers stay at zero until measure is set.
  if (!writeReg(kRegPowerCtl, 0x00)) {
    return false;
  }
  if (!writeReg(kRegDataFormat, kDataFormat4gFullRes)) {
    return false;
  }
  if (!writeReg(kRegBwRate, kBwRate100Hz)) {
    return false;
  }
  if (!writeReg(kRegPowerCtl, kPowerMeasure)) {
    return false;
  }
  delay(2);
  return true;
}

// Hold BOOT, then release. Restarting while the pin is still low would
// strap the chip into the ROM download mode instead of this app.
static void pollBootRestart() {
  static bool down = false;
  static bool armed = false;
  static uint32_t downAt = 0;

  const bool pressed = digitalRead(kBootPin) == LOW;
  if (pressed) {
    if (!down) {
      down = true;
      armed = false;
      downAt = millis();
    } else if (!armed && millis() - downAt >= kRestartHoldMs) {
      armed = true;
    }
    return;
  }
  if (down && armed) {
    Serial.println("BOOT held, restarting");
    Serial.flush();
    esp_restart();
  }
  down = false;
  armed = false;
}

static bool adxlReadG(float &x, float &y, float &z) {
  uint8_t raw[6];
  if (readRegs(kRegDataX0, raw, sizeof(raw)) != static_cast<int>(sizeof(raw))) {
    return false;
  }
  const int16_t rx = static_cast<int16_t>(raw[0] | (raw[1] << 8));
  const int16_t ry = static_cast<int16_t>(raw[2] | (raw[3] << 8));
  const int16_t rz = static_cast<int16_t>(raw[4] | (raw[5] << 8));
  x = rx / kCountsPerG;
  y = ry / kCountsPerG;
  z = rz / kCountsPerG;
  return true;
}

// Pitch tips the nose, roll drops a wing. Both come from the gravity vector.
static void tiltFromGravity(float ax, float ay, float az, float &pitch, float &roll) {
  roll = atan2f(ay, az) * kRollSign;
  const float horizon = sqrtf(ay * ay + az * az);
  pitch = atan2f(-ax, horizon) * kPitchSign;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static Vec3 rotateTilt(Vec3 v, float pitch, float roll) {
  const float cp = cosf(pitch);
  const float sp = sinf(pitch);
  const float cr = cosf(roll);
  const float sr = sinf(roll);

  const float x1 = v.x * cp + v.z * sp;
  const float z1 = -v.x * sp + v.z * cp;
  const float y2 = v.y * cr - z1 * sr;
  const float z2 = v.y * sr + z1 * cr;
  return {x1, y2, z2};
}

// U8g2 takes unsigned coordinates, so a negative end wraps. Clip in signed
// space first, then hand the visible piece to the driver.
static int outCode(int x, int y) {
  const int w = display.getDisplayWidth();
  const int h = display.getDisplayHeight();
  int code = 0;
  if (x < 0) {
    code |= 1;
  } else if (x >= w) {
    code |= 2;
  }
  if (y < 0) {
    code |= 4;
  } else if (y >= h) {
    code |= 8;
  }
  return code;
}

static bool clipLine(int &x0, int &y0, int &x1, int &y1) {
  const int w = display.getDisplayWidth();
  const int h = display.getDisplayHeight();
  for (int guard = 0; guard < 8; ++guard) {
    const int c0 = outCode(x0, y0);
    const int c1 = outCode(x1, y1);
    if ((c0 | c1) == 0) {
      return true;
    }
    if ((c0 & c1) != 0) {
      return false;
    }
    const int c = c0 != 0 ? c0 : c1;
    int x = x0;
    int y = y0;
    if ((c & 8) != 0 && y1 != y0) {
      x = x0 + (x1 - x0) * (h - 1 - y0) / (y1 - y0);
      y = h - 1;
    } else if ((c & 4) != 0 && y1 != y0) {
      x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0);
      y = 0;
    } else if ((c & 2) != 0 && x1 != x0) {
      y = y0 + (y1 - y0) * (w - 1 - x0) / (x1 - x0);
      x = w - 1;
    } else if (x1 != x0) {
      y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
      x = 0;
    }
    if (x < 0) {
      x = 0;
    } else if (x >= w) {
      x = w - 1;
    }
    if (y < 0) {
      y = 0;
    } else if (y >= h) {
      y = h - 1;
    }
    if (c == c0) {
      x0 = x;
      y0 = y;
    } else {
      x1 = x;
      y1 = y;
    }
  }
  return outCode(x0, y0) == 0 && outCode(x1, y1) == 0;
}

static void drawClipped(int x0, int y0, int x1, int y1) {
  if (!clipLine(x0, y0, x1, y1)) {
    return;
  }
  display.drawLine(x0, y0, x1, y1);
}

static void drawTunnel(float pitch, float roll, float fly) {
  const float pit = clampf(pitch, -kMaxPitch, kMaxPitch);
  const float rol = clampf(roll, -kMaxRoll, kMaxRoll);
  const int cx = display.getDisplayWidth() / 2;
  const int cy = display.getDisplayHeight() / 2;
  const float zSpan = logf(kZFar / kZNear);
  const float step = expf(zSpan / static_cast<float>(kRings));

  static float cosA[kSegs];
  static float sinA[kSegs];
  static bool anglesReady = false;
  if (!anglesReady) {
    for (int seg = 0; seg < kSegs; ++seg) {
      const float a = (static_cast<float>(seg) * 6.2831853f) / kSegs;
      cosA[seg] = cosf(a);
      sinA[seg] = sinf(a);
    }
    anglesReady = true;
  }

  int sx[kRings][kSegs];
  int sy[kRings][kSegs];
  bool ok[kRings][kSegs];
  float zs[kRings];

  for (int ring = 0; ring < kRings; ++ring) {
    // fly advances and each ring's slot drops, so depth shrinks toward the camera.
    // Past the near plane the slot wraps to the far end.
    float slot = static_cast<float>(ring) - fly;
    slot -= floorf(slot / static_cast<float>(kRings)) * static_cast<float>(kRings);
    if (slot < 0.0f) {
      slot += static_cast<float>(kRings);
    }
    const float u = slot / static_cast<float>(kRings);
    const float z = kZNear * expf(zSpan * u);
    zs[ring] = z;
    for (int seg = 0; seg < kSegs; ++seg) {
      const Vec3 view = rotateTilt({kRadius * cosA[seg], kRadius * sinA[seg], z}, pit, rol);
      if (view.z < 0.2f) {
        ok[ring][seg] = false;
        continue;
      }
      const float scale = kFocal / view.z;
      sx[ring][seg] = cx + static_cast<int>(lroundf(view.x * scale));
      sy[ring][seg] = cy - static_cast<int>(lroundf(view.y * scale));
      ok[ring][seg] = true;
    }
  }

  for (int ring = 0; ring < kRings; ++ring) {
    for (int seg = 0; seg < kSegs; ++seg) {
      const int next = (seg + 1) % kSegs;
      if (ok[ring][seg] && ok[ring][next]) {
        drawClipped(sx[ring][seg], sy[ring][seg], sx[ring][next], sy[ring][next]);
      }
    }
  }

  // Spokes join rings one depth step apart. The wrap from near back to far
  // is a much larger gap, so that pair is left unconnected.
  for (int a = 0; a < kRings; ++a) {
    for (int b = a + 1; b < kRings; ++b) {
      const float ratio = zs[a] > zs[b] ? zs[a] / zs[b] : zs[b] / zs[a];
      if (ratio > step * 1.35f) {
        continue;
      }
      for (int seg = 0; seg < kSegs; ++seg) {
        if (ok[a][seg] && ok[b][seg]) {
          drawClipped(sx[a][seg], sy[a][seg], sx[b][seg], sy[b][seg]);
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(kBootPin, INPUT_PULLUP);
  gpio_reset_pin(static_cast<gpio_num_t>(kPwrPin));
  pinMode(kPwrPin, INPUT_PULLUP);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);
  holdBatteryPower();
  eink_begin();
  Wire.setClock(400000);

  const uint8_t oledAddr = waitForOled();
  Serial.printf("OLED at 0x%02X\n", oledAddr);
  display.setI2CAddress(static_cast<uint8_t>(oledAddr << 1));
  display.begin();
  Wire.setClock(400000);

  if (adxlBegin()) {
    Serial.printf("ADXL345 at 0x%02X\n", gAdxlAddress);
  } else {
    scanI2c();
    Serial.println("No ADXL345 at 0x53 or 0x1D.");
    Serial.println("CS -> 3V3 (I2C mode), SDO -> GND for 0x53, SDA -> SDA, SCL -> SCL, VCC -> 3V3.");
  }
}

void loop() {
  static float gx = 0.0f;
  static float gy = 0.0f;
  static float gz = 1.0f;
  static float pitch = 0.0f;
  static float roll = 0.0f;
  static float fly = 0.0f;
  static uint32_t lastFrameMs = 0;
  static uint32_t lastLogMs = 0;
  static bool live = false;

  pollBootRestart();
  pollPowerButton();

  const uint32_t now = millis();
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (gAdxlAddress != 0 && adxlReadG(ax, ay, az)) {
    live = true;
    constexpr float kSmooth = 0.28f;
    gx += (ax - gx) * kSmooth;
    gy += (ay - gy) * kSmooth;
    gz += (az - gz) * kSmooth;

    const float mag = sqrtf(gx * gx + gy * gy + gz * gz);
    // Keep the last pose in free-fall, where tilt is undefined.
    if (mag > 0.25f) {
      tiltFromGravity(gx, gy, gz, pitch, roll);
    }
  } else if (gAdxlAddress == 0 && now - lastLogMs >= 2000) {
    lastLogMs = now;
    if (adxlBegin()) {
      Serial.printf("ADXL345 at 0x%02X\n", gAdxlAddress);
    } else {
      Serial.println(gAdxlStatus);
    }
  } else {
    live = gAdxlAddress != 0;
  }

  if (lastFrameMs != 0) {
    fly += (now - lastFrameMs) * (kRingsPerSec / 1000.0f);
    while (fly >= static_cast<float>(kRings)) {
      fly -= static_cast<float>(kRings);
    }
  }
  lastFrameMs = now;

  display.clearBuffer();
  drawTunnel(pitch, roll, fly);
  display.sendBuffer();

  if (live && now - lastLogMs >= 500) {
    lastLogMs = now;
    Serial.printf("g  x=%+.2f  y=%+.2f  z=%+.2f  raw %+.2f %+.2f %+.2f\n", gx, gy, gz, ax, ay, az);
  }

  delay(33);
}
