#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "bitmaps.h"

// Header pins on the ESP32-C6-ePaper-1.54. SDA/SCL are the shared I2C bus.
static constexpr int kSdaPin = 18;
static constexpr int kSclPin = 8;
static constexpr int kWidth = 128;
static constexpr int kHeight = 64;
static constexpr int kFrameBytes = (kWidth * kHeight) / 8;
// OLED Animation Maker export: 121 frames, 67 ms apart (~15 fps).
static constexpr uint32_t kFrameMs = 67;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);

static const uint8_t *const kFrames[] = {
    frame0,   frame1,   frame2,   frame3,   frame4,   frame5,   frame6,
    frame7,   frame8,   frame9,   frame10,  frame11,  frame12,  frame13,
    frame14,  frame15,  frame16,  frame17,  frame18,  frame19,  frame20,
    frame21,  frame22,  frame23,  frame24,  frame25,  frame26,  frame27,
    frame28,  frame29,  frame30,  frame31,  frame32,  frame33,  frame34,
    frame35,  frame36,  frame37,  frame38,  frame39,  frame40,  frame41,
    frame42,  frame43,  frame44,  frame45,  frame46,  frame47,  frame48,
    frame49,  frame50,  frame51,  frame52,  frame53,  frame54,  frame55,
    frame56,  frame57,  frame58,  frame59,  frame60,  frame61,  frame62,
    frame63,  frame64,  frame65,  frame66,  frame67,  frame68,  frame69,
    frame70,  frame71,  frame72,  frame73,  frame74,  frame75,  frame76,
    frame77,  frame78,  frame79,  frame80,  frame81,  frame82,  frame83,
    frame84,  frame85,  frame86,  frame87,  frame88,  frame89,  frame90,
    frame91,  frame92,  frame93,  frame94,  frame95,  frame96,  frame97,
    frame98,  frame99,  frame100, frame101, frame102, frame103, frame104,
    frame105, frame106, frame107, frame108, frame109, frame110, frame111,
    frame112, frame113, frame114, frame115, frame116, frame117, frame118,
    frame119, frame120,
};

static constexpr int kFrameCount = sizeof(kFrames) / sizeof(kFrames[0]);

// Adafruit bitmaps are MSB-left. U8g2 XBM is LSB-left. Same pixels, flipped bits.
static uint8_t xbm[kFrameBytes];

static uint8_t reverse_bits(uint8_t value) {
  value = static_cast<uint8_t>(((value & 0xF0) >> 4) | ((value & 0x0F) << 4));
  value = static_cast<uint8_t>(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
  value = static_cast<uint8_t>(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
  return value;
}

static void scan_i2c() {
  Serial.println("I2C scan (onboard chips will show up too):");
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
    Serial.println("No OLED at 0x3C or 0x3D. VCC -> 3V3, GND -> GND, SDA -> GPIO18, SCL -> GPIO8.");
    delay(1000);
  }
}

static void show_frame(const uint8_t *src) {
  for (int i = 0; i < kFrameBytes; ++i) {
    xbm[i] = reverse_bits(src[i]);
  }
  display.clearBuffer();
  display.drawXBM(0, 0, kWidth, kHeight, xbm);
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(400000);

  const uint8_t addr = wait_for_oled();
  Serial.printf("OLED at 0x%02X, %d frames\n", addr, kFrameCount);
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  Wire.setClock(400000);
  display.setDrawColor(1);
}

void loop() {
  static int index = 0;
  static uint32_t next_ms = 0;

  const uint32_t now = millis();
  if (now < next_ms) {
    return;
  }
  next_ms = now + kFrameMs;
  show_frame(kFrames[index]);
  index = (index + 1) % kFrameCount;
}
