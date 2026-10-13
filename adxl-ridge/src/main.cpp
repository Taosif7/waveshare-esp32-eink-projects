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
static constexpr int kSelectPin = 3;
static constexpr uint32_t kRestartHoldMs = 800;
static constexpr uint32_t kBoostRampMs = 500;

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

static constexpr uint8_t kDataFormat4gFullRes = 0x09;
static constexpr uint8_t kBwRate100Hz = 0x0A;
static constexpr uint8_t kPowerMeasure = 0x08;
static constexpr float kCountsPerG = 256.0f;

// Flip a sign if that axis tilts opposite the module.
static constexpr float kRollSign = 1.0f;
static constexpr float kPitchSign = 1.0f;

static constexpr int kShipX = 64;
static constexpr int kShipY = 46;
static constexpr float kHorizon = 22.0f;
static constexpr float kFocal = 40.0f;
static constexpr float kVert = 32.0f;
static constexpr float kMaxBank = 0.48f;
static constexpr float kMaxPitch = 0.50f;
static constexpr float kPitchPx = 16.0f;
static constexpr float kZNear = 6.0f;
static constexpr float kZFar = 38.0f;
static constexpr int kSlices = 9;
static constexpr int kCols = 19;
// Above 1 packs columns toward the middle of the glass. 1 is even spacing.
static constexpr float kCenterDensity = 1.45f;
static constexpr float kCruise = 7.5f;
static constexpr float kBoostPeak = 3.2f;
static constexpr float kClearance = 8.0f;
static constexpr float kClimb = 46.0f;
static constexpr float kTurn = 0.95f;
static constexpr uint32_t kCrashBurstMs = 900;

#if defined(OLED_SH1106)
static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#endif

static uint8_t gAdxlAddress = 0;
static char gAdxlStatus[32] = "ADXL345 not found";

static uint32_t gSeed = 0xA5F17u;
static float gCamX = 0.0f;
static float gCamZ = 0.0f;
static float gCamH = 14.0f;
static float gHeading = 0.0f;
static float gTravel = 0.0f;
static bool gMenu = true;
static int gItem = 0;
static bool gCrashed = false;
static uint32_t gCrashAt = 0;
static float gStarTime = 0.0f;

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

static void powerOff() {
  Serial.println("PWR off");
  Serial.flush();

  display.clearBuffer();
  display.setFont(u8g2_font_helvB12_tr);
  display.setDrawColor(1);
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

// Level is 45% of the way from flat toward upright, same pose as the horizon.
static constexpr float kLevelTilt = 0.45f * 1.5707963f;

static void tiltFromGravity(float ax, float ay, float az, float &pitch, float &roll) {
  const float c = cosf(kLevelTilt);
  const float s = sinf(kLevelTilt);
  const float levelY = ay * c + az * s;
  const float levelZ = -ay * s + az * c;
  roll = atan2f(ax, levelZ) * kRollSign;
  const float horizon = sqrtf(ax * ax + levelZ * levelZ);
  pitch = atan2f(-levelY, horizon) * kPitchSign;
}

// Hold BOOT, then release, restarts. A short release is the menu key.
// Restarting while the pin is still low would strap the ROM download mode.
enum class BootTap { None, Short };

static BootTap pollBoot() {
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
    return BootTap::None;
  }
  if (!down) {
    return BootTap::None;
  }
  down = false;
  if (armed) {
    Serial.println("BOOT held, restarting");
    Serial.flush();
    esp_restart();
  }
  armed = false;
  if (millis() - downAt < 40) {
    return BootTap::None;
  }
  return BootTap::Short;
}

static float clampf(float value, float lo, float hi) {
  if (value < lo) {
    return lo;
  }
  if (value > hi) {
    return hi;
  }
  return value;
}

static float hash01(int x, int z) {
  uint32_t h = gSeed;
  h ^= static_cast<uint32_t>(x) * 0x9E3779B1u;
  h ^= static_cast<uint32_t>(z) * 0x85EBCA77u;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  return static_cast<float>(h & 1023u) * (1.0f / 1023.0f);
}

