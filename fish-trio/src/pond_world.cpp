#include "pond_world.h"

#include <math.h>

namespace pond {
namespace {

float clampf(float value, float lo, float hi) {
  if (value < lo) {
    return lo;
  }
  if (value > hi) {
    return hi;
  }
  return value;
}

}  // namespace

PondWorld pond_world() {
  PondWorld world = {};
  world.tft.x0_mm = 0.f;
  world.tft.x1_mm = kTftWidthMm;
  world.tft.height_mm = kTftHeightMm;
  world.tft.px_per_mm = kTftPxPerMm;
  world.tft.y_offset_mm = TFT_Y_OFFSET_MM;
  world.tft.px_w = kTftPx;
  world.tft.px_h = kTftPx;

  world.oled.x0_mm = world.tft.x1_mm + GAP_OLED_TFT_MM;
  world.oled.x1_mm = world.oled.x0_mm + kOledWidthMm;
  world.oled.height_mm = kOledHeightMm;
  world.oled.px_per_mm = kOledPxPerMm;
  world.oled.y_offset_mm = OLED_Y_OFFSET_MM;
  world.oled.px_w = kOledPxW;
  world.oled.px_h = kOledPxH;

  world.eink.x0_mm = world.oled.x1_mm + GAP_EINK_OLED_MM;
  world.eink.x1_mm = world.eink.x0_mm + kEinkWidthMm;
  world.eink.height_mm = kEinkHeightMm;
  world.eink.px_per_mm = kEinkPxPerMm;
  world.eink.y_offset_mm = EINK_Y_OFFSET_MM;
  world.eink.px_w = kEinkPx;
  world.eink.px_h = kEinkPx;
  return world;
}

float pond_speed_at(float x_nose_mm, const PondWorld &world) {
  if (x_nose_mm > world.oled.x1_mm) {
    return kEinkSpeedMmS;
  }
  if (x_nose_mm > world.tft.x1_mm) {
    return kOledSpeedMmS;
  }
  return kTftSpeedMmS;
}

float pond_next_bound(float x_nose_mm, const PondWorld &world) {
  float bound = -100000.f;
  if (world.oled.x1_mm < x_nose_mm - 1e-5f && world.oled.x1_mm > bound) {
    bound = world.oled.x1_mm;
  }
  if (world.tft.x1_mm < x_nose_mm - 1e-5f && world.tft.x1_mm > bound) {
    bound = world.tft.x1_mm;
  }
  return bound;
}

float pond_travel_s(float x_from_mm, float x_to_mm, const PondWorld &world) {
  if (x_to_mm >= x_from_mm) {
    return 0.f;
  }
  float time_s = 0.f;
  float x = x_from_mm;
  for (int guard = 0; guard < 8 && x > x_to_mm + 1e-4f; ++guard) {
    float next = pond_next_bound(x, world);
    if (next < x_to_mm) {
      next = x_to_mm;
    }
    const float mid = (x + next) * 0.5f;
    const float speed = pond_speed_at(mid, world);
    time_s += (x - next) / speed;
    x = next;
  }
  return time_s;
}

float pond_phase_rate(float speed_mm_s) {
  return speed_mm_s * (2.f * kPi) / (kKoiLengthMm * 0.90f);
}

float pond_bob_mm(float phase) { return kBobMm * sinf(phase * 0.22f); }

float pond_handoff_to_done_s(const PondWorld &world) {
  return pond_travel_s(world.eink.x0_mm, world.tft.x0_mm - kKoiLengthMm, world);
}

float pond_safety_timeout_s(const PondWorld &world) {
  return pond_handoff_to_done_s(world) * 1.8f + 1.5f;
}

void pond_sim_reset(PondSim *sim, float x_nose_mm, float phase) {
  sim->x_nose_mm = x_nose_mm;
  sim->phase = phase;
}

void pond_sim_step(PondSim *sim, float dt_s, const PondWorld &world) {
  if (dt_s < 0.f) {
    return;
  }
  float left = dt_s;
  for (int i = 0; i < 8 && left > 1e-5f; ++i) {
    const float speed = pond_speed_at(sim->x_nose_mm, world);
    const float bound = pond_next_bound(sim->x_nose_mm, world);
    float room = sim->x_nose_mm - bound;
    if (room < 0.f) {
      room = 0.f;
    }
    const float dt_room = room / speed;
    const float step = left < dt_room ? left : dt_room;
    if (step <= 1e-6f) {
      sim->x_nose_mm -= 1e-4f;
      left -= 1e-4f / speed;
      continue;
    }
    sim->x_nose_mm -= speed * step;
    sim->phase += pond_phase_rate(speed) * step;
    left -= step;
  }
}

PanelView pond_panel_view(bool calibrating, float x_nose_mm, float x0_mm, float x1_mm) {
  if (calibrating) {
    return PanelView::Cal;
  }
  const float tail = x_nose_mm + kKoiLengthMm;
  if (x_nose_mm <= x1_mm && tail >= x0_mm) {
    return PanelView::Swim;
  }
  return PanelView::Dark;
}

int pond_round(float value) {
  return static_cast<int>(value >= 0.f ? value + 0.5f : value - 0.5f);
}

int pond_screen_x(const DisplaySpan &span, float x_mm) {
  return pond_round((x_mm - span.x0_mm) * span.px_per_mm);
}

int pond_screen_y(const DisplaySpan &span, float y_mm) {
  return pond_round(span.px_h * 0.5f - (y_mm + span.y_offset_mm) * span.px_per_mm);
}

void ripples_reset(RippleField *field) {
  *field = RippleField{};
  field->next_s = 0.12f;
}

void ripples_advance(RippleField *field, float dt_s, float tail_x_mm, float tail_y_mm, bool spawn) {
  if (dt_s > 0.f) {
    field->time_s += dt_s;
  }
  for (int i = 0; i < 4; ++i) {
    RippleField::Item &item = field->items[i];
    if (item.live && field->time_s - item.born_s > 0.48f) {
      item.live = false;
    }
  }
  if (!spawn || field->time_s + 1e-4f < field->next_s) {
    return;
  }
  field->next_s += 0.16f;
  RippleField::Item &slot = field->items[field->cursor];
  field->cursor = (field->cursor + 1) % 4;
  slot.live = true;
  slot.born_s = field->time_s;
  slot.x_mm = tail_x_mm;
  slot.y_mm = tail_y_mm;
}

float ripple_radius_mm(float age_s) {
  if (age_s <= 0.f) {
    return 0.f;
  }
  return clampf(age_s * 13.5f, 0.f, 6.5f);
}

int ripples_alive(const RippleField &field) {
  int count = 0;
  for (int i = 0; i < 4; ++i) {
    if (field.items[i].live) {
      ++count;
    }
  }
  return count;
}

}  // namespace pond
