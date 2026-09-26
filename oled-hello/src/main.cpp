#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

// Header pins on the ESP32-C6-ePaper-1.54. SDA/SCL are the shared I2C bus.
static constexpr int kSdaPin = 18;
static constexpr int kSclPin = 8;

#if defined(OLED_128X32)
static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#elif defined(OLED_SH1106)
static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kSclPin, kSdaPin);
#endif

static const char kMessage[] = "HELLO WORLD";

static void scanI2c() {
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

static bool devicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
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
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(400000);

  const uint8_t addr = waitForOled();
  Serial.printf("OLED at 0x%02X\n", addr);
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  display.setFont(u8g2_font_helvB12_tr);
  if (display.getStrWidth(kMessage) > display.getDisplayWidth() - 2) {
    display.setFont(u8g2_font_helvB08_tr);
  }
  display.setFontPosBaseline();
}

void loop() {
  static uint32_t frame = 0;

  const int width = display.getDisplayWidth();
  const int height = display.getDisplayHeight();
  const int textWidth = display.getStrWidth(kMessage);
  const int originX = (width - textWidth) / 2;
  const int restBaseline = height / 2 + display.getAscent() / 2;
  const int amplitude = height >= 64 ? 8 : 3;

  // Type the phrase, wave it, then start over.
  const int typeFrames = static_cast<int>(strlen(kMessage)) * 6 + 8;
  const int waveFrames = 140;
  const int cycle = typeFrames + waveFrames;
  const int tick = static_cast<int>(frame % cycle);

  int visible = static_cast<int>(strlen(kMessage));
  bool waving = true;
  bool cursor = false;
  if (tick < typeFrames) {
    visible = min(visible, tick / 6);
    waving = false;
    cursor = (tick / 4) % 2 == 0;
  }

  display.clearBuffer();

  int cursorX = originX;
  for (int i = 0; i < visible; ++i) {
    char glyph[2] = {kMessage[i], '\0'};
    int baseline = restBaseline;
    if (waving) {
      const float phase = (static_cast<int>(frame) + i * 8) * 0.22f;
      baseline += static_cast<int>(lroundf(sinf(phase) * amplitude));
    }
    display.drawStr(cursorX, baseline, glyph);
    cursorX += display.getStrWidth(glyph);
  }

  if (cursor && cursorX < width - 2) {
    const int cursorTop = restBaseline - display.getAscent();
    display.drawBox(cursorX + 1, cursorTop, 2, display.getAscent());
  }

  display.sendBuffer();
  frame++;
  delay(33);
}
