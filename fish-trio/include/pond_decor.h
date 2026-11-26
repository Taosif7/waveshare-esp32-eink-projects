#pragma once

#include <math.h>

#include "pond_config.h"

namespace pond {

struct Pad {
  float x_mm;
  float y_mm;
  float r_mm;
  float notch;
  float anim;
  bool lotus;
};

struct Stone {
  float x_mm;
  float y_mm;
  float r_mm;
  uint8_t tone;
};

enum class DecorScreen : uint8_t { Eink = 0, Oled, Tft };

inline float pond_wrap_pi(float angle) {
  while (angle > kPi) {
    angle -= 2.f * kPi;
  }
  while (angle < -kPi) {
    angle += 2.f * kPi;
  }
  return angle;
}

inline int pond_pad_count(DecorScreen screen) {
  switch (screen) {
    case DecorScreen::Eink:
      return 3;
    case DecorScreen::Oled:
      return 2;
    case DecorScreen::Tft:
      return 3;
  }
  return 0;
}

inline const Pad *pond_pads(DecorScreen screen) {
  static const Pad kEink[] = {
      {-8.5f, 11.2f, 4.0f, -kPi * 0.5f, 0.4f, false},
      {9.5f, 11.6f, 3.6f, -kPi * 0.5f, 1.7f, false},
      {7.5f, -11.4f, 3.8f, kPi * 0.5f, 2.8f, false},
  };
  static const Pad kOled[] = {
      {-10.0f, 7.9f, 2.6f, -kPi * 0.5f, 0.6f, false},
      {9.0f, -7.7f, 2.4f, kPi * 0.5f, 2.1f, false},
  };
  static const Pad kTft[] = {
      {-7.2f, 9.0f, 3.3f, -kPi * 0.5f, 0.2f, true},
      {8.2f, -8.8f, 3.1f, kPi * 0.5f, 1.4f, false},
      {-8.6f, -8.4f, 2.5f, kPi * 0.5f, 2.6f, false},
  };
  switch (screen) {
    case DecorScreen::Eink:
      return kEink;
    case DecorScreen::Oled:
      return kOled;
    case DecorScreen::Tft:
      return kTft;
  }
  return kEink;
}

inline int pond_stone_count() { return 4; }

inline const Stone *pond_stones() {
  static const Stone kStones[] = {
      {10.2f, 10.0f, 1.8f, 0},
      {8.4f, 10.5f, 1.25f, 1},
      {-10.0f, -9.6f, 1.7f, 2},
      {10.3f, -8.0f, 1.15f, 1},
  };
  return kStones;
}

// Glass coordinates: origin at the panel center, +x right, +y up.
// dx/dy are the animated drift in millimeters.
inline bool pond_pad_hit(const Pad &pad, float x_mm, float y_mm, float dx_mm, float dy_mm, int *ring) {
  const float lx = x_mm - (pad.x_mm + dx_mm);
  const float ly = y_mm - (pad.y_mm + dy_mm);
  const float d2 = lx * lx + ly * ly;
  const float r = pad.r_mm;
  if (d2 > r * r) {
    return false;
  }
  const float da = pond_wrap_pi(atan2f(ly, lx) - pad.notch);
  if (fabsf(da) < 0.55f && d2 > (r * 0.42f) * (r * 0.42f)) {
    return false;
  }
  if (ring != nullptr) {
    if (d2 > (r * 0.72f) * (r * 0.72f)) {
      *ring = 0;
    } else if (d2 > (r * 0.40f) * (r * 0.40f)) {
      *ring = 1;
    } else {
      *ring = 2;
    }
  }
  return true;
}

inline void pond_pad_drift(const Pad &pad, float time_s, float amp_px, float px_per_mm, float *dx_mm, float *dy_mm) {
  const float amp_mm = (px_per_mm > 0.f) ? (amp_px / px_per_mm) : 0.f;
  *dx_mm = sinf(time_s * 0.55f + pad.anim) * amp_mm;
  *dy_mm = cosf(time_s * 0.38f + pad.anim * 1.3f) * amp_mm;
}

inline bool pond_stone_hit(float x_mm, float y_mm, int *tone) {
  const Stone *stones = pond_stones();
  for (int i = 0; i < pond_stone_count(); ++i) {
    const float dx = x_mm - stones[i].x_mm;
    const float dy = y_mm - stones[i].y_mm;
    if (dx * dx + dy * dy <= stones[i].r_mm * stones[i].r_mm) {
      if (tone != nullptr) {
        *tone = stones[i].tone;
      }
      return true;
    }
  }
  return false;
}

}  // namespace pond
