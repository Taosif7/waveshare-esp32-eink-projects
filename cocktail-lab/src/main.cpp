#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

// Pins: component-specifications/touch-sensor.md, potentiometer.md
static constexpr int kOledSda = 6;
static constexpr int kOledScl = 7;
static constexpr int kPotPin = 3;
static constexpr int kTouchPins[] = {4, 10, 20, 21, 1};
static constexpr int kFlavorCount = 5;
static constexpr int kMaxLayers = 5;
static constexpr int kGlassH = 38;
static constexpr int kGlassW = 34;
static constexpr int kFlowDeadzone = 6;
static constexpr int kHeightSlop = 3;
static constexpr float kDumpHoldSec = 0.8f;

#if !defined(TOUCH_ACTIVE_HIGH)
#define TOUCH_ACTIVE_HIGH 1
#endif

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

struct Layer {
  int8_t flavor;
  uint8_t height;
};

enum class Phase : uint8_t { Play, Win, Fail };

struct Game {
  Layer recipe[kMaxLayers];
  uint8_t recipeCount = 0;
  Layer sealed[kMaxLayers];
  uint8_t sealedCount = 0;
  int8_t openFlavor = -1;
  uint8_t openHeight = 0;
  int8_t heldPad = -1;  // bottle held down right now (−1 = none)
  Phase phase = Phase::Play;
  uint8_t level = 1;
  uint32_t phaseUntil = 0;
  float shine = 0;
  float stream = 0;
  float settle = 0;
  float shake = 0;
  float garnish = 0;
  float dumpAnim = 0;
  float dumpCharge = 0;  // 0..1 while long-holding pad with tap closed
  bool wasOpen = false;
  bool dumpLatched = false;  // already dumped this hold
};

static Game g;

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
    Serial.println("No OLED");
    delay(1000);
  }
}

static bool touchDown(int pin) {
#if TOUCH_ACTIVE_HIGH
  return digitalRead(pin) == HIGH;
#else
  return digitalRead(pin) == LOW;
#endif
}

static int potPercent() {
  const int raw = analogRead(kPotPin);
  const int value = 4095 - constrain(raw, 0, 4095);
  return map(value, 0, 4095, 0, 100);
}

static uint8_t sealedSum() {
  uint8_t t = 0;
  for (uint8_t i = 0; i < g.sealedCount; ++i) {
    t = static_cast<uint8_t>(t + g.sealed[i].height);
  }
  return t;
}

static uint8_t totalFilled() {
  return static_cast<uint8_t>(sealedSum() + g.openHeight);
}

static void clearGlass() {
  g.sealedCount = 0;
  g.openFlavor = -1;
  g.openHeight = 0;
  g.settle = 1.0f;
  g.dumpAnim = 1.0f;
}

static void newRecipe() {
  uint8_t n = 2;
  if (g.level >= 3) {
    n = 3;
  }
  if (g.level >= 5) {
    n = 4;
  }
  if (g.level >= 8) {
    n = 5;
  }
  if (g.level > 1 && random(0, 3) == 0 && n < 5) {
    n++;
  }
  n = constrain(n, (uint8_t)2, (uint8_t)5);

  g.recipeCount = n;
  uint8_t remaining = kGlassH;
  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t left = static_cast<uint8_t>(n - i);
    uint8_t h;
    if (left == 1) {
      h = remaining;
    } else {
      const uint8_t minH = 6;
      const uint8_t maxH = static_cast<uint8_t>(remaining - (left - 1) * minH);
      h = static_cast<uint8_t>(random(minH, max((int)minH + 1, (int)maxH + 1)));
      remaining = static_cast<uint8_t>(remaining - h);
    }
    int8_t flavor = static_cast<int8_t>(random(0, kFlavorCount));
    if (i > 0 && flavor == g.recipe[i - 1].flavor) {
      flavor = static_cast<int8_t>((flavor + 1 + random(0, 4)) % kFlavorCount);
    }
    g.recipe[i] = {flavor, h};
  }

  clearGlass();
  g.dumpAnim = 0;
  g.phase = Phase::Play;
  g.heldPad = -1;
  g.dumpCharge = 0;
  g.dumpLatched = false;
  g.garnish = 0;
  g.shake = 0;
  g.wasOpen = false;
}

