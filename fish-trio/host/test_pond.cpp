#include <math.h>
#include <stdio.h>
#include <string.h>

#include "koi_model.h"
#include "pond_decor.h"
#include "pond_proto.h"
#include "pond_world.h"

namespace {

int gFailures = 0;

void expect(bool ok, const char *name) {
  if (ok) {
    printf("  ok  %s\n", name);
    return;
  }
  printf("FAIL  %s\n", name);
  ++gFailures;
}

void expect_near(float got, float want, float tol, const char *name) {
  if (fabsf(got - want) <= tol) {
    printf("  ok  %s\n", name);
    return;
  }
  printf("FAIL  %s (got %.4f want %.4f)\n", name, got, want);
  ++gFailures;
}

float vertical_half(float phase) {
  float max_abs = 0.f;
  const float bob = pond::pond_bob_mm(phase);
  for (float u = -0.02f; u <= 1.05f; u += 0.02f) {
    const float spine = bob + pond::koi_spine_lat(u, phase);
    for (float v = -6.6f; v <= 6.6f; v += 0.08f) {
      if (pond::koi_material_uv(u, v, phase) == pond::KoiMat::None) {
        continue;
      }
      const float y = fabsf(spine + v);
      if (y > max_abs) {
        max_abs = y;
      }
    }
  }
  return max_abs;
}

void test_layout() {
  const pond::PondWorld world = pond::pond_world();
  expect_near(world.tft.x1_mm - world.tft.x0_mm, pond::kTftWidthMm, 1e-4f, "tft width");
  expect_near(world.oled.x1_mm - world.oled.x0_mm, pond::kOledWidthMm, 1e-4f, "oled width");
  expect_near(world.eink.x1_mm - world.eink.x0_mm, pond::kEinkWidthMm, 1e-4f, "eink width");
  expect(world.tft.x1_mm < world.oled.x0_mm && world.oled.x1_mm < world.eink.x0_mm, "right-to-left order is tft, oled, eink");
  expect_near(world.oled.x0_mm - world.tft.x1_mm, GAP_OLED_TFT_MM, 1e-4f, "oled/tft gap matches the define");
  expect_near(world.eink.x0_mm - world.oled.x1_mm, GAP_EINK_OLED_MM, 1e-4f, "eink/oled gap matches the define");
  expect(GAP_EINK_OLED_MM == 9.0f && GAP_OLED_TFT_MM == 5.0f, "bezel defaults are 9 mm and 5 mm");
  expect(EINK_Y_OFFSET_MM == 0.0f && OLED_Y_OFFSET_MM == 0.0f && TFT_Y_OFFSET_MM == 0.0f, "y offsets default to zero");
}

void test_projection() {
  const pond::PondWorld world = pond::pond_world();
  const float mid = (world.eink.x0_mm + world.eink.x1_mm) * 0.5f;
  expect(pond::pond_screen_x(world.eink, mid) == 100, "eink center x is pixel 100");
  expect(pond::pond_screen_y(world.eink, 0.f) == 100, "eink y = 0 is pixel 100");
  pond::DisplaySpan shifted = world.eink;
  shifted.y_offset_mm = 2.f;
  const int lifted = pond::pond_screen_y(shifted, 0.f);
  expect(lifted < 100, "positive Y offset moves the centerline up");
  expect(abs(lifted - (100 - pond::pond_round(2.f * pond::kEinkPxPerMm))) <= 1, "2 mm offset is 2 mm of pixels");
  const int y5 = pond::pond_screen_y(world.oled, 5.f);
  const int y0 = pond::pond_screen_y(world.oled, 0.f);
  expect(y5 > 0 && y5 < y0, "+5 mm tick lands on the OLED");
  expect(pond::pond_screen_y(world.oled, 10.f) < 0, "+10 mm tick is above the OLED glass");
}

void test_bridge_and_reveal() {
  const pond::PondWorld world = pond::pond_world();
  const float nose_oled = world.oled.x1_mm;
  expect(nose_oled + pond::kKoiLengthMm > world.eink.x0_mm, "tail is still on the e-ink when the nose enters the OLED");
  const float nose_tft = world.tft.x1_mm;
  expect(nose_tft + pond::kKoiLengthMm > world.oled.x0_mm, "tail is still on the OLED when the nose enters the IPS");
  expect(pond::kKoiLengthMm > GAP_EINK_OLED_MM && pond::kKoiLengthMm > GAP_OLED_TFT_MM, "koi is longer than both bezels");

  expect(pond::pond_panel_view(false, world.oled.x1_mm + 0.2f, world.oled.x0_mm, world.oled.x1_mm) == pond::PanelView::Dark,
         "OLED stays dark until the nose crosses in");
  expect(pond::pond_panel_view(false, world.oled.x1_mm, world.oled.x0_mm, world.oled.x1_mm) == pond::PanelView::Swim,
         "OLED lights as the nose touches its right edge");
  const float exited = world.oled.x0_mm - pond::kKoiLengthMm - 0.05f;
  expect(pond::pond_panel_view(false, exited, world.oled.x0_mm, world.oled.x1_mm) == pond::PanelView::Dark,
         "OLED goes dark after the tail leaves");
  expect(pond::pond_panel_view(true, -100.f, world.tft.x0_mm, world.tft.x1_mm) == pond::PanelView::Cal,
         "calibration lights a panel with no fish");

  expect(pond::pond_panel_view(false, world.eink.x0_mm, world.oled.x0_mm, world.oled.x1_mm) == pond::PanelView::Dark,
         "the bezel gap keeps the OLED dark at the handoff instant");
  expect(pond::pond_panel_view(false, world.eink.x0_mm, world.eink.x0_mm, world.eink.x1_mm) == pond::PanelView::Swim,
         "e-ink is still lit at handoff because the tail has not left");
}

void test_speed_and_gaps() {
  const pond::PondWorld world = pond::pond_world();
  expect_near(pond::pond_speed_at(world.eink.x0_mm + 1.f, world), pond::kEinkSpeedMmS, 1e-4f, "speed on the e-ink");
  const float gap_mid = (world.eink.x0_mm + world.oled.x1_mm) * 0.5f;
  expect_near(pond::pond_speed_at(gap_mid, world), pond::kEinkSpeedMmS, 1e-4f, "bezel after the e-ink keeps e-ink speed");
  expect_near(pond::pond_speed_at(world.oled.x1_mm, world), pond::kOledSpeedMmS, 1e-4f, "speed picks up as the nose enters the OLED");
  const float second_gap = (world.oled.x0_mm + world.tft.x1_mm) * 0.5f;
  expect_near(pond::pond_speed_at(second_gap, world), pond::kOledSpeedMmS, 1e-4f, "bezel after the OLED keeps OLED speed");
  expect_near(pond::pond_speed_at(world.tft.x1_mm, world), pond::kTftSpeedMmS, 1e-4f, "speed picks up again on the IPS");
  expect(pond::kEinkSpeedMmS >= 18.f && pond::kEinkSpeedMmS <= 20.f, "e-ink speed is 18-20 mm/s");
  expect(pond::kOledSpeedMmS >= 26.f && pond::kOledSpeedMmS <= 28.f, "OLED speed is 26-28 mm/s");
  expect(pond::kTftSpeedMmS >= 32.f && pond::kTftSpeedMmS <= 36.f, "IPS speed is 32-36 mm/s");

  expect_near(pond::pond_travel_s(world.eink.x0_mm, world.oled.x1_mm, world), GAP_EINK_OLED_MM / pond::kEinkSpeedMmS, 1e-3f,
              "eink-to-OLED bezel transit");
  expect_near(pond::pond_travel_s(world.oled.x0_mm, world.tft.x1_mm, world), GAP_OLED_TFT_MM / pond::kOledSpeedMmS, 1e-3f,
              "OLED-to-IPS bezel transit");

  pond::PondSim sim;
  pond::pond_sim_reset(&sim, world.eink.x0_mm, 0.4f);
  const float want = pond::pond_travel_s(world.eink.x0_mm, world.oled.x1_mm, world);
  const int steps = static_cast<int>(want / 0.001f);
  for (int i = 0; i < steps; ++i) {
    pond::pond_sim_step(&sim, 0.001f, world);
  }
  expect_near(sim.x_nose_mm, world.oled.x1_mm, 0.05f, "1 ms steps land on the OLED edge after the bezel time");
  expect(sim.phase > 0.4f, "phase keeps advancing through the gap");

  const float done = pond::pond_handoff_to_done_s(world);
  expect(done > 1.f && done < 8.f, "handoff-to-done is a few seconds");
  expect(pond::pond_safety_timeout_s(world) > done + 1.f, "safety timeout outlasts the swim");
}

void test_pass_reveal() {
  const pond::PondWorld world = pond::pond_world();
  pond::PondSim sim;
  pond::pond_sim_reset(&sim, world.eink.x1_mm - 0.4f, 0.2f);
  bool saw_oled = false;
  bool saw_tft = false;
  bool oled_while_eink = false;
  bool tft_while_oled = false;
  bool oled_before_tft = false;
  int guard = 0;
  while (sim.x_nose_mm + pond::kKoiLengthMm >= world.tft.x0_mm && guard < 20000) {
    const bool eink = pond::pond_panel_view(false, sim.x_nose_mm, world.eink.x0_mm, world.eink.x1_mm) == pond::PanelView::Swim;
    const bool oled = pond::pond_panel_view(false, sim.x_nose_mm, world.oled.x0_mm, world.oled.x1_mm) == pond::PanelView::Swim;
    const bool tft = pond::pond_panel_view(false, sim.x_nose_mm, world.tft.x0_mm, world.tft.x1_mm) == pond::PanelView::Swim;
    if (oled && !saw_oled) {
      saw_oled = true;
      oled_before_tft = !saw_tft;
      oled_while_eink = eink;
    }
    if (tft && !saw_tft) {
      saw_tft = true;
      tft_while_oled = oled;
    }
    pond::pond_sim_step(&sim, 0.005f, world);
    ++guard;
  }
  expect(saw_oled && oled_before_tft, "OLED reveals before the IPS");
  expect(oled_while_eink, "OLED reveal still overlaps the e-ink");
  expect(saw_tft && tft_while_oled, "IPS reveal still overlaps the OLED");
  expect(pond::pond_panel_view(false, sim.x_nose_mm, world.oled.x0_mm, world.oled.x1_mm) == pond::PanelView::Dark,
         "OLED is dark once the koi has left the IPS");
  expect(pond::pond_panel_view(false, sim.x_nose_mm, world.tft.x0_mm, world.tft.x1_mm) == pond::PanelView::Dark,
         "IPS is dark after the tail exits");
}

void test_koi() {
  expect_near(pond::koi_half_width(0.34f), 2.70f, 1e-4f, "widest torso knot is 2.70 mm");
  expect_near(pond::koi_half_width(0.00f), 0.18f, 1e-4f, "snout half-width");
  expect_near(pond::koi_half_width(0.82f), 0.48f, 1e-4f, "peduncle half-width");
  float widest = 0.f;
  for (float u = 0.f; u <= 0.82f; u += 0.01f) {
    const float half = pond::koi_half_width(u);
    if (half > widest) {
      widest = half;
    }
  }
  expect_near(widest * 2.f, 5.40f, 0.02f, "widest torso is 5.4 mm");
  expect(pond::koi_wave_amp_mm(0.95f) > pond::koi_wave_amp_mm(0.15f), "the spine wave grows toward the tail");
  expect_near(pond::koi_wave_amp_mm(1.f), pond::kWaveTailAmpMm, 1e-3f, "tail amplitude is 1.55 mm");

  float lo = 20.f;
  float hi = 0.f;
  for (int i = 0; i < 36; ++i) {
    const float span = pond::koi_pectoral_span_mm(i * 0.2f);
    if (span < lo) lo = span;
    if (span > hi) hi = span;
  }
  expect(lo >= 9.49f && hi <= 9.81f, "pectoral span stays inside 9.5-9.8 mm");

  const float half = pond::koi_half_width(0.16f);
  const pond::KoiMat calico = pond::koi_material_uv(0.16f, 0.12f * half, 0.2f);
  expect(calico == pond::KoiMat::Calico || calico == pond::KoiMat::CalicoHi || calico == pond::KoiMat::CalicoShade,
         "first Kohaku patch is calico");
  const pond::KoiMat eye = pond::koi_material_uv(0.08f, 0.46f * pond::koi_half_width(0.08f), 0.2f);
  expect(eye == pond::KoiMat::Eye, "snout carries two dark eyes");
  expect(pond::koi_material_uv(0.4f, 6.4f, 0.2f) == pond::KoiMat::None, "water beside the koi is empty");

  float extent = 0.f;
  for (int i = 0; i < 12; ++i) {
    const float half_y = vertical_half(i * 0.45f);
    if (half_y > extent) {
      extent = half_y;
    }
  }
  const float limit = pond::kOledHeightMm * 0.5f - 0.05f;
  if (extent >= limit) {
    printf("  extent %.3f mm, OLED half %.3f mm\n", extent, pond::kOledHeightMm * 0.5f);
  }
  expect(extent < limit, "koi plus bob and weave stays inside the OLED");
}

void test_decor() {
  expect(pond::pond_pad_count(pond::DecorScreen::Eink) == 3, "e-ink has three lily pads");
  expect(pond::pond_pad_count(pond::DecorScreen::Oled) == 2, "OLED has two lily pads");
  expect(pond::pond_pad_count(pond::DecorScreen::Tft) == 3, "IPS has three lily pads");
  const pond::DecorScreen screens[] = {pond::DecorScreen::Eink, pond::DecorScreen::Oled, pond::DecorScreen::Tft};
  bool lane_clear = true;
  bool pad_solid = true;
  for (pond::DecorScreen screen : screens) {
    const pond::Pad *pads = pond::pond_pads(screen);
    const int count = pond::pond_pad_count(screen);
    if (!pond::pond_pad_hit(pads[0], pads[0].x_mm, pads[0].y_mm, 0.f, 0.f, nullptr)) {
      pad_solid = false;
    }
    for (float y = -5.f; y <= 5.f; y += 0.5f) {
      for (float x = -12.f; x <= 12.f; x += 0.5f) {
        for (int i = 0; i < count; ++i) {
          if (pond::pond_pad_hit(pads[i], x, y, 0.f, 0.f, nullptr)) {
            lane_clear = false;
          }
        }
        if (screen == pond::DecorScreen::Tft && pond::pond_stone_hit(x, y, nullptr)) {
          lane_clear = false;
        }
      }
    }
  }
  expect(pad_solid, "a pad center is solid");
  expect(lane_clear, "the center lane is free of pads and stones");
}

void test_ripples() {
  pond::RippleField field;
  pond::ripples_reset(&field);
  pond::ripples_advance(&field, 0.20f, 40.f, 0.2f, true);
  expect(pond::ripples_alive(field) >= 1, "a ripple spawns behind the tail");
  expect(field.items[0].live, "first slot is the new ripple");
  expect_near(pond::ripple_radius_mm(field.time_s - field.items[0].born_s), (field.time_s - field.items[0].born_s) * 13.5f, 0.05f,
              "ripple radius grows with age");
  pond::ripples_advance(&field, 1.0f, 30.f, 0.f, false);
  expect(pond::ripples_alive(field) == 0, "old ripples expire");
}

void test_protocol() {
  expect(sizeof(pond::Packet) == 28, "packet is 28 bytes");
  pond::Packet packet = {};
  packet.magic = pond::kMagic;
  packet.version = pond::kProtoVersion;
  packet.event = static_cast<uint8_t>(pond::Event::Handoff);
  packet.seq = 7;
  packet.y_mm = -0.35f;
  packet.phase = 1.25f;
  packet.speed_mm_s = pond::kEinkSpeedMmS;
  packet.x_nose_mm = 12.5f;
  packet.loop_index = 4;

  uint8_t bytes[sizeof(packet)];
  memcpy(bytes, &packet, sizeof(packet));
  pond::Packet out = {};
  expect(pond::parse_packet(bytes, sizeof(bytes), &out), "parse accepts a handoff");
  expect(out.seq == 7 && out.loop_index == 4, "seq and loop index survive");
  expect_near(out.y_mm, -0.35f, 1e-6f, "y_mm survives");
  expect_near(out.phase, 1.25f, 1e-6f, "phase survives");
  expect_near(out.x_nose_mm, 12.5f, 1e-6f, "x_nose survives");

  bytes[0] ^= 0xFF;
  expect(!pond::parse_packet(bytes, sizeof(bytes), &out), "wrong magic is rejected");
  expect(!pond::parse_packet(bytes, sizeof(bytes) - 1, &out), "short frame is rejected");

  pond::Dedup dedup = {};
  packet.magic = pond::kMagic;
  int accepted = 0;
  for (int i = 0; i < 3; ++i) {
    if (pond::dedup_accept(&dedup, packet)) {
      ++accepted;
    }
  }
  expect(accepted == 1, "three retries count as one event");
  packet.seq = 8;
  packet.event = static_cast<uint8_t>(pond::Event::CycleDone);
  expect(pond::dedup_accept(&dedup, packet), "a new seq is a new event");
  packet.magic = 0;
  expect(!pond::dedup_accept(&dedup, packet), "dedup rejects a bad packet");
}

}  // namespace

int main() {
  test_layout();
  test_projection();
  test_bridge_and_reveal();
  test_speed_and_gaps();
  test_pass_reveal();
  test_koi();
  test_decor();
  test_ripples();
  test_protocol();
  if (gFailures != 0) {
    printf("%d failed\n", gFailures);
    return 1;
  }
  printf("all pond checks passed\n");
  return 0;
}
