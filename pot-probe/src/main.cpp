#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>

#include <U8g2lib.h>

// Same OLED wiring as sling-bird. Pads are intentionally not touched.
static constexpr int kOledSda = 8;
static constexpr int kOledScl = 9;
static constexpr int kPotPin = 3;
static constexpr uint32_t kWindowMs = 1000;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);
static bool oledOk = false;

static bool device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static uint8_t wait_for_oled() {
  for (int attempt = 0; attempt < 40; ++attempt) {
    if (device_present(0x3C)) {
      return 0x3C;
    }
    if (device_present(0x3D)) {
      return 0x3D;
    }
    delay(50);
  }
  return 0;
}

struct Window {
  int minV;
  int maxV;
  int last;
  int64_t sum;
  int64_t sumSq;
  uint32_t count;
  uint32_t bigJumps;
};

static void window_reset(Window *w, int seed) {
  w->minV = seed;
  w->maxV = seed;
  w->last = seed;
  w->sum = seed;
  w->sumSq = static_cast<int64_t>(seed) * seed;
  w->count = 1;
  w->bigJumps = 0;
}

static void window_add(Window *w, int raw) {
  if (raw < w->minV) {
    w->minV = raw;
  }
  if (raw > w->maxV) {
    w->maxV = raw;
  }
  const int delta = raw - w->last;
  if (delta > 40 || delta < -40) {
    w->bigJumps++;
  }
  w->last = raw;
  w->sum += raw;
  w->sumSq += static_cast<int64_t>(raw) * raw;
  w->count++;
}

static int window_mean(const Window *w) {
  if (w->count == 0) {
    return 0;
  }
  return static_cast<int>(w->sum / w->count);
}

static int window_rms(const Window *w) {
  if (w->count < 2) {
    return 0;
  }
  const int64_t mean = w->sum / w->count;
  const int64_t var = w->sumSq / w->count - mean * mean;
  if (var <= 0) {
    return 0;
  }
  int64_t x = var;
  int64_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + var / x) / 2;
  }
  return static_cast<int>(x);
}

static int settled_read() {
  pinMode(kPotPin, INPUT);
  analogRead(kPotPin);
  delay(2);
  return analogRead(kPotPin);
}

static int pulled_read(bool up) {
  analogRead(kPotPin);
  if (up) {
    gpio_pullup_en(static_cast<gpio_num_t>(kPotPin));
    gpio_pulldown_dis(static_cast<gpio_num_t>(kPotPin));
  } else {
    gpio_pullup_dis(static_cast<gpio_num_t>(kPotPin));
    gpio_pulldown_en(static_cast<gpio_num_t>(kPotPin));
  }
  delay(8);
  const int raw = analogRead(kPotPin);
  gpio_pullup_dis(static_cast<gpio_num_t>(kPotPin));
  gpio_pulldown_dis(static_cast<gpio_num_t>(kPotPin));
  pinMode(kPotPin, INPUT);
  return raw;
}

static void probe_connection() {
  const int open = settled_read();
  const int up = pulled_read(true);
  const int down = pulled_read(false);
  const char *state = "driven-by-pot";
  if (up > 3000 && down < 400) {
    state = "OPEN/floating — SIG not held by pot";
  } else if (open < 80 && up < 200) {
    state = "held at GND";
  } else if (open > 3900 && down > 3500) {
    state = "held at 3.3V";
  } else if (up - down > 800) {
    state = "WEAK — pot not firmly driving the pin";
  }
  Serial.printf("conn plain=%4d pullup=%4d pulldown=%4d  %s\n", open, up, down, state);
}

void setup() {
  Serial.begin(115200);
  delay(400);
  pinMode(kPotPin, INPUT);
  analogReadResolution(12);
  analogRead(kPotPin);
  Serial.println("connection check (leave knob still):");
  probe_connection();
  probe_connection();

  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(400000);
  const uint8_t addr = wait_for_oled();
  if (addr == 0) {
    Serial.println("No OLED at 0x3C/0x3D on SDA8/SCL9. Serial probe still runs.");
  } else {
    Serial.printf("OLED at 0x%02X\n", addr);
    display.setI2CAddress(static_cast<uint8_t>(addr << 1));
    display.begin();
    display.setFontPosBaseline();
    oledOk = true;
  }

  Serial.println("pot-probe: GPIO3 only. Leave pads alone.");
  Serial.println("1) Leave the knob still for a few seconds — watch span/rms.");
  Serial.println("2) Then turn it slowly end to end — watch min/max cover ~0..4095.");
}

void loop() {
  static Window win;
  static bool haveWin = false;
  static uint32_t winStart = 0;
  static int stillBestSpan = 9999;
  static int moveBestSpan = 0;
  static uint32_t windows = 0;

  const uint32_t now = millis();
  analogRead(kPotPin);
  const int raw = analogRead(kPotPin);

  if (!haveWin) {
    window_reset(&win, raw);
    winStart = now;
    haveWin = true;
  } else {
    window_add(&win, raw);
  }

  if (now - winStart < kWindowMs) {
    return;
  }

  const int span = win.maxV - win.minV;
  const int mean = window_mean(&win);
  const int rms = window_rms(&win);
  windows++;

  const bool likelyStill = span < 120;
  if (likelyStill && span < stillBestSpan) {
    stillBestSpan = span;
  }
  if (span > moveBestSpan) {
    moveBestSpan = span;
  }

  const char *verdict = "ok";
  // With the knob held still, span>80 is already bad; span>200 is a broken/dirty track.
  if (span >= 200) {
    verdict = "BAD-CONTACT";
  } else if (likelyStill && span >= 80) {
    verdict = "NOISY-still";
  } else if (likelyStill && span >= 40) {
    verdict = "mild-jitter";
  } else if (!likelyStill && span < 200) {
    verdict = "weak-sweep";
  } else if (!likelyStill) {
    verdict = "moving";
  }

  Serial.printf(
      "win#%lu raw=%4d mean=%4d min=%4d max=%4d span=%4d rms=%3d jumps=%lu  %s  "
      "(best_still_span=%d best_move_span=%d)\n",
      static_cast<unsigned long>(windows), raw, mean, win.minV, win.maxV, span, rms,
      static_cast<unsigned long>(win.bigJumps), verdict, stillBestSpan, moveBestSpan);

  if ((windows % 5) == 0) {
    probe_connection();
  }

  if (oledOk) {
    display.clearBuffer();
    display.setFont(u8g2_font_5x7_tr);
    char line[32];
    snprintf(line, sizeof(line), "POT GPIO3 probe");
    display.drawStr(2, 10, line);
    snprintf(line, sizeof(line), "raw %d", raw);
    display.drawStr(2, 22, line);
    snprintf(line, sizeof(line), "1s span %d rms %d", span, rms);
    display.drawStr(2, 34, line);
    snprintf(line, sizeof(line), "min %d max %d", win.minV, win.maxV);
    display.drawStr(2, 46, line);
    snprintf(line, sizeof(line), "%s", verdict);
    display.drawStr(2, 58, line);
    display.sendBuffer();
  }

  haveWin = false;
}