static float valueNoise(float x, float z) {
  const float xFloor = floorf(x);
  const float zFloor = floorf(z);
  const int x0 = static_cast<int>(xFloor);
  const int z0 = static_cast<int>(zFloor);
  const float fx = x - xFloor;
  const float fz = z - zFloor;
  const float u = fx * fx * (3.0f - 2.0f * fx);
  const float v = fz * fz * (3.0f - 2.0f * fz);
  const float a = hash01(x0, z0);
  const float b = hash01(x0 + 1, z0);
  const float c = hash01(x0, z0 + 1);
  const float d = hash01(x0 + 1, z0 + 1);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

// Peaks where the noise crosses its middle, so ridges connect instead of speckling.
static float ridge(float x, float z) {
  const float n = valueNoise(x, z);
  const float crest = 1.0f - fabsf(n * 2.0f - 1.0f);
  return crest * crest;
}

// The camera follows this. Spires are added on top and are not climbed for you.
static float rangeHeight(float x, float z) {
  const float range = ridge(x * 0.012f, z * 0.012f);
  const float fold = ridge(x * 0.030f + 11.0f, z * 0.030f - 4.0f);
  return range * 13.0f + fold * 5.5f;
}

// Crests of a second field. Only the upper part rises, so most of the range
// stays a valley and the tall peaks stand in the flight path.
static float spireHeight(float x, float z) {
  constexpr float kKnee = 0.32f;
  const float n = ridge(x * 0.018f + 5.0f, z * 0.018f - 2.0f);
  if (n <= kKnee) {
    return 0.0f;
  }
  const float t = (n - kKnee) / (1.0f - kKnee);
  return t * t * 26.0f;
}

static float terrainHeight(float x, float z) {
  return rangeHeight(x, z) + spireHeight(x, z);
}

// Nose and both wings, a short way ahead. A peak that reaches the belly counts.
static bool hitsMountain() {
  const float fx = sinf(gHeading);
  const float fz = cosf(gHeading);
  const float rx = cosf(gHeading);
  const float rz = -sinf(gHeading);
  const float ahead[3] = {0.0f, 1.8f, 3.6f};
  const float side[3] = {0.0f, -3.2f, 3.2f};
  for (int a = 0; a < 3; ++a) {
    for (int s = 0; s < 3; ++s) {
      const float wx = gCamX + fx * ahead[a] + rx * side[s];
      const float wz = gCamZ + fz * ahead[a] + rz * side[s];
      if (terrainHeight(wx, wz) > gCamH) {
        return true;
      }
    }
  }
  return false;
}

static void seedTerrain() {
  gSeed = esp_random();
  if (gSeed == 0) {
    gSeed = 0xA5F17u;
  }
  gCamZ = 0.0f;
  for (int step = 0; step < 24; ++step) {
    if (spireHeight(gCamX, gCamZ) < 1.0f) {
      break;
    }
    gCamZ += 8.0f;
  }
  gCamH = rangeHeight(gCamX, gCamZ) + kClearance;
  Serial.printf("Range seed %08lX\n", static_cast<unsigned long>(gSeed));
}

static int outCode(int x, int y) {
  int code = 0;
  if (x < 0) {
    code |= 1;
  } else if (x > 127) {
    code |= 2;
  }
  if (y < 0) {
    code |= 4;
  } else if (y > 63) {
    code |= 8;
  }
  return code;
}

static bool clipSeg(int &x0, int &y0, int &x1, int &y1) {
  for (int guard = 0; guard < 8; ++guard) {
    const int c0 = outCode(x0, y0);
    const int c1 = outCode(x1, y1);
    if ((c0 | c1) == 0) {
      return true;
    }
    if ((c0 & c1) != 0) {
      return false;
    }
    const int edge = c0 != 0 ? c0 : c1;
    int x = x0;
    int y = y0;
    if ((edge & 8) != 0) {
      if (y1 == y0) {
        return false;
      }
      x = x0 + (x1 - x0) * (63 - y0) / (y1 - y0);
      y = 63;
    } else if ((edge & 4) != 0) {
      if (y1 == y0) {
        return false;
      }
      x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0);
      y = 0;
    } else if ((edge & 2) != 0) {
      if (x1 == x0) {
        return false;
      }
      y = y0 + (y1 - y0) * (127 - x0) / (x1 - x0);
      x = 127;
    } else {
      if (x1 == x0) {
        return false;
      }
      y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
      x = 0;
    }
    if (edge == c0) {
      x0 = x;
      y0 = y;
    } else {
      x1 = x;
      y1 = y;
    }
  }
  return false;
}

