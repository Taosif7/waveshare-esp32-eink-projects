#pragma once

#include "pond_config.h"

namespace pond {

struct DisplaySpan {
  float x0_mm;
  float x1_mm;
  float height_mm;
  float px_per_mm;
  float y_offset_mm;
  int px_w;
  int px_h;
};

struct PondWorld {
  DisplaySpan tft;
  DisplaySpan oled;
  DisplaySpan eink;
};

enum class PanelView : uint8_t { Dark = 0, Swim, Cal };

struct PondSim {
  float x_nose_mm;
  float phase;
};

struct RippleField {
  struct Item {
    float x_mm;
    float y_mm;
    float born_s;
    bool live;
  } items[4];
  float time_s;
  float next_s;
  int cursor;
};

PondWorld pond_world();

float pond_speed_at(float x_nose_mm, const PondWorld &world);
float pond_next_bound(float x_nose_mm, const PondWorld &world);
float pond_travel_s(float x_from_mm, float x_to_mm, const PondWorld &world);
float pond_phase_rate(float speed_mm_s);
float pond_bob_mm(float phase);
float pond_handoff_to_done_s(const PondWorld &world);
float pond_safety_timeout_s(const PondWorld &world);

void pond_sim_reset(PondSim *sim, float x_nose_mm, float phase);
void pond_sim_step(PondSim *sim, float dt_s, const PondWorld &world);

PanelView pond_panel_view(bool calibrating, float x_nose_mm, float x0_mm, float x1_mm);

int pond_round(float value);
int pond_screen_x(const DisplaySpan &span, float x_mm);
int pond_screen_y(const DisplaySpan &span, float y_mm);

void ripples_reset(RippleField *field);
void ripples_advance(RippleField *field, float dt_s, float tail_x_mm, float tail_y_mm, bool spawn);
float ripple_radius_mm(float age_s);
int ripples_alive(const RippleField &field);

}  // namespace pond
