#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_system.h>
#include <math.h>

#include "eink_warn.h"
#include "engine_audio.h"

// Header I2C on the ESP32-C6-ePaper-1.54. OLED and ADXL345 share this bus.
static constexpr int kSdaPin = 18;
static constexpr int kSclPin = 8;
static constexpr int kBootPin = 9;
static constexpr int kPwrPin = 2;
static constexpr int kBoostPin = 3;
static constexpr uint32_t kRestartHoldMs = 800;
static constexpr uint32_t kBoostRampMs = 500;
static constexpr float kBoostPeak = 18.0f;

static constexpr uint8_t kAdxlAddressLow = 0x53;   // SDO / ALT tied to GND
static constexpr uint8_t kAdxlAddressHigh = 0x1D;  // SDO / ALT tied to 3V3
static constexpr uint8_t kAdxlDeviceId = 0xE5;

// Onboard TCA9554. EXIO5 is the battery soft-power latch.
static constexpr uint8_t kTca9554Address = 0x20;
static constexpr uint8_t kTcaOutputReg = 0x01;
static constexpr uint8_t kTcaConfigReg = 0x03;
static constexpr uint8_t kBatteryHoldBit = 1 << 5;
static constexpr uint8_t kEpdPowerBit = 1 << 0;
static constexpr uint8_t kAudioPowerBit = 1 << 1;
static constexpr uint8_t kAmpBit = 1 << 3;
static constexpr uint8_t kRailMask = kBatteryHoldBit | kEpdPowerBit | kAudioPowerBit | kAmpBit;

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

#if defined(OLED_SH1106)
static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#endif

static uint8_t gAdxlAddress = 0;
static char gAdxlStatus[32] = "ADXL345 not found";

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
static bool writeRails(bool batteryOn, bool epdOn, bool audioOn) {
  uint8_t output = 0;
  if (batteryOn) {
    output |= kBatteryHoldBit;
  }
  if (epdOn) {
    output |= kEpdPowerBit;
  }
  if (audioOn) {
    output |= kAudioPowerBit | kAmpBit;
  }
  const uint8_t config = static_cast<uint8_t>(~kRailMask);
  return writeDevReg(kTca9554Address, kTcaOutputReg, output) &&
         writeDevReg(kTca9554Address, kTcaConfigReg, config);
}