static void drawSeg(int x0, int y0, int x1, int y1) {
  if (!clipSeg(x0, y0, x1, y1)) {
    return;
  }
  display.drawLine(x0, y0, x1, y1);
}

// Nose up lifts the craft a little on the glass as well as in the world.
static int shipScreenY(float pitch) {
  const float nose = clampf(pitch, -kMaxPitch, kMaxPitch);
  int y = kShipY - static_cast<int>(lroundf(nose * 12.0f));
  if (y < 36) {
    y = 36;
  }
  if (y > 54) {
    y = 54;
  }
  return y;
}

struct Star {
  int8_t along;
  int8_t up;
  uint8_t kind;
};

// along spreads across the sky, up lifts off the horizon. Depth shrinks as
// you fly, so each star grows out of the point ahead and slips past the wings.
static const Star kStars[] = {
    {-18, 2, 0}, {-14, 7, 1}, {-16, 4, 2}, {-10, 9, 0}, {-8, 3, 1}, {-5, 6, 2},
    {-2, 2, 0},  {0, 8, 1},   {3, 4, 2},   {6, 9, 0},   {9, 3, 1},   {12, 6, 0},
    {16, 2, 2},  {18, 8, 1},  {-12, 5, 0}, {7, 7, 2},   {-7, 8, 1},  {14, 4, 0},
    {-20, 6, 2}, {4, 5, 0},   {-6, 1, 0},  {11, 1, 1},  {-3, 1, 2},  {2, 3, 0},
};

static void projectSky(float rawX, float rawY, float cr, float sr, float pivotX, float pivotY, int &sx, int &sy) {
  const float dx = rawX - pivotX;
  const float dy = rawY - pivotY;
  sx = static_cast<int>(lroundf(pivotX + dx * cr + dy * sr));
  sy = static_cast<int>(lroundf(pivotY - dx * sr + dy * cr));
}

static void drawStars(float pitch, float roll, int shipY, float warp) {
  const float bank = clampf(roll, -kMaxBank, kMaxBank);
  const float nose = clampf(pitch, -kMaxPitch, kMaxPitch);
  const float horizonY = kHorizon + nose * kPitchPx;
  const float cr = cosf(bank);
  const float sr = sinf(bank);
  const float pivotX = static_cast<float>(kShipX);
  const float pivotY = static_cast<float>(shipY);
  constexpr float kStarFocal = 22.0f;
  constexpr float kStarNear = 1.15f;
  constexpr float kStarSpan = 7.6f;

  display.setDrawColor(1);
  const int count = static_cast<int>(sizeof(kStars) / sizeof(kStars[0]));
  for (int i = 0; i < count; ++i) {
    const Star &star = kStars[i];
    if (warp < 2.0f && star.kind == 2 &&
        ((millis() / 180 + static_cast<uint32_t>(i)) % 5) == 0) {
      continue;
    }
    const float speed = 0.85f * (0.75f + static_cast<float>(i % 4) * 0.18f);
    float depth = kStarSpan - fmodf(gStarTime * speed + static_cast<float>(i) * 0.85f, kStarSpan);
    if (depth < 0.0f) {
      depth += kStarSpan;
    }
    depth += kStarNear;
    const float along = static_cast<float>(star.along) * kStarFocal / depth;
    const float up = static_cast<float>(star.up) * kStarFocal / depth;
    const float rawX = 63.5f + along;
    const float rawY = horizonY - up;
    int sx = 0;
    int sy = 0;
    projectSky(rawX, rawY, cr, sr, pivotX, pivotY, sx, sy);

    const float streak = (warp - 1.0f) * 1.15f;
    const float radius = sqrtf(along * along + up * up);
    if (streak >= 1.4f && radius > 2.0f) {
      float len = streak;
      if (len > radius * 0.8f) {
        len = radius * 0.8f;
      }
      const float ux = along / radius;
      const float uy = up / radius;
      int x0 = 0;
      int y0 = 0;
      projectSky(rawX - ux * len, rawY + uy * len, cr, sr, pivotX, pivotY, x0, y0);
      drawSeg(x0, y0, sx, sy);
      continue;
    }
    if (sx < 0 || sy < 0 || sx > 127 || sy > 63) {
      continue;
    }
    display.drawPixel(sx, sy);
    if (star.kind == 1 || depth < 2.2f) {
      if (sx > 0) {
        display.drawPixel(sx - 1, sy);
      }
      if (sx < 127) {
        display.drawPixel(sx + 1, sy);
      }
      if (sy > 0) {
        display.drawPixel(sx, sy - 1);
      }
      if (sy < 63) {
        display.drawPixel(sx, sy + 1);
      }
    }
  }
}

