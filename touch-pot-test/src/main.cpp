#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ESP32-C3 Super Mini. OLED: component-specifications/esp32-c3-mini-1.md.
// Touch and pot: component-specifications/touch-sensor.md, potentiometer.md.
static constexpr int kOledSda = 6;
static constexpr int kOledScl = 7;
static constexpr int kPotPin = 3;

// Five pads, left to right on the OLED. Pot keeps GPIO3.
static constexpr int kTouchPins[] = {4, 10, 20, 21, 1};
static constexpr int kTouchCount = 5;

#if !defined(TOUCH_ACTIVE_HIGH)
#define TOUCH_ACTIVE_HIGH 1
#endif

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

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
    Serial.println("No OLED at 0x3C/0x3D. VCC->3.3 GND->G SDA->6 SCL->7");
    delay(1000);
  }
}

static bool touchPressed(int pin) {
  const int level = digitalRead(pin);
#if TOUCH_ACTIVE_HIGH
  return level == HIGH;
#else
  return level == LOW;
#endif
}

static void drawTouchRow(const bool pressed[kTouchCount]) {
  const int box = 20;
  const int gap = 4;
  const int rowWidth = kTouchCount * box + (kTouchCount - 1) * gap;
  const int originX = (display.getDisplayWidth() - rowWidth) / 2;
  const int originY = 10;
  const int radius = 6;

  for (int i = 0; i < kTouchCount; ++i) {
    const int x = originX + i * (box + gap);
    const int y = originY;
    display.drawFrame(x, y, box, box);

    const int cx = x + box / 2;
    const int cy = y + box / 2;
    if (pressed[i]) {
      display.drawDisc(cx, cy, radius);
    } else {
      display.drawCircle(cx, cy, radius);
    }
  }
}

static int potDisplayValue(int raw) {
  // Lab: this module's sense is reversed — see potentiometer.md.
  return 4095 - constrain(raw, 0, 4095);
}

static void drawPotBar(int raw) {
  const int barX = 4;
  const int barY = 48;
  const int barW = display.getDisplayWidth() - 8;
  const int barH = 12;
  const int value = potDisplayValue(raw);

  const int fill = map(value, 0, 4095, 0, barW);

  display.drawFrame(barX, barY, barW, barH);
  if (fill > 0) {
    display.drawBox(barX + 1, barY + 1, max(0, fill - 2), barH - 2);
  }

  display.setFont(u8g2_font_5x7_tr);
  char label[16];
  snprintf(label, sizeof(label), "%d", value);
  display.drawStr(barX, barY - 2, label);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < kTouchCount; ++i) {
#if TOUCH_ACTIVE_HIGH
    pinMode(kTouchPins[i], INPUT_PULLDOWN);
#else
    pinMode(kTouchPins[i], INPUT_PULLUP);
#endif
  }
  pinMode(kPotPin, INPUT);
  analogReadResolution(12);

  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(400000);

  const uint8_t addr = waitForOled();
  Serial.printf(
      "OLED at 0x%02X  touches=%d,%d,%d,%d,%d pot=GPIO%d active_high=%d\n",
      addr,
      kTouchPins[0],
      kTouchPins[1],
      kTouchPins[2],
      kTouchPins[3],
      kTouchPins[4],
      kPotPin,
      TOUCH_ACTIVE_HIGH);
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  display.setFontPosBaseline();
}

void loop() {
  bool pressed[kTouchCount];
  for (int i = 0; i < kTouchCount; ++i) {
    pressed[i] = touchPressed(kTouchPins[i]);
  }
  const int potRaw = analogRead(kPotPin);

  display.clearBuffer();
  drawTouchRow(pressed);
  drawPotBar(potRaw);
  display.sendBuffer();

  static uint32_t lastLogMs = 0;
  const uint32_t now = millis();
  if (now - lastLogMs >= 200) {
    lastLogMs = now;
    Serial.printf(
        "T %d%d%d%d%d  pot_raw=%4d  pot=%4d\n",
        pressed[0],
        pressed[1],
        pressed[2],
        pressed[3],
        pressed[4],
        potRaw,
        potDisplayValue(potRaw));
  }

  delay(16);
}