static bool recipesMatch() {
  if (g.openHeight > 0 || g.sealedCount != g.recipeCount) {
    return false;
  }
  for (uint8_t i = 0; i < g.recipeCount; ++i) {
    if (g.sealed[i].flavor != g.recipe[i].flavor) {
      return false;
    }
    if (abs((int)g.sealed[i].height - (int)g.recipe[i].height) > kHeightSlop) {
      return false;
    }
  }
  return true;
}

static void sealOpenLayer() {
  if (g.openHeight == 0 || g.openFlavor < 0) {
    g.openFlavor = -1;
    g.openHeight = 0;
    return;
  }
  if (g.sealedCount < kMaxLayers) {
    g.sealed[g.sealedCount++] = {g.openFlavor, g.openHeight};
  }
  g.openFlavor = -1;
  g.openHeight = 0;
  g.settle = 1.0f;
}

static void tryJudge() {
  if (g.phase != Phase::Play) {
    return;
  }
  if (g.openHeight > 0) {
    sealOpenLayer();
  }
  if (recipesMatch()) {
    g.phase = Phase::Win;
    g.phaseUntil = millis() + 2400;
    g.garnish = 0;
    return;
  }
  if (totalFilled() >= kGlassH) {
    g.phase = Phase::Fail;
    g.phaseUntil = millis() + 1600;
    g.shake = 1.0f;
  }
}

// First held pad wins (leftmost). Hold-to-pour: no toggle state.
static int8_t readHeldPad() {
  for (uint8_t i = 0; i < kFlavorCount; ++i) {
    if (touchDown(kTouchPins[i])) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

static void pourStep(int flow, float dt) {
  const int room = kGlassH - (int)sealedSum() - (int)g.openHeight;
  if (room <= 0) {
    sealOpenLayer();
    tryJudge();
    return;
  }

  const float rate = 8.0f + (flow - kFlowDeadzone) * 0.7f;
  int add = (int)(rate * dt + 0.5f);
  if (add < 1 && rate * dt > 0.15f) {
    add = 1;
  }
  if (add > room) {
    add = room;
  }
  g.openHeight = static_cast<uint8_t>(g.openHeight + add);

  if (totalFilled() >= kGlassH) {
    sealOpenLayer();
    tryJudge();
  }
}

static void simulate(float dt) {
  g.shine += dt * 1.3f;
  g.stream += dt * (1.0f + potPercent() * 0.05f);
  if (g.settle > 0) {
    g.settle = max(0.0f, g.settle - dt * 2.8f);
  }
  if (g.shake > 0) {
    g.shake = max(0.0f, g.shake - dt * 2.2f);
  }
  if (g.dumpAnim > 0) {
    g.dumpAnim = max(0.0f, g.dumpAnim - dt * 2.0f);
  }
  if (g.phase == Phase::Win) {
    g.garnish = min(1.0f, g.garnish + dt * 1.5f);
  }

  if (g.phase != Phase::Play) {
    if (millis() >= g.phaseUntil) {
      if (g.phase == Phase::Win) {
        g.level++;
      }
      newRecipe();
    }
    return;
  }

  const int flow = potPercent();
  const bool tapOpen = flow > kFlowDeadzone;
  const int8_t held = readHeldPad();
  const int8_t prevHeld = g.heldPad;

  // Released bottle → seal current pour
  if (prevHeld >= 0 && held < 0) {
    sealOpenLayer();
    g.dumpCharge = 0;
    g.dumpLatched = false;
    if (totalFilled() >= kGlassH && !tapOpen) {
      tryJudge();
    }
  }

  // Switched bottle mid-hold → seal, then continue with new flavor
  if (prevHeld >= 0 && held >= 0 && held != prevHeld) {
    sealOpenLayer();
  }

  g.heldPad = held;

  // Closed the tap → seal; if full, auto-serve
  if (!tapOpen && g.wasOpen) {
    sealOpenLayer();
    if (totalFilled() >= kGlassH) {
      tryJudge();
    }
  }
  g.wasOpen = tapOpen;

  if (held < 0) {
    g.dumpCharge = 0;
    g.dumpLatched = false;
    return;
  }

  if (!tapOpen) {
    // Long-hold with tap closed = dump
    if (!g.dumpLatched) {
      g.dumpCharge = min(1.0f, g.dumpCharge + dt / kDumpHoldSec);
      if (g.dumpCharge >= 1.0f) {
        clearGlass();
        g.dumpLatched = true;
        g.dumpCharge = 0;
      }
    }
    return;
  }

  // Tap open + hold pad = pour
  g.dumpCharge = 0;
  g.dumpLatched = false;
  g.openFlavor = held;
  if (g.openHeight == 0) {
    g.openFlavor = held;
  }
  pourStep(flow, dt);
}

// --- drawing (Playdate-ish 1-bit: chunky outlines, Bayer dither, sparse HUD) ---

// 4x4 Bayer thresholds 0..15 for ordered dither
static const uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static void ditherRect(int x, int y, int w, int h, int level /*0..16*/, int scroll) {
  // level: how many Bayer cells light up (0 empty .. 16 solid-ish)
  if (w <= 0 || h <= 0) {
    return;
  }
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      const int bx = (xx + scroll) & 3;
      const int by = (yy + (scroll >> 1)) & 3;
      if (kBayer4[by][bx] < level) {
        display.drawPixel(xx, yy);
      }
    }
  }
}