// Depth lines and cross lines of one mesh. A slice that has wrapped back to
// the horizon is not joined to the slice passing under the wings.
static void drawRidges(float pitch, float roll, int shipY) {
  const float bank = clampf(roll, -kMaxBank, kMaxBank);
  const float nose = clampf(pitch, -kMaxPitch, kMaxPitch);
  const float horizonY = kHorizon + nose * kPitchPx;
  const float cr = cosf(bank);
  const float sr = sinf(bank);
  const float fx = sinf(gHeading);
  const float fz = cosf(gHeading);
  const float rightX = cosf(gHeading);
  const float rightZ = -sinf(gHeading);
  const float span = kZFar - kZNear;
  const float phase = fmodf(gTravel, span);
  const float half = (kCols - 1) * 0.5f;
  const float pivotX = static_cast<float>(kShipX);
  const float pivotY = static_cast<float>(shipY);

  display.setDrawColor(1);
  int sx[kSlices][kCols];
  int sy[kSlices][kCols];
  float zs[kSlices];

  for (int slice = 0; slice < kSlices; ++slice) {
    // Ease the slots toward the near plane, so the mesh is finer in front
    // of the wings and opens up at the horizon.
    const float t = (static_cast<float>(slice) + 0.5f) / static_cast<float>(kSlices);
    const float biased = 1.0f - (1.0f - t) * (1.0f - t);
    float along = biased * span + phase;
    along -= floorf(along / span) * span;
    const float z = kZFar - along;
    zs[slice] = z;
    const float scale = kVert / z;
    for (int col = 0; col < kCols; ++col) {
      // Each row leaves the glass. Columns sit closer together in the center.
      const float uniform = (static_cast<float>(col) - half) / half;
      const float mag = fabsf(uniform);
      const float ndc = copysignf(powf(mag, kCenterDensity), uniform);
      const float halfPx = 70.0f + 16.0f * (kZNear / z);
      const float lateral = ndc * z * (halfPx / kFocal);
      const float wx = gCamX + fx * z + rightX * lateral;
      const float wz = gCamZ + fz * z + rightZ * lateral;
      const float rawX = 63.5f + ndc * halfPx;
      const float rawY = horizonY + (gCamH - terrainHeight(wx, wz)) * scale;
      const float dx = rawX - pivotX;
      const float dy = rawY - pivotY;
      sx[slice][col] = static_cast<int>(lroundf(pivotX + dx * cr + dy * sr));
      sy[slice][col] = static_cast<int>(lroundf(pivotY - dx * sr + dy * cr));
    }
    for (int col = 0; col < kCols - 1; ++col) {
      drawSeg(sx[slice][col], sy[slice][col], sx[slice][col + 1], sy[slice][col + 1]);
    }
  }

  // Join slices that sit next to each other in depth. The one that just
  // wrapped to the horizon is the far end of this order, so it is not
  // tied back to the row passing under the wings.
  int order[kSlices];
  for (int i = 0; i < kSlices; ++i) {
    order[i] = i;
  }
  for (int i = 1; i < kSlices; ++i) {
    const int key = order[i];
    int j = i - 1;
    while (j >= 0 && zs[order[j]] > zs[key]) {
      order[j + 1] = order[j];
      --j;
    }
    order[j + 1] = key;
  }
  for (int i = 0; i < kSlices - 1; ++i) {
    const int a = order[i];
    const int b = order[i + 1];
    for (int col = 0; col < kCols; ++col) {
      drawSeg(sx[a][col], sy[a][col], sx[b][col], sy[b][col]);
    }
  }
}

