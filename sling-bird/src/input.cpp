#include "input.h"

#include <Arduino.h>

#include "fx.h"

namespace {

constexpr int kPadPins[] = {4, 10, 20, 21, 1};
constexpr int kPadCount = 5;
constexpr int kRight = 4;

#if !defined(TOUCH_ACTIVE_HIGH)
#define TOUCH_ACTIVE_HIGH 1
#endif

enum class Sling : uint8_t { Idle, Draw, Release };

Sling sling = Sling::Idle;
bool prev[kPadCount];
bool waitRelease = true;
uint32_t drawStart = 0;
uint32_t releaseStart = 0;
int32_t lastPower = 0;
constexpr int kPotPin = 3;
int smooth = 2048;

bool pad_down(int pin) {
  const int level = digitalRead(pin);
#if TOUCH_ACTIVE_HIGH
  return level == HIGH;
#else
  return level == LOW;
#endif
}

int32_t centroid(const bool down[kPadCount]) {
  int num = 0;
  int den = 0;
  for (int i = 0; i < kPadCount; ++i) {
    if (!down[i]) {
      continue;
    }
    num += kRight - i;
    den += 1;
  }
  if (den == 0) {
    return 0;
  }
  return static_cast<int32_t>((static_cast<int64_t>(num) * kOne) / (den * kRight));
}

int settled_read(int pin) {
  pinMode(pin, INPUT);
  analogRead(pin);
  delay(2);
  return analogRead(pin);
}

int pulled_read(int pin, bool up) {
  analogRead(pin);
  if (up) {
    gpio_pullup_en(static_cast<gpio_num_t>(pin));
    gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
  } else {
    gpio_pullup_dis(static_cast<gpio_num_t>(pin));
    gpio_pulldown_en(static_cast<gpio_num_t>(pin));
  }
  delay(8);
  const int raw = analogRead(pin);
  gpio_pullup_dis(static_cast<gpio_num_t>(pin));
  gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
  return raw;
}

void probe_pin(int pin) {
  const int open = settled_read(pin);
  const int up = pulled_read(pin, true);
  const int down = pulled_read(pin, false);
  const char *state = "driven";
  if (up > 3000 && down < 400) {
    state = "open, nothing on this pin";
  } else if (open < 80 && up < 200) {
    state = "held at ground";
  } else if (open > 3900 && down > 3500) {
    state = "held at 3.3V";
  } else if (up - down > 800) {
    state = "weak, pull can move it";
  }
  Serial.printf("conn GPIO%d plain=%4d pullup=%4d pulldown=%4d %s\n", pin, open, up, down, state);
}

int read_pot(int32_t *angle) {
  const int raw = analogRead(kPotPin);
  smooth += (raw - smooth) >> 3;
  if (smooth < 0) {
    smooth = 0;
  } else if (smooth > 4095) {
    smooth = 4095;
  }
  // Top 30% of the wiper: 100% aims up, 70% aims forward. Below that stays forward.
  constexpr int kStart = 4095 * 70 / 100;
  constexpr int kTop = 75;
  constexpr int kFront = 15;
  int pos = smooth;
  if (pos < kStart) {
    pos = kStart;
  }
  const int deg = kFront + (pos - kStart) * (kTop - kFront) / (4095 - kStart);
  *angle = static_cast<int32_t>(deg) * kOne / 360;
  return raw;
}

}  // namespace

void input_begin() {
  analogReadResolution(12);
  Serial.println("pot connection check");
  const int pins[] = {0, 1, 2, 3, 4, 5};
  for (int pin : pins) {
    probe_pin(pin);
  }
  for (int i = 0; i < kPadCount; ++i) {
#if TOUCH_ACTIVE_HIGH
    pinMode(kPadPins[i], INPUT_PULLDOWN);
#else
    pinMode(kPadPins[i], INPUT_PULLUP);
#endif
    prev[i] = false;
  }
  pinMode(kPotPin, INPUT);
  analogReadResolution(12);
  analogRead(kPotPin);
  smooth = analogRead(kPotPin);
  sling = Sling::Idle;
  waitRelease = true;
}

void input_poll(PadInput *out, bool aim, uint32_t nowMs) {
  bool down[kPadCount];
  bool edge = false;
  bool any = false;
  for (int i = 0; i < kPadCount; ++i) {
    down[i] = pad_down(kPadPins[i]);
    if (down[i]) {
      any = true;
    }
    if (down[i] && !prev[i]) {
      edge = true;
    }
    prev[i] = down[i];
  }

  out->potRaw = read_pot(&out->angle);
  static uint32_t lastPotLog = 0;
  if (nowMs - lastPotLog >= 200) {
    lastPotLog = nowMs;
    Serial.printf("pot_raw=%4d\n", out->potRaw);
  }
  out->power = 0;
  out->drawing = false;
  out->launch = false;
  out->shake = false;
  out->padEdge = edge;

  if (!aim) {
    sling = Sling::Idle;
    waitRelease = true;
    lastPower = 0;
    return;
  }
  if (waitRelease) {
    if (!any) {
      waitRelease = false;
    }
    return;
  }

  const int32_t power = centroid(down);
  if (sling == Sling::Idle) {
    if (down[kRight]) {
      sling = Sling::Draw;
      drawStart = nowMs;
      lastPower = power;
    }
    return;
  }

  if (sling == Sling::Draw) {
    if (any) {
      lastPower = power;
      out->drawing = true;
      out->power = power;
      const uint32_t held = nowMs - drawStart;
      out->shake = held >= 1500;
      return;
    }
    sling = Sling::Release;
    releaseStart = nowMs;
  }

  if (sling == Sling::Release) {
    if (any) {
      sling = Sling::Draw;
      out->drawing = true;
      out->power = power;
      lastPower = power;
      out->shake = (nowMs - drawStart) >= 1500;
      return;
    }
    if (nowMs - releaseStart >= 40) {
      sling = Sling::Idle;
      if (lastPower > 0) {
        out->launch = true;
        out->power = lastPower;
      }
      lastPower = 0;
    }
  }
}
