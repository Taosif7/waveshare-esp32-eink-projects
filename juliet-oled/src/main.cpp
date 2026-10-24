#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ESP32-C3 Super Mini. No e-paper on this module.
// SH1106: SDA GPIO6, SCL GPIO7. component-specifications/esp32-c3-mini-1.md
static constexpr int kSdaPin = 6;
static constexpr int kSclPin = 7;
static constexpr int kWidth = 128;
static constexpr int kHeight = 64;
static constexpr int kFrameBytes = (kWidth * kHeight) / 8;

// Packed from juliet-frames.h. Adafruit bitmaps are MSB-left; these bytes are
// already reversed so U8g2 XBM (LSB-left) can draw them in place.
extern "C" {
extern const uint8_t juliet_frames[];
extern const uint8_t juliet_frames_end[];
}

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);

static void scan_i2c() {
  Serial.println("I2C scan:");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  (none)");
  }
}

static bool device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static uint8_t wait_for_oled() {
  for (;;) {
    if (device_present(0x3C)) {
      return 0x3C;
    }
    if (device_present(0x3D)) {
      return 0x3D;
    }
    scan_i2c();
    Serial.println("No OLED at 0x3C or 0x3D. VCC -> 3.3, GND -> G, SDA -> 6, SCL -> 7.");
    delay(1000);
  }
}

static void show_frame(const uint8_t *src) {
  display.clearBuffer();
  display.drawXBM(0, 0, kWidth, kHeight, src);
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  const size_t bytes = static_cast<size_t>(juliet_frames_end - juliet_frames);
  const size_t frames = bytes / kFrameBytes;
  Serial.printf("Juliet clip: %u bytes, %u frames\n",
                static_cast<unsigned>(bytes), static_cast<unsigned>(frames));
  if (frames == 0 || bytes != frames * kFrameBytes) {
    Serial.println("Frame blob is not a whole number of 128x64 frames.");
    for (;;) {
      delay(1000);
    }
  }

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(400000);

  const uint8_t addr = wait_for_oled();
  Serial.printf("OLED at 0x%02X\n", addr);
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  Wire.setClock(400000);
  display.setDrawColor(1);
}

void loop() {
  static size_t index = 0;
  const size_t frames =
      static_cast<size_t>(juliet_frames_end - juliet_frames) / kFrameBytes;
  show_frame(juliet_frames + index * kFrameBytes);
  index++;
  if (index >= frames) {
    index = 0;
  }
}