// Pressing PWR powers the board only while the button is down. EXIO5 has to
// stay high after that, or the rail drops as soon as the button is released.
static void holdBatteryPower() {
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (writeRails(true, true, true)) {
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
    if (writeRails(false, true, false)) {
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

// Pitch tips the nose, roll drops a wing. On this module Y runs along the nose
// and X along the wings, so forward/back must not be read as a bank.
static void tiltFromGravity(float ax, float ay, float az, float &pitch, float &roll) {
  roll = atan2f(ax, az) * kRollSign;
  const float horizon = sqrtf(ax * ax + az * az);
  pitch = atan2f(-ay, horizon) * kPitchSign;
}

// Infinite ground. A fixed bitmap wrapped every few seconds, so this is a
// seeded noise field instead: new coastlines, no repeating tile.
static uint32_t gMapSeed = 0xA5F17u;

static void seedTerrain() {
  gMapSeed = esp_random();
  if (gMapSeed == 0) {
    gMapSeed = 0xA5F17u;
  }
}

static int terrainHash(int x, int y) {
  uint32_t h = gMapSeed;
  h ^= static_cast<uint32_t>(x) * 0x9E3779B1u;
  h ^= static_cast<uint32_t>(y) * 0x85EBCA77u;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  return static_cast<int>(h & 255u);
}

static int floorDiv(int value, int cell) {
  int q = value / cell;
  if (value < 0 && value % cell != 0) {
    --q;
  }
  return q;
}

static int valueNoise(int mx, int my, int cell) {
  const int x0 = floorDiv(mx, cell);
  const int y0 = floorDiv(my, cell);
  const int fx = mx - x0 * cell;
  const int fy = my - y0 * cell;
  const int n00 = terrainHash(x0, y0);
  const int n10 = terrainHash(x0 + 1, y0);
  const int n01 = terrainHash(x0, y0 + 1);
  const int n11 = terrainHash(x0 + 1, y0 + 1);
  const int nx0 = n00 + (n10 - n00) * fx / cell;
  const int nx1 = n01 + (n11 - n01) * fx / cell;
  return nx0 + (nx1 - nx0) * fy / cell;
}

static bool worldLand(int mx, int my) {
  const int coarse = valueNoise(mx, my, 8);
  const int fine = valueNoise(mx, my, 3);
  return (coarse * 3 + fine) / 4 > 152;
}

struct Star {
  int8_t along;
  int8_t up;
  uint8_t kind;
};

// along spreads across the sky, up lifts off the horizon. Both are in the
// same pixel scale so stars travel left, right, and up through the top half.
static const Star kStars[] = {
    {-18, 2, 0}, {-14, 7, 1}, {-16, 4, 2}, {-10, 9, 0}, {-8, 3, 1}, {-5, 6, 2},
    {-2, 2, 0},  {0, 8, 1},   {3, 4, 2},   {6, 9, 0},   {9, 3, 1},   {12, 6, 0},
    {16, 2, 2},  {18, 8, 1},  {-12, 5, 0}, {7, 7, 2},   {-7, 8, 1},  {14, 4, 0},
    {-20, 6, 2}, {4, 5, 0},
};

static void groundSpan(int y, float cx, float cy0, float nx, float ny, int width, int &x0, int &x1) {
  x0 = 0;
  x1 = 0;
  if (fabsf(nx) < 0.05f) {
    if ((static_cast<float>(y) - cy0) * ny < 0.0f) {
      x1 = width;
    }
    return;
  }
  const float xSplit = cx - (static_cast<float>(y) - cy0) * ny / nx;
  if (nx > 0.0f) {
    x1 = static_cast<int>(floorf(xSplit));
    if (x1 < 0) {
      x1 = 0;
    }
    if (x1 > width) {
      x1 = width;
    }
    return;
  }
  x0 = static_cast<int>(ceilf(xSplit));
  if (x0 < 0) {
    x0 = 0;
  }
  if (x0 > width) {
    x0 = width;
  }
  x1 = width;
}

static bool inSky(int x, int y, float cx, float cy0, float nx, float ny, int width, int height) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return false;
  }
  return (static_cast<float>(x) - cx) * nx + (static_cast<float>(y) - cy0) * ny >= 0.0f;
}

static void tri(int cx, int cy, int x0, int y0, int x1, int y1, int x2, int y2) {
  display.drawTriangle(cx + x0, cy + y0, cx + x1, cy + y1, cx + x2, cy + y2);
}

// Rear view of a light aircraft. Drawn twice: a white coat, then a black
// body one pixel inside, so the silhouette stays visible on sky and ground.
static void drawAircraft(int cx, int cy, bool body) {
  if (body) {
    tri(cx, cy, -3, 0, -30, 3, -28, 6);
    tri(cx, cy, -3, 0, -28, 6, -3, 4);
    tri(cx, cy, 3, 0, 30, 3, 28, 6);
    tri(cx, cy, 3, 0, 28, 6, 3, 4);
    tri(cx, cy, -2, -7, -12, -9, -11, -6);
    tri(cx, cy, -2, -7, -11, -6, -2, -5);
    tri(cx, cy, 2, -7, 12, -9, 11, -6);
    tri(cx, cy, 2, -7, 11, -6, 2, -5);
    tri(cx, cy, -1, -7, 0, -16, 1, -7);
    display.drawBox(cx - 2, cy - 7, 5, 15);
    return;
  }
  tri(cx, cy, -5, -2, -36, 1, -33, 8);
  tri(cx, cy, -5, -2, -33, 8, -5, 5);
  tri(cx, cy, 5, -2, 36, 1, 33, 8);
  tri(cx, cy, 5, -2, 33, 8, 5, 5);
  tri(cx, cy, -4, -8, -16, -11, -14, -5);
  tri(cx, cy, -4, -8, -14, -5, -4, -4);
  tri(cx, cy, 4, -8, 16, -11, 14, -5);
  tri(cx, cy, 4, -8, 14, -5, 4, -4);
  tri(cx, cy, -3, -8, 0, -20, 3, -8);
  display.drawBox(cx - 4, cy - 8, 9, 18);
}

// GP3 is active-high. Speed climbs while the button is held and falls on release.
static float boostScale(uint32_t now) {
  static bool primed = false;
  static float scale = 1.0f;
  static uint32_t lastMs = 0;

  const bool held = digitalRead(kBoostPin) == HIGH;
  if (!primed) {
    primed = true;
    lastMs = now;
    return 1.0f;
  }
  uint32_t dt = now - lastMs;
  lastMs = now;
  if (dt > 100) {
    dt = 100;
  }
  const float step =
      (kBoostPeak - 1.0f) * (static_cast<float>(dt) / static_cast<float>(kBoostRampMs));
  if (held) {
    scale += step;
    if (scale > kBoostPeak) {
      scale = kBoostPeak;
    }
  } else {
    scale -= step;
    if (scale < 1.0f) {
      scale = 1.0f;
    }
  }
  return scale;
}

static void drawHorizon(float pitch, float roll, float flight, float starTime, float warp) {
  const int width = display.getDisplayWidth();
  const int height = display.getDisplayHeight();
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  // About one pixel per degree, enough that a normal tilt still keeps the line on glass.
  constexpr float kPxPerRad = 52.0f;
  const float cy0 = cy + pitch * kPxPerRad;

  const float dx = cosf(roll);
  const float dy = -sinf(roll);
  const float nx = dy;
  const float ny = -dx;

  // Mild perspective so the bitmap stays a world map. A hard 1/v divide
  // squeezed every continent into a stripe at the horizon.
  display.setDrawColor(1);
  for (int y = 0; y < height; ++y) {
    int x0 = 0;
    int x1 = 0;
    groundSpan(y, cx, cy0, nx, ny, width, x0, x1);
    if (x1 <= x0) {
      continue;
    }
    display.setDrawColor(1);
    display.drawHLine(x0, y, x1 - x0);
    display.setDrawColor(0);
    for (int x = x0; x < x1; ++x) {
      const float u = (static_cast<float>(x) - cx) * dx + (static_cast<float>(y) - cy0) * dy;
      const float v = -((static_cast<float>(x) - cx) * nx + (static_cast<float>(y) - cy0) * ny);
      if (v < 0.5f) {
        continue;
      }
      const float depth = 28.0f / (v + 14.0f);
      const int mx = static_cast<int>(floorf(u * depth * 0.22f));
      const int my = static_cast<int>(floorf(flight + depth * 10.0f));
      if (worldLand(mx, my)) {
        display.drawPixel(x, y);
      }
    }
  }

  display.setDrawColor(1);
  constexpr float kFocal = 26.0f;
  constexpr float kStarNear = 1.15f;
  constexpr float kStarSpan = 7.4f;
  for (int i = 0; i < static_cast<int>(sizeof(kStars) / sizeof(kStars[0])); ++i) {
    const Star &star = kStars[i];
    if (warp < 2.0f && star.kind == 2 &&
        ((millis() / 180 + static_cast<uint32_t>(i)) % 5) == 0) {
      continue;
    }
    const float speed = 1.1f * (0.75f + static_cast<float>(i % 4) * 0.18f);
    float depth = kStarSpan - fmodf(starTime * speed + static_cast<float>(i) * 0.85f, kStarSpan);
    if (depth < 0.0f) {
      depth += kStarSpan;
    }
    depth += kStarNear;
    const float screenAlong = static_cast<float>(star.along) * kFocal / depth;
    const float screenUp = static_cast<float>(star.up) * kFocal / depth;
    const int sx = static_cast<int>(lroundf(cx + screenAlong * dx + screenUp * nx));
    const int sy = static_cast<int>(lroundf(cy0 + screenAlong * dy + screenUp * ny));
    if (!inSky(sx, sy, cx, cy0, nx, ny, width, height)) {
      continue;
    }
    const float streak = (warp - 1.0f) * 1.05f;
    const float radius = sqrtf(screenAlong * screenAlong + screenUp * screenUp);
    if (streak >= 1.5f && radius > 1.0f) {
      float len = streak;
      if (len > radius * 0.85f) {
        len = radius * 0.85f;
      }
      const float ux = screenAlong / radius;
      const float uy = screenUp / radius;
      const int x0 = static_cast<int>(lroundf(sx - (ux * dx + uy * nx) * len));
      const int y0 = static_cast<int>(lroundf(sy - (ux * dy + uy * ny) * len));
      display.drawLine(x0, y0, sx, sy);
      continue;
    }
    display.drawPixel(sx, sy);
    if (star.kind == 1 || depth < 2.2f) {
      if (inSky(sx - 1, sy, cx, cy0, nx, ny, width, height)) {
        display.drawPixel(sx - 1, sy);
      }
      if (inSky(sx + 1, sy, cx, cy0, nx, ny, width, height)) {
        display.drawPixel(sx + 1, sy);
      }
      if (inSky(sx, sy - 1, cx, cy0, nx, ny, width, height)) {
        display.drawPixel(sx, sy - 1);
      }
      if (inSky(sx, sy + 1, cx, cy0, nx, ny, width, height)) {
        display.drawPixel(sx, sy + 1);
      }
    }
  }

  // Bright limb so the planet edge stays solid where land meets space.
  display.setDrawColor(1);
  for (int t = -96; t <= 96; ++t) {
    const float ft = static_cast<float>(t);
    const int hx = static_cast<int>(lroundf(cx + ft * dx));
    const int hy = static_cast<int>(lroundf(cy0 + ft * dy));
    if (hx >= 0 && hy >= 0 && hx < width && hy < height) {
      display.drawPixel(hx, hy);
    }
    const int ax = static_cast<int>(lroundf(cx + ft * dx + nx));
    const int ay = static_cast<int>(lroundf(cy0 + ft * dy + ny));
    if (ax >= 0 && ay >= 0 && ax < width && ay < height) {
      display.drawPixel(ax, ay);
    }
  }

  const int planeX = width / 2;
  const int planeY = height / 2;
  display.setDrawColor(1);
  drawAircraft(planeX, planeY, false);
  display.setDrawColor(0);
  drawAircraft(planeX, planeY, true);
  display.setDrawColor(1);
}

void setup() {
  Serial.begin(115200);

  pinMode(kBootPin, INPUT_PULLUP);
  gpio_reset_pin(static_cast<gpio_num_t>(kPwrPin));
  pinMode(kPwrPin, INPUT_PULLUP);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);
  holdBatteryPower();
  seedTerrain();
  engineAudioBegin();
  eink_begin();
  Wire.setClock(400000);

  const uint8_t oledAddr = waitForOled();
  Serial.printf("OLED at 0x%02X\n", oledAddr);
  display.setI2CAddress(static_cast<uint8_t>(oledAddr << 1));
  display.begin();
  Wire.setClock(400000);

  pinMode(kBoostPin, INPUT_PULLDOWN);

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
  static uint32_t lastLogMs = 0;
  static uint32_t lastFrameMs = 0;
  static float flight = 0.0f;
  static float starTime = 0.0f;
  static bool live = false;

  pollBootRestart();
  pollPowerButton();

  const uint32_t now = millis();
  const float warp = boostScale(now);
  uint32_t dtMs = lastFrameMs == 0 ? 0 : now - lastFrameMs;
  lastFrameMs = now;
  if (dtMs > 100) {
    dtMs = 100;
  }
  const float dt = static_cast<float>(dtMs) / 1000.0f;
  flight += 2.2f * warp * dt;
  starTime += warp * dt;
  engineAudioSetWarp(warp);
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

  display.clearBuffer();
  drawHorizon(pitch, roll, flight, starTime, warp);
  display.sendBuffer();

  if (live && now - lastLogMs >= 500) {
    lastLogMs = now;
    Serial.printf("g  x=%+.2f  y=%+.2f  z=%+.2f  raw %+.2f %+.2f %+.2f\n", gx, gy, gz, ax, ay, az);
  }

  delay(33);
}