static uint8_t gStroke[128 * 64 / 8];

static void strokePixel(int x, int y) {
  if (x < 0 || y < 0 || x >= 128 || y >= 64) {
    return;
  }
  gStroke[(y * 128 + x) >> 3] |= static_cast<uint8_t>(1u << (x & 7));
}

static void fillTri(int x0, int y0, int x1, int y1, int x2, int y2) {
  if (y0 > y1) {
    int swap = x0;
    x0 = x1;
    x1 = swap;
    swap = y0;
    y0 = y1;
    y1 = swap;
  }
  if (y1 > y2) {
    int swap = x1;
    x1 = x2;
    x2 = swap;
    swap = y1;
    y1 = y2;
    y2 = swap;
  }
  if (y0 > y1) {
    int swap = x0;
    x0 = x1;
    x1 = swap;
    swap = y0;
    y0 = y1;
    y1 = swap;
  }
  if (y2 < 0 || y0 > 63) {
    return;
  }
  const int yStart = y0 < 0 ? 0 : y0;
  const int yEnd = y2 > 63 ? 63 : y2;
  for (int y = yStart; y <= yEnd; ++y) {
    const float yf = static_cast<float>(y);
    const float span = static_cast<float>(y2 - y0);
    const float xLong = span == 0.0f
        ? static_cast<float>(x0)
        : static_cast<float>(x0) + static_cast<float>(x2 - x0) * (yf - static_cast<float>(y0)) / span;
    float xShort = xLong;
    if (y0 == y1) {
      const float leg = static_cast<float>(y2 - y1);
      xShort = leg == 0.0f
          ? static_cast<float>(x1)
          : static_cast<float>(x1) + static_cast<float>(x2 - x1) * (yf - static_cast<float>(y1)) / leg;
    } else if (yf < static_cast<float>(y1)) {
      const float leg = static_cast<float>(y1 - y0);
      xShort = static_cast<float>(x0) + static_cast<float>(x1 - x0) * (yf - static_cast<float>(y0)) / leg;
    } else {
      const float leg = static_cast<float>(y2 - y1);
      xShort = leg == 0.0f
          ? static_cast<float>(x2)
          : static_cast<float>(x1) + static_cast<float>(x2 - x1) * (yf - static_cast<float>(y1)) / leg;
    }
    int left = static_cast<int>(floorf(xLong < xShort ? xLong : xShort));
    int right = static_cast<int>(ceilf(xLong > xShort ? xLong : xShort));
    if (left < 0) {
      left = 0;
    }
    if (right > 127) {
      right = 127;
    }
    for (int x = left; x <= right; ++x) {
      strokePixel(x, y);
    }
  }
}

static void fillFan(int cx, int cy, const int8_t *v, int n) {
  const int x0 = cx + v[0];
  const int y0 = cy + v[1];
  for (int i = 1; i < n - 1; ++i) {
    fillTri(x0, y0, cx + v[i * 2], cy + v[i * 2 + 1], cx + v[(i + 1) * 2], cy + v[(i + 1) * 2 + 1]);
  }
}

static void outlinePoly(int cx, int cy, const int8_t *v, int n) {
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    display.drawLine(cx + v[i * 2], cy + v[i * 2 + 1], cx + v[j * 2], cy + v[j * 2 + 1]);
  }
}

static void flushSolid() {
  display.setDrawColor(1);
  for (int i = 0; i < 128 * 64; ++i) {
    if ((gStroke[i >> 3] & (1u << (i & 7))) == 0) {
      continue;
    }
    display.drawPixel(i % 128, i / 128);
  }
  memset(gStroke, 0, sizeof(gStroke));
}