static void fillFlavor(int x, int y, int w, int h, int8_t flavor, float t) {
  if (w <= 0 || h <= 0 || flavor < 0) {
    return;
  }
  const int scroll = (int)(t * 3.0f);
  switch (flavor) {
    case 0:  // black — “cola”
      display.drawBox(x, y, w, h);
      break;
    case 1:  // 25% dither
      ditherRect(x, y, w, h, 4, scroll);
      break;
    case 2:  // 50%
      ditherRect(x, y, w, h, 8, 0);
      break;
    case 3:  // 75%
      ditherRect(x, y, w, h, 12, scroll);
      break;
    default:  // brick / hatch
      for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
          if (((xx + yy * 2) & 3) == 0) {
            display.drawPixel(xx, yy);
          }
        }
      }
      break;
  }
}

static void drawThickGlass(int x, int rimY, int bottom) {
  // Stemless tumbler: wide rim, slight taper via inset base, 2px rim lip
  display.drawHLine(x + 1, rimY, kGlassW - 2);
  display.drawHLine(x, rimY + 1, kGlassW);
  display.drawVLine(x, rimY + 1, bottom - rimY - 1);
  display.drawVLine(x + kGlassW - 1, rimY + 1, bottom - rimY - 1);
  display.drawHLine(x + 2, bottom, kGlassW - 4);
  display.drawHLine(x + 1, bottom - 1, kGlassW - 2);
  // Inner highlight line (Playdate glass glint)
  display.drawVLine(x + 2, rimY + 3, 8);
}