// Filled rear view. White body, black rim, so it stays a solid shape on sky
// and where a contour crosses the wings.
static void drawShip(bool boosting, int cx, int cy) {
  static const int8_t kNose[] = {-3, -5, 0, -11, 3, -5};
  static const int8_t kHull[] = {-7, -5, 7, -5, 9, 1, 6, 7, -6, 7, -9, 1};
  static const int8_t kWingL[] = {-6, -4, -29, -5, -29, 1, -6, 2};
  static const int8_t kWingR[] = {6, -4, 29, -5, 29, 1, 6, 2};
  static const int8_t kFinL[] = {-11, 2, -19, 9, -11, 6};
  static const int8_t kFinR[] = {11, 2, 19, 9, 11, 6};
  const int8_t *parts[] = {kNose, kHull, kWingL, kWingR, kFinL, kFinR};
  const int counts[] = {3, 6, 4, 4, 3, 3};

  for (int i = 0; i < 6; ++i) {
    fillFan(cx, cy, parts[i], counts[i]);
  }
  if (boosting) {
    const int ext = 4 + static_cast<int>((millis() / 70) & 1) * 5;
    fillTri(cx - 16, cy + 2, cx - 14, cy + 2, cx - 15, cy + 2 + ext);
    fillTri(cx + 14, cy + 2, cx + 16, cy + 2, cx + 15, cy + 2 + ext);
    fillTri(cx - 1, cy + 6, cx + 1, cy + 6, cx, cy + 6 + ext);
  }
  flushSolid();

  display.setDrawColor(0);
  for (int i = 0; i < 6; ++i) {
    outlinePoly(cx, cy, parts[i], counts[i]);
  }
  display.drawBox(cx - 1, cy - 4, 3, 3);
  display.setDrawColor(1);
}

static void drawItem(const char *text, int y, bool selected) {
  display.setFont(u8g2_font_helvB08_tr);
  display.setFontMode(1);
  const int w = static_cast<int>(display.getStrWidth(text));
  const int x = (128 - w) / 2;
  if (selected) {
    display.setDrawColor(1);
    display.drawBox(x - 5, y - 9, w + 10, 12);
    display.setDrawColor(0);
    display.drawStr(x, y, text);
    display.setDrawColor(1);
    return;
  }
  display.setDrawColor(0);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx != 0 || dy != 0) {
        display.drawStr(x + dx, y + dy, text);
      }
    }
  }
  display.setDrawColor(1);
  display.drawStr(x, y, text);
}

static void drawMenu() {
  drawItem("Fly", 14, gItem == 0);
  drawItem("Exit", 30, gItem == 1);
}

// GP3 clicks change the row. A hold that began in flight is throttle, so it
// does not also move the menu when BOOT opens it.
static void pollMenuSelect() {
  static bool down = false;
  static bool beganInMenu = false;
  static uint32_t downAt = 0;

  const bool pressed = digitalRead(kSelectPin) == HIGH;
  if (pressed) {
    if (!down) {
      down = true;
      beganInMenu = gMenu;
      downAt = millis();
    }
    return;
  }
  if (down && beganInMenu && gMenu) {
    const uint32_t held = millis() - downAt;
    if (held >= 40 && held < 700) {
      gItem ^= 1;
      Serial.println(gItem == 0 ? "Menu Fly" : "Menu Exit");
    }
  }
  down = false;
}

static void onBootTap() {
  if (!gMenu) {
    gMenu = true;
    gItem = 0;
    Serial.println("Menu");
    return;
  }
  if (gItem == 0) {
    if (gCrashed) {
      const float back = 12.0f;
      gCamX -= sinf(gHeading) * back;
      gCamZ -= cosf(gHeading) * back;
      if (gTravel > back) {
        gTravel -= back;
      }
      gCamH = rangeHeight(gCamX, gCamZ) + kClearance + 4.0f;
      gCrashed = false;
    }
    gMenu = false;
    Serial.println("Fly");
    return;
  }
  powerOff();
}