static void drawGlassStack(
    int glassX,
    int glassBottom,
    const Layer *layers,
    uint8_t count,
    int8_t openFlav,
    uint8_t openH,
    float t,
    bool showBubbles) {
  const int innerX = glassX + 3;
  const int innerW = kGlassW - 6;
  int y = glassBottom - 2;

  auto drawBand = [&](int8_t flav, uint8_t h, bool bubbling) {
    if (h == 0) {
      return;
    }
    y -= h;
    int bandY = y;
    if (g.settle > 0.05f && !bubbling) {
      bandY += (int)(sinf((1.0f - g.settle) * 3.14f) * 2.0f);
    }
    fillFlavor(innerX, bandY, innerW, h, flav, t);
    // Meniscus
    display.drawHLine(innerX, bandY, innerW);
    if (bubbling && showBubbles) {
      for (int i = 0; i < 5; ++i) {
        const float a = t * 4.0f + i * 1.3f;
        const int bx = innerX + 2 + (int)((sinf(a) * 0.5f + 0.5f) * (innerW - 6));
        const int by = bandY + h - 3 - ((int)(t * 18 + i * 7) % max(2, (int)h - 2));
        if (by >= bandY + 1 && by < bandY + h - 1) {
          display.drawDisc(bx, by, 1);
          display.setDrawColor(0);
          display.drawPixel(bx, by);
          display.setDrawColor(1);
        }
      }
    }
  };

  for (uint8_t i = 0; i < count; ++i) {
    drawBand(layers[i].flavor, layers[i].height, false);
  }
  if (openH > 0 && openFlav >= 0) {
    drawBand(openFlav, openH, true);
  }
}

static void drawPour(int glassX, int rimY, int flow, float t) {
  if (flow <= kFlowDeadzone) {
    return;
  }
  const int cx = glassX + kGlassW / 2;
  const int thick = 1 + (flow - kFlowDeadzone) / 22;
  // Chunky dashed stream
  for (int i = 0; i < 6; ++i) {
    const float u = fmodf(t * 3.0f + i * 0.16f, 1.0f);
    const int sy = rimY - 10 + (int)(u * 12);
    const int wobble = (int)(sinf(t * 10 + i) * 1.5f);
    display.drawBox(cx - thick / 2 + wobble, sy, thick + 1, 3);
  }
}

static void drawBottle(int x, int y, int8_t flavor, bool selected, float t) {
  // Chunky 9×14 bottle sprite
  const int lift = selected ? (int)(fabsf(sinf(t * 8)) * 2) : 0;
  const int by = y - lift;
  display.drawBox(x + 3, by, 3, 3);          // neck
  display.drawFrame(x + 1, by + 3, 7, 10);   // body
  fillFlavor(x + 2, by + 4, 5, 8, flavor, t);
  display.drawHLine(x + 2, by, 5);           // cork
  if (selected) {
    display.drawHLine(x, by + 14, 9);  // shadow under lifted bottle
  }
}

static void drawCrank(int cx, int cy, int flow, bool pouring, float t) {
  // Playdate-style crank: angle from flow; spins only while pouring
  display.drawCircle(cx, cy, 6);
  display.drawDisc(cx, cy, 1);
  const float ang =
      -1.2f + (flow / 100.0f) * 3.6f + (pouring ? t * 5.0f : 0.0f);
  const int hx = cx + (int)(cosf(ang) * 5);
  const int hy = cy + (int)(sinf(ang) * 5);
  display.drawLine(cx, cy, hx, hy);
  display.drawDisc(hx, hy, 2);
}

static void drawSpeech(int x, int y, int w, int h, const char *msg) {
  display.setDrawColor(0);
  display.drawBox(x, y, w, h);
  display.setDrawColor(1);
  display.drawFrame(x, y, w, h);
  display.drawFrame(x + 1, y + 1, w - 2, h - 2);
  // Tail
  display.drawTriangle(x + 8, y + h, x + 14, y + h, x + 10, y + h + 4);
  display.setFont(u8g2_font_6x12_tr);
  const int tw = display.getStrWidth(msg);
  display.drawStr(x + (w - tw) / 2, y + h - 5, msg);
  display.setFont(u8g2_font_5x7_tr);
}