static float boostScale(uint32_t now, bool held) {
  static bool primed = false;
  static float scale = 1.0f;
  static uint32_t lastMs = 0;

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
  const float step = (kBoostPeak - 1.0f) * (static_cast<float>(dt) / static_cast<float>(kBoostRampMs));
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

// Horizontal tears. The OLED buffer is pages of columns, LSB at the top of the page.
static bool glitchPixel(const uint8_t *buf, int stride, int x, int y) {
  return (buf[(y >> 3) * stride + x] & static_cast<uint8_t>(1u << (y & 7))) != 0;
}

static void glitchSet(uint8_t *buf, int stride, int x, int y, bool on) {
  uint8_t &cell = buf[(y >> 3) * stride + x];
  const uint8_t mask = static_cast<uint8_t>(1u << (y & 7));
  if (on) {
    cell |= mask;
  } else {
    cell &= static_cast<uint8_t>(~mask);
  }
}

static void tearRow(uint8_t *buf, int stride, int y, int shift, bool invert, uint32_t noise) {
  uint8_t row[128];
  for (int x = 0; x < 128; ++x) {
    row[x] = glitchPixel(buf, stride, x, y) ? 1 : 0;
  }
  for (int x = 0; x < 128; ++x) {
    const int src = x - shift;
    bool on = false;
    if (src >= 0 && src < 128) {
      on = row[src] != 0;
    } else {
      on = ((noise >> (x & 7)) & 1u) != 0;
    }
    if (invert) {
      on = !on;
    }
    glitchSet(buf, stride, x, y, on);
  }
}

static void tearColumn(uint8_t *buf, int stride, int x, int shift, uint32_t noise) {
  uint8_t col[64];
  for (int y = 0; y < 64; ++y) {
    col[y] = glitchPixel(buf, stride, x, y) ? 1 : 0;
  }
  for (int y = 0; y < 64; ++y) {
    const int src = y - shift;
    bool on = false;
    if (src >= 0 && src < 64) {
      on = col[src] != 0;
    } else {
      on = ((noise >> (y & 7)) & 1u) != 0;
    }
    glitchSet(buf, stride, x, y, on);
  }
}

// Severe tears the whole glass. After the impact, a few bands keep sliding
// so the picture stays broken until the menu takes over.
static void drawGlitch(uint32_t now, bool severe) {
  uint8_t *buf = display.getBufferPtr();
  const int stride = static_cast<int>(display.getBufferTileWidth()) * 8;
  uint32_t noise = now * 747796405u + 2891336453u;
  const int bands = severe ? 14 : 4;
  for (int band = 0; band < bands; ++band) {
    noise = noise * 1664525u + 1013904223u;
    const int y0 = static_cast<int>(noise >> 8) & 63;
    const int rows = severe ? 2 + static_cast<int>((noise >> 2) & 3) : 1;
    int shift = static_cast<int>((noise >> 16) & 31) - 15;
    if (severe) {
      shift *= 2;
    }
    if (shift > -6 && shift < 6) {
      shift = (noise & 1u) != 0 ? 18 : -18;
    }
    const bool invert = severe ? ((noise >> 5) & 1u) == 0 : ((noise >> 5) & 3u) == 0;
    for (int y = y0; y < y0 + rows && y < 64; ++y) {
      tearRow(buf, stride, y, shift, invert, noise);
    }
  }
  if (!severe) {
    return;
  }
  for (int strip = 0; strip < 5; ++strip) {
    noise = noise * 1664525u + 1013904223u;
    const int x0 = static_cast<int>(noise >> 4) & 127;
    const int shift = static_cast<int>((noise >> 12) & 15) - 7;
    const int width = 2 + static_cast<int>((noise >> 18) & 3);
    for (int x = x0; x < x0 + width && x < 128; ++x) {
      tearColumn(buf, stride, x, shift == 0 ? 9 : shift, noise);
    }
  }
}

static void drawCrashLabel() {
  display.setFont(u8g2_font_helvB12_tr);
  display.setFontMode(1);
  const char *text = "CRASH";
  const int w = static_cast<int>(display.getStrWidth(text));
  const int x = (128 - w) / 2;
  const int y = 16;
  display.setDrawColor(0);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx != 0 || dy != 0) {
        display.drawStr(x + dx, y + dy, text);
      }
    }
  }
  display.setDrawColor(1);
  display.drawStr(x, y, text);
}