static void drawUi() {
  const float t = millis() / 1000.0f;
  display.clearBuffer();
  display.setDrawColor(1);
  display.setFont(u8g2_font_5x7_tr);

  const int shakeX = g.shake > 0 ? (int)(sinf(t * 36) * g.shake * 2) : 0;
  const int flow = potPercent();

  // Floor / bar counter
  display.drawHLine(0, 58, 128);
  display.drawHLine(0, 59, 128);
  for (int x = 0; x < 128; x += 4) {
    display.drawPixel(x + ((int)(t) & 1), 60);
  }

  const bool pouring =
      flow > kFlowDeadzone && g.heldPad >= 0 && g.phase == Phase::Play;

  // Bottles on the shelf — lift only while finger is down
  for (int i = 0; i < kFlavorCount; ++i) {
    drawBottle(2 + i * 13, 2, (int8_t)i, g.heldPad == i, t);
  }

  // Level pip dots (no text chrome)
  for (int i = 0; i < 5 && i < g.level; ++i) {
    display.drawDisc(100 + i * 5, 4, 1);
  }

  const int leftX = 4 + shakeX;
  const int rightX = 70;
  const int rimY = 18;
  const int bottom = rimY + kGlassH;

  drawThickGlass(leftX, rimY, bottom);
  drawThickGlass(rightX, rimY, bottom);

  // Star marker on ticket glass
  display.drawPixel(rightX + kGlassW / 2, rimY - 3);
  display.drawHLine(rightX + kGlassW / 2 - 1, rimY - 2, 3);

  drawGlassStack(
      leftX, bottom, g.sealed, g.sealedCount, g.openFlavor, g.openHeight, t, true);
  drawGlassStack(rightX, bottom, g.recipe, g.recipeCount, -1, 0, t, false);

  // Glass shine
  if (totalFilled() < kGlassH / 2) {
    const int sx = leftX + 3 + ((int)(g.shine * 10) % (kGlassW - 8));
    display.drawVLine(sx, rimY + 4, 5);
  }

  if (pouring) {
    drawPour(leftX, rimY, flow, g.stream);
  }

  drawCrank(58, 36, flow, pouring, t);

  // Dump charge: long-hold pad while tap closed
  if (g.dumpCharge > 0.05f && g.phase == Phase::Play) {
    display.drawFrame(48, 28, 32, 6);
    const int fill = (int)(g.dumpCharge * 30);
    if (fill > 0) {
      display.drawBox(49, 29, fill, 4);
    }
    display.setFont(u8g2_font_5x7_tr);
    display.drawStr(50, 27, "DUMP");
  }

  if (g.phase == Phase::Win) {
    drawSpeech(30, 20, 68, 18, "Served!");
    const int gx = leftX + kGlassW / 2;
    const int gy = rimY - (int)(g.garnish * 7);
    display.drawTriangle(gx - 3, gy + 4, gx + 3, gy + 4, gx, gy);
    display.drawBox(leftX + 6, rimY - (int)(g.garnish * 5), 2, 10);
  } else if (g.phase == Phase::Fail) {
    drawSpeech(34, 20, 60, 18, "Nope!");
  }

  if (totalFilled() >= kGlassH && g.phase == Phase::Play) {
    for (int i = 0; i < 6; ++i) {
      display.drawDisc(leftX + 5 + i * 4, rimY - ((int)(t * 8 + i) & 1), 1);
    }
  }

  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  for (int i = 0; i < kFlavorCount; ++i) {
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
  display.setI2CAddress(static_cast<uint8_t>(addr << 1));
  display.begin();
  display.setFontPosBaseline();

  newRecipe();
  Serial.println(
      "cocktail-lab  hold pad+open tap=pour  release/close=seal  "
      "long-hold pad tap-closed=dump  full+close=serve");
}

void loop() {
  static uint32_t last = millis();
  const uint32_t now = millis();
  float dt = (now - last) / 1000.0f;
  last = now;
  if (dt > 0.08f) {
    dt = 0.08f;
  }

  simulate(dt);
  drawUi();
  delay(16);
}