static void fly(float pitch, float roll, float scale, float dt) {
  const float bank = clampf(roll, -kMaxBank, kMaxBank);
  const float nose = clampf(pitch, -kMaxPitch, kMaxPitch);
  gHeading += bank * kTurn * dt;
  if (gHeading > 6.2831853f) {
    gHeading -= 6.2831853f;
  } else if (gHeading < 0.0f) {
    gHeading += 6.2831853f;
  }
  const float step = kCruise * scale * dt;
  gTravel += step;
  gCamX += sinf(gHeading) * step;
  gCamZ += cosf(gHeading) * step;

  const float ground = rangeHeight(gCamX, gCamZ);
  float desired = ground + kClearance + nose * kClimb;
  const float floorH = ground + 2.5f;
  if (desired < floorH) {
    desired = floorH;
  }
  if (desired > ground + 36.0f) {
    desired = ground + 36.0f;
  }
  float blend = dt * 3.6f;
  if (blend > 1.0f) {
    blend = 1.0f;
  }
  gCamH += (desired - gCamH) * blend;
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

  pinMode(kSelectPin, INPUT_PULLDOWN);
  Serial.println("Menu is up. GP3 switches Fly and Exit. BOOT confirms. Exit shuts down.");

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
  static bool live = false;

  pollPowerButton();
  if (pollBoot() == BootTap::Short) {
    onBootTap();
  }
  pollMenuSelect();

  const uint32_t now = millis();
  const bool throttle = !gMenu && !gCrashed && digitalRead(kSelectPin) == HIGH;
  const float scale = boostScale(now, throttle);
  uint32_t dtMs = lastFrameMs == 0 ? 0 : now - lastFrameMs;
  lastFrameMs = now;
  if (dtMs > 100) {
    dtMs = 100;
  }
  const float dt = static_cast<float>(dtMs) / 1000.0f;

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

  if (!gMenu && !gCrashed) {
    fly(pitch, roll, scale, dt);
    if (hitsMountain()) {
      gCrashed = true;
      gCrashAt = now;
      Serial.println("Crash");
    }
  }
  if (!gCrashed) {
    gStarTime += (gMenu ? 0.35f : scale) * dt;
  }
  float warp = gMenu ? 1.0f : scale;
  if (gCrashed) {
    warp = (now - gCrashAt) < 200 ? kBoostPeak : 1.0f;
  }
  engineAudioSetWarp(warp);

  display.clearBuffer();
  const int shipY = shipScreenY(pitch);
  drawStars(pitch, roll, shipY, warp);
  drawRidges(pitch, roll, shipY);
  int shipX = kShipX;
  int shipDrawY = shipY;
  if (gCrashed && !gMenu) {
    const uint32_t age = now - gCrashAt;
    if (age < kCrashBurstMs) {
      shipX += static_cast<int>((age / 40) & 7) - 3;
      shipDrawY += static_cast<int>((age / 55) & 5) - 2;
    } else {
      shipX += 4;
      shipDrawY += 3;
    }
  }
  drawShip(!gMenu && !gCrashed && scale > 1.08f, shipX, shipDrawY);
  if (gMenu) {
    drawMenu();
  } else if (gCrashed) {
    drawCrashLabel();
    drawGlitch(now, (now - gCrashAt) < kCrashBurstMs);
  }
  display.sendBuffer();

  if (live && !gMenu && now - lastLogMs >= 500) {
    lastLogMs = now;
    Serial.printf(
        "hdg %.0f  alt %.1f  g %+0.2f %+0.2f %+0.2f\n",
        gHeading * 57.2958f, gCamH, gx, gy, gz);
  }

  const uint32_t spent = millis() - now;
  if (spent < 28) {
    delay(28 - spent);
  }
}
