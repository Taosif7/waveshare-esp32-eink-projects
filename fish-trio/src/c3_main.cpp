#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2lib.h>
#include <driver/ledc.h>

#include <math.h>

#include "koi_model.h"
#include "pond_decor.h"
#include "pond_world.h"
#include "radio.h"

namespace {

constexpr int kOledSda = 20;
constexpr int kOledScl = 21;
constexpr int kTftSck = 4;
constexpr int kTftMosi = 6;
constexpr int kTftRst = 5;
constexpr int kTftDc = 7;
constexpr int kTftBlk = 10;
constexpr int kTftCs = -1;

constexpr uint16_t kEdge = pond::rgb565(0x24, 0x3B, 0x42);
constexpr uint16_t kDark = pond::rgb565(0x2E, 0x4B, 0x52);
constexpr uint16_t kMid = pond::rgb565(0x4A, 0x70, 0x7A);
constexpr uint16_t kBright = pond::rgb565(0x5D, 0x86, 0x8E);
constexpr uint16_t kShimmer = pond::rgb565(0x7E, 0xA4, 0xAA);
constexpr uint16_t kCream = pond::rgb565(0xF4, 0xF2, 0xEC);
constexpr uint16_t kCreamShade = pond::rgb565(0xD8, 0xD2, 0xC6);
constexpr uint16_t kOrange = pond::rgb565(0xE8, 0x53, 0x1E);
constexpr uint16_t kOrangeHi = pond::rgb565(0xF0, 0x6A, 0x28);
constexpr uint16_t kOrangeShade = pond::rgb565(0xB8, 0x38, 0x14);
constexpr uint16_t kInk = pond::rgb565(0x23, 0x23, 0x28);
constexpr uint16_t kFin = pond::rgb565(0xE4, 0xD8, 0xC6);
constexpr uint16_t kFinTip = pond::rgb565(0xC5, 0xD0, 0xC8);
constexpr uint16_t kShadow = pond::rgb565(0x1C, 0x32, 0x39);
constexpr uint16_t kPadOuter = pond::rgb565(0x58, 0x7A, 0x3C);
constexpr uint16_t kPadMid = pond::rgb565(0x76, 0x9A, 0x4C);
constexpr uint16_t kPadInner = pond::rgb565(0x94, 0xB4, 0x5E);
constexpr uint16_t kLotus = pond::rgb565(0xE8, 0xA8, 0xA0);
constexpr uint16_t kLotusHi = pond::rgb565(0xF4, 0xE4, 0xD4);
constexpr uint16_t kStoneA = pond::rgb565(0x5A, 0x4A, 0x3B);
constexpr uint16_t kStoneB = pond::rgb565(0x78, 0x65, 0x52);
constexpr uint16_t kStoneC = pond::rgb565(0x3D, 0x32, 0x28);

constexpr ledc_channel_t kBlChannel = LEDC_CHANNEL_0;
constexpr ledc_mode_t kBlMode = LEDC_LOW_SPEED_MODE;

SPIClass gSpi(FSPI);
Adafruit_ST7789 gTft(&gSpi, kTftCs, kTftDc, kTftRst);
U8G2_SH1106_128X64_NONAME_F_HW_I2C gOled(U8G2_R0, U8X8_PIN_NONE, kOledScl, kOledSda);

pond::PondWorld gWorld;
pond::PondSim gSim;
pond::RippleField gRipples;
bool gOledOk = false;
bool gActive = false;
bool gCalForced = false;
bool gOledWas = false;
bool gTftWas = false;
bool gTftFull = true;
bool gTftCalDrawn = false;
bool gOledCalDrawn = false;
int gDuty = 0;
uint32_t gBootDeadline = 0;
uint32_t gLastStepMs = 0;
uint32_t gLastOledMs = 0;
uint32_t gLastTftMs = 0;
uint32_t gLoopIndex = 0;

struct Rect {
  int x0;
  int y0;
  int x1;
  int y1;
  bool any;
};

Rect gPrev = {};
uint16_t gLine[pond::kTftPx];

bool calibrating() {
  if (gCalForced) {
    return true;
  }
  return static_cast<int32_t>(gBootDeadline - millis()) > 0;
}

void bl_init() {
  pinMode(kTftBlk, OUTPUT);
  digitalWrite(kTftBlk, LOW);
  ledc_timer_config_t timer = {};
  timer.speed_mode = kBlMode;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = 5000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);
  ledc_channel_config_t channel = {};
  channel.gpio_num = kTftBlk;
  channel.speed_mode = kBlMode;
  channel.channel = kBlChannel;
  channel.timer_sel = LEDC_TIMER_0;
  channel.duty = 0;
  channel.hpoint = 0;
  ledc_channel_config(&channel);
}

void bl_set(int duty) {
  if (duty < 0) {
    duty = 0;
  }
  if (duty > 255) {
    duty = 255;
  }
  gDuty = duty;
  ledc_set_duty(kBlMode, kBlChannel, static_cast<uint32_t>(duty));
  ledc_update_duty(kBlMode, kBlChannel);
}

bool probe_oled(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void oled_begin() {
  Wire.begin(kOledSda, kOledScl);
  Wire.setClock(100000);
  uint8_t addr = 0;
  for (int attempt = 0; attempt < 12 && addr == 0; ++attempt) {
    if (probe_oled(0x3C)) {
      addr = 0x3C;
    } else if (probe_oled(0x3D)) {
      addr = 0x3D;
    } else {
      delay(40);
    }
  }
  if (addr == 0) {
    Serial.println("No SH1106 at 0x3C/0x3D. SDA GPIO20, SCL GPIO21, VCC 3V3.");
    return;
  }
  gOled.setBusClock(400000);
  gOled.setI2CAddress(static_cast<uint8_t>(addr << 1));
  gOled.begin();
  gOled.setBusClock(400000);
  gOled.setPowerSave(1);
  gOledOk = true;
  Serial.printf("SH1106 at 0x%02X, 400 kHz\n", addr);
}

void tft_begin() {
  gSpi.begin(kTftSck, -1, kTftMosi, -1);
  gTft.init(240, 240, SPI_MODE3);
  gTft.setSPISpeed(pond::kTftSpiHz);
  gTft.setRotation(TFT_ROTATION);
  gTft.invertDisplay(true);
  gTft.fillScreen(0x0000);
  Serial.println("ST7789 GMT130 SPI_MODE3, CS tied low, 240x240 in 240x320 RAM");
}

void send_event(pond::Event event) {
  pond::Packet packet = {};
  packet.magic = pond::kMagic;
  packet.version = pond::kProtoVersion;
  packet.event = static_cast<uint8_t>(event);
  packet.seq = radio_take_seq();
  const pond::KoiPose pose = pond::koi_pose_from(gSim.x_nose_mm, gSim.phase);
  packet.y_mm = pose.y_mm;
  packet.phase = gSim.phase;
  packet.speed_mm_s = pond::pond_speed_at(gSim.x_nose_mm, gWorld);
  packet.x_nose_mm = gSim.x_nose_mm;
  packet.loop_index = gLoopIndex;
  radio_send(packet);
}

void on_packet(const pond::Packet &packet) {
  const auto event = static_cast<pond::Event>(packet.event);
  if (event == pond::Event::Handoff) {
    gSim.x_nose_mm = packet.x_nose_mm;
    gSim.phase = packet.phase;
    gLoopIndex = packet.loop_index;
    gActive = true;
    gLastStepMs = millis();
    pond::ripples_reset(&gRipples);
    Serial.printf("HANDOFF x %.2f y %.2f\n", packet.x_nose_mm, packet.y_mm);
  } else if (event == pond::Event::CalOn) {
    gCalForced = true;
    Serial.println("Calibration on");
  } else if (event == pond::Event::CalOff) {
    gCalForced = false;
    gBootDeadline = millis();
    Serial.println("Calibration off");
  } else if (event == pond::Event::PassReset) {
    gActive = false;
    Serial.println("Pass reset");
  }
}

void glass_of_tft(int lx, int ly, float *x_mm, float *y_mm, float *world_y) {
  const float phys_x = static_cast<float>(lx) * pond::kTftScale + 1.f;
  const float phys_y = static_cast<float>(ly) * pond::kTftScale + 1.f;
  *x_mm = phys_x / pond::kTftPxPerMm - pond::kTftWidthMm * 0.5f;
  *y_mm = (pond::kTftPx * 0.5f - phys_y) / pond::kTftPxPerMm;
  *world_y = *y_mm - gWorld.tft.y_offset_mm;
}

uint16_t fish_color(pond::KoiMat mat) {
  switch (mat) {
    case pond::KoiMat::Body:
      return kCream;
    case pond::KoiMat::BodyShade:
      return kCreamShade;
    case pond::KoiMat::Calico:
      return kOrange;
    case pond::KoiMat::CalicoHi:
      return kOrangeHi;
    case pond::KoiMat::CalicoShade:
      return kOrangeShade;
    case pond::KoiMat::Eye:
    case pond::KoiMat::Fleck:
    case pond::KoiMat::Outline:
      return kInk;
    case pond::KoiMat::Fin:
      return kFin;
    case pond::KoiMat::FinTip:
      return kFinTip;
    default:
      return kInk;
  }
}

uint16_t pad_color(int ring) {
  if (ring <= 0) {
    return kPadOuter;
  }
  if (ring == 1) {
    return kPadMid;
  }
  return kPadInner;
}

uint16_t stone_color(int tone) {
  if (tone <= 0) {
    return kStoneA;
  }
  if (tone == 1) {
    return kStoneB;
  }
  return kStoneC;
}

bool ripple_here(float x_mm, float y_mm, float px_per_mm) {
  const float thickness = 0.55f / px_per_mm + 0.12f;
  for (int i = 0; i < 4; ++i) {
    const pond::RippleField::Item &item = gRipples.items[i];
    if (!item.live) {
      continue;
    }
    const float age = gRipples.time_s - item.born_s;
    const float radius = pond::ripple_radius_mm(age);
    const float dx = x_mm - item.x_mm;
    const float dy = y_mm - item.y_mm;
    const float dist = sqrtf(dx * dx + dy * dy);
    if (fabsf(dist - radius) > thickness) {
      continue;
    }
    const int bucket = static_cast<int>(floorf(atan2f(dy, dx) * 8.f));
    if ((bucket & 1) == 0) {
      return true;
    }
  }
  return false;
}

uint16_t tft_pixel(int lx, int ly) {
  float glass_x = 0.f;
  float glass_y = 0.f;
  float world_y = 0.f;
  glass_of_tft(lx, ly, &glass_x, &glass_y, &world_y);
  const float world_x = gWorld.tft.x0_mm + (static_cast<float>(lx) * pond::kTftScale + 1.f) / pond::kTftPxPerMm;

  const float ny = (static_cast<float>(ly) + 0.5f) / pond::kTftLogical;
  const float edge = fabsf(ny - 0.5f) * 2.f;
  uint16_t color = kBright;
  if (edge > 0.78f) {
    color = kEdge;
  } else if (edge > 0.56f) {
    color = kDark;
  } else if (edge > 0.28f) {
    color = kMid;
  }
  const uint32_t shimmer = static_cast<uint32_t>(lx) * 374761393u ^ static_cast<uint32_t>(ly) * 668265263u;
  if (edge < 0.5f && (shimmer & 0x7F) == 0) {
    color = kShimmer;
  }

  int tone = 0;
  if (pond::pond_stone_hit(glass_x, glass_y, &tone)) {
    color = stone_color(tone);
  }

  const float now_s = millis() * 0.001f;
  const pond::Pad *pads = pond::pond_pads(pond::DecorScreen::Tft);
  const int pad_count = pond::pond_pad_count(pond::DecorScreen::Tft);
  for (int i = 0; i < pad_count; ++i) {
    float dx = 0.f;
    float dy = 0.f;
    pond::pond_pad_drift(pads[i], now_s, 2.f, pond::kTftPxPerMm, &dx, &dy);
    int ring = 0;
    if (!pond::pond_pad_hit(pads[i], glass_x, glass_y, dx, dy, &ring)) {
      continue;
    }
    color = pad_color(ring);
    if (pads[i].lotus) {
      const float lx_mm = glass_x - (pads[i].x_mm + dx);
      const float ly_mm = glass_y - (pads[i].y_mm + dy);
      const float dist = sqrtf(lx_mm * lx_mm + ly_mm * ly_mm);
      if (dist < 0.62f) {
        color = dist < 0.30f ? kLotusHi : kLotus;
      }
    }
  }

  if (ripple_here(world_x, world_y, pond::kTftPxPerMm)) {
    color = kShimmer;
  }

  if (gActive) {
    const pond::KoiPose pose = pond::koi_pose_from(gSim.x_nose_mm, gSim.phase);
    const pond::KoiPose shadow = pond::koi_shadow_pose(pose);
    if (pond::koi_material_world(shadow, world_x, world_y) != pond::KoiMat::None) {
      color = kShadow;
    }
    const pond::KoiMat mat = pond::koi_material_world(pose, world_x, world_y);
    if (mat != pond::KoiMat::None) {
      color = fish_color(mat);
    }
  }
  return color;
}

void rect_include(Rect *rect, int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > pond::kTftLogical) x1 = pond::kTftLogical;
  if (y1 > pond::kTftLogical) y1 = pond::kTftLogical;
  if (x1 <= x0 || y1 <= y0) {
    return;
  }
  if (!rect->any) {
    rect->x0 = x0;
    rect->y0 = y0;
    rect->x1 = x1;
    rect->y1 = y1;
    rect->any = true;
    return;
  }
  if (x0 < rect->x0) rect->x0 = x0;
  if (y0 < rect->y0) rect->y0 = y0;
  if (x1 > rect->x1) rect->x1 = x1;
  if (y1 > rect->y1) rect->y1 = y1;
}

void rect_union(Rect *rect, const Rect &other) {
  if (!other.any) {
    return;
  }
  rect_include(rect, other.x0, other.y0, other.x1, other.y1);
}

int logical_x(float x_mm) {
  const float phys = (x_mm - gWorld.tft.x0_mm) * pond::kTftPxPerMm;
  return static_cast<int>(floorf(phys / pond::kTftScale));
}

int logical_y_world(float y_mm) {
  const float screen = gWorld.tft.px_h * 0.5f - (y_mm + gWorld.tft.y_offset_mm) * pond::kTftPxPerMm;
  return static_cast<int>(floorf(screen / pond::kTftScale));
}

Rect fish_rect() {
  Rect rect = {};
  if (!gActive) {
    return rect;
  }
  const pond::KoiPose pose = pond::koi_pose_from(gSim.x_nose_mm, gSim.phase);
  const float x0 = pose.x_nose_mm - 1.2f;
  const float x1 = pose.x_nose_mm + pond::kKoiLengthMm + 2.2f;
  const float y0 = pose.y_mm - 7.2f;
  const float y1 = pose.y_mm + 7.2f;
  rect_include(&rect, logical_x(x0), logical_y_world(y1), logical_x(x1) + 1, logical_y_world(y0) + 1);
  rect_include(&rect, logical_x(x0 + pond::kShadowDxMm), logical_y_world(y1 + pond::kShadowDyMm),
               logical_x(x1 + pond::kShadowDxMm) + 1, logical_y_world(y0 + pond::kShadowDyMm) + 1);
  return rect;
}

Rect decor_rect() {
  Rect rect = {};
  const pond::Pad *pads = pond::pond_pads(pond::DecorScreen::Tft);
  const int count = pond::pond_pad_count(pond::DecorScreen::Tft);
  for (int i = 0; i < count; ++i) {
    const float cx = gWorld.tft.x0_mm + pond::kTftWidthMm * 0.5f + pads[i].x_mm;
    const float cy = pads[i].y_mm;
    const float r = pads[i].r_mm + 0.6f;
    rect_include(&rect, logical_x(cx - r), logical_y_world(cy + r), logical_x(cx + r) + 1, logical_y_world(cy - r) + 1);
  }
  const pond::Stone *stones = pond::pond_stones();
  for (int i = 0; i < pond::pond_stone_count(); ++i) {
    const float cx = gWorld.tft.x0_mm + pond::kTftWidthMm * 0.5f + stones[i].x_mm;
    const float cy = stones[i].y_mm;
    const float r = stones[i].r_mm + 0.3f;
    rect_include(&rect, logical_x(cx - r), logical_y_world(cy + r), logical_x(cx + r) + 1, logical_y_world(cy - r) + 1);
  }
  for (int i = 0; i < 4; ++i) {
    const pond::RippleField::Item &item = gRipples.items[i];
    if (!item.live) {
      continue;
    }
    const float radius = pond::ripple_radius_mm(gRipples.time_s - item.born_s) + 0.8f;
    rect_include(&rect, logical_x(item.x_mm - radius), logical_y_world(item.y_mm + radius),
                 logical_x(item.x_mm + radius) + 1, logical_y_world(item.y_mm - radius) + 1);
  }
  return rect;
}

void blit_rect(const Rect &rect) {
  if (!rect.any) {
    return;
  }
  gTft.startWrite();
  for (int ly = rect.y0; ly < rect.y1; ++ly) {
    int count = 0;
    for (int lx = rect.x0; lx < rect.x1; ++lx) {
      const uint16_t color = tft_pixel(lx, ly);
      gLine[count++] = color;
      gLine[count++] = color;
    }
    const int width = (rect.x1 - rect.x0) * pond::kTftScale;
    gTft.setAddrWindow(static_cast<uint16_t>(rect.x0 * pond::kTftScale),
                       static_cast<uint16_t>(ly * pond::kTftScale), static_cast<uint16_t>(width),
                       static_cast<uint16_t>(pond::kTftScale));
    gTft.writePixels(gLine, static_cast<uint32_t>(width));
    gTft.writePixels(gLine, static_cast<uint32_t>(width));
  }
  gTft.endWrite();
}

void draw_tft_pond() {
  Rect dirty = {};
  if (gTftFull) {
    rect_include(&dirty, 0, 0, pond::kTftLogical, pond::kTftLogical);
    gTftFull = false;
  } else {
    rect_union(&dirty, fish_rect());
    rect_union(&dirty, decor_rect());
    rect_union(&dirty, gPrev);
  }
  blit_rect(dirty);
  gPrev = fish_rect();
  rect_union(&gPrev, decor_rect());
}

void draw_tft_cal() {
  gTft.fillScreen(0x0000);
  const int y0 = pond::pond_screen_y(gWorld.tft, 0.f);
  gTft.drawFastHLine(0, y0, pond::kTftPx, 0xFFFF);
  const float marks[] = {10.f, 5.f, -5.f, -10.f};
  for (float mark : marks) {
    if (fabsf(mark) > gWorld.tft.height_mm * 0.5f) {
      continue;
    }
    const int y = pond::pond_screen_y(gWorld.tft, mark);
    const int half = fabsf(mark) > 7.f ? 16 : 9;
    gTft.drawFastVLine(pond::kTftPx / 2, y - half, half * 2, 0xFFFF);
  }
  gTft.setTextColor(0xFFFF);
  gTft.setTextSize(2);
  gTft.setCursor(8, 8);
  gTft.print("IPS");
  gTftFull = true;
  gPrev.any = false;
}

bool oled_lit(pond::KoiMat mat) {
  switch (mat) {
    case pond::KoiMat::Body:
    case pond::KoiMat::BodyShade:
    case pond::KoiMat::Fin:
    case pond::KoiMat::FinTip:
    case pond::KoiMat::Outline:
      return true;
    default:
      return false;
  }
}

void draw_oled_pads() {
  const float now_s = millis() * 0.001f;
  const pond::Pad *pads = pond::pond_pads(pond::DecorScreen::Oled);
  const int count = pond::pond_pad_count(pond::DecorScreen::Oled);
  for (int i = 0; i < count; ++i) {
    float dx = 0.f;
    float dy = 0.f;
    pond::pond_pad_drift(pads[i], now_s, 1.5f, pond::kOledPxPerMm, &dx, &dy);
    const int samples = static_cast<int>(pads[i].r_mm * pond::kOledPxPerMm * 4.f);
    for (int s = 0; s < samples; ++s) {
      const float angle = 2.f * pond::kPi * static_cast<float>(s) / static_cast<float>(samples);
      if (fabsf(pond::pond_wrap_pi(angle - pads[i].notch)) < 0.55f) {
        continue;
      }
      const float gx = pads[i].x_mm + dx + cosf(angle) * pads[i].r_mm;
      const float gy = pads[i].y_mm + dy + sinf(angle) * pads[i].r_mm;
      const int sx = pond::pond_round(pond::kOledPxW * 0.5f + gx * pond::kOledPxPerMm);
      const int sy = pond::pond_round(pond::kOledPxH * 0.5f - gy * pond::kOledPxPerMm);
      gOled.drawPixel(sx, sy);
      const float gx2 = pads[i].x_mm + dx + cosf(angle) * (pads[i].r_mm - 0.28f);
      const float gy2 = pads[i].y_mm + dy + sinf(angle) * (pads[i].r_mm - 0.28f);
      gOled.drawPixel(pond::pond_round(pond::kOledPxW * 0.5f + gx2 * pond::kOledPxPerMm),
                      pond::pond_round(pond::kOledPxH * 0.5f - gy2 * pond::kOledPxPerMm));
    }
  }
}

void draw_oled_ripples() {
  for (int i = 0; i < 4; ++i) {
    const pond::RippleField::Item &item = gRipples.items[i];
    if (!item.live) {
      continue;
    }
    const float radius = pond::ripple_radius_mm(gRipples.time_s - item.born_s);
    const int samples = 28;
    for (int s = 0; s < samples; s += 2) {
      const float angle = 2.f * pond::kPi * static_cast<float>(s) / samples;
      const float x = item.x_mm + cosf(angle) * radius;
      const float y = item.y_mm + sinf(angle) * radius;
      const int sx = pond::pond_screen_x(gWorld.oled, x);
      const int sy = pond::pond_screen_y(gWorld.oled, y);
      gOled.drawPixel(sx, sy);
    }
  }
}

void draw_oled_koi() {
  const pond::KoiPose pose = pond::koi_pose_from(gSim.x_nose_mm, gSim.phase);
  const float step = 1.f / pond::kOledPxPerMm;
  for (float y = pose.y_mm - 6.9f; y <= pose.y_mm + 6.9f; y += step) {
    for (float x = pose.x_nose_mm - 0.8f; x <= pose.x_nose_mm + pond::kKoiLengthMm + 1.4f; x += step) {
      if (!oled_lit(pond::koi_material_world(pose, x, y))) {
        continue;
      }
      const int sx = pond::pond_screen_x(gWorld.oled, x);
      const int sy = pond::pond_screen_y(gWorld.oled, y);
      gOled.drawPixel(sx, sy);
    }
  }
}

void draw_oled_cal() {
  gOled.clearBuffer();
  const int y0 = pond::pond_screen_y(gWorld.oled, 0.f);
  gOled.drawHLine(0, y0, pond::kOledPxW);
  const int y5 = pond::pond_screen_y(gWorld.oled, 5.f);
  const int ym5 = pond::pond_screen_y(gWorld.oled, -5.f);
  gOled.drawVLine(pond::kOledPxW / 2, y5 - 4, 8);
  gOled.drawVLine(pond::kOledPxW / 2, ym5 - 4, 8);
  gOled.setFont(u8g2_font_5x7_tf);
  gOled.drawStr(2, 8, "OLED");
  gOled.sendBuffer();
}

void draw_oled_pond() {
  gOled.clearBuffer();
  draw_oled_pads();
  draw_oled_ripples();
  if (gActive) {
    draw_oled_koi();
  }
  gOled.sendBuffer();
}

void oled_power(bool on) {
  if (!gOledOk) {
    return;
  }
  if (!on) {
    if (gOledWas) {
      gOled.clearBuffer();
      gOled.sendBuffer();
      gOled.setPowerSave(1);
      gOledWas = false;
    }
    return;
  }
  if (!gOledWas) {
    gOled.setPowerSave(0);
    gOledWas = true;
  }
}

void tft_power(bool on) {
  if (!on) {
    if (gTftWas) {
      gTft.fillScreen(0x0000);
      bl_set(0);
      gTftWas = false;
      gTftFull = true;
      gPrev.any = false;
    }
    return;
  }
  if (!gTftWas) {
    gTftWas = true;
    gTftFull = true;
  }
  if (gDuty < pond::kTftBacklightDuty) {
    int next = gDuty + 18;
    if (next > pond::kTftBacklightDuty) {
      next = pond::kTftBacklightDuty;
    }
    bl_set(next);
  }
}

void step_sim() {
  const uint32_t now = millis();
  float dt = static_cast<float>(now - gLastStepMs) / 1000.f;
  gLastStepMs = now;
  if (dt < 0.f) {
    dt = 0.f;
  }
  if (dt > 0.05f) {
    dt = 0.05f;
  }
  pond::pond_sim_step(&gSim, dt, gWorld);
  float tail_x = 0.f;
  float tail_y = 0.f;
  const pond::KoiPose pose = pond::koi_pose_from(gSim.x_nose_mm, gSim.phase);
  pond::koi_tail_point(pose, &tail_x, &tail_y);
  const bool on_glass = pond::pond_panel_view(false, gSim.x_nose_mm, gWorld.oled.x0_mm, gWorld.oled.x1_mm) ==
                            pond::PanelView::Swim ||
                        pond::pond_panel_view(false, gSim.x_nose_mm, gWorld.tft.x0_mm, gWorld.tft.x1_mm) ==
                            pond::PanelView::Swim;
  pond::ripples_advance(&gRipples, dt, tail_x, tail_y, on_glass);
  if (gSim.x_nose_mm + pond::kKoiLengthMm < gWorld.tft.x0_mm) {
    gActive = false;
    send_event(pond::Event::CycleDone);
    Serial.println("CYCLE_DONE");
  }
}

}  // namespace

void setup() {
  bl_init();
  Serial.begin(115200);
  delay(200);
  Serial.println("fish-trio OLED + IPS");
  gWorld = pond::pond_world();
  radio_begin();
  tft_begin();
  oled_begin();
  gBootDeadline = millis() + pond::kCalBootMs;
  gLastStepMs = millis();
  Serial.println("Centerline for 5 s, then dark until the koi arrives.");
}

void loop() {
  radio_poll(on_packet);
  const bool cal = calibrating();
  if (cal) {
    gLastStepMs = millis();
  } else if (gActive) {
    step_sim();
  }

  const bool oled_swim =
      gActive && pond::pond_panel_view(false, gSim.x_nose_mm, gWorld.oled.x0_mm, gWorld.oled.x1_mm) == pond::PanelView::Swim;
  const bool tft_swim =
      gActive && pond::pond_panel_view(false, gSim.x_nose_mm, gWorld.tft.x0_mm, gWorld.tft.x1_mm) == pond::PanelView::Swim;
  const bool oled_on = cal || oled_swim;
  const bool tft_on = cal || tft_swim;
  oled_power(oled_on);
  tft_power(tft_on);

  if (!cal) {
    gTftCalDrawn = false;
    gOledCalDrawn = false;
  }
  const uint32_t now = millis();
  bool drew = false;
  if (tft_on && (cal ? !gTftCalDrawn : now - gLastTftMs >= pond::kTftFrameMs)) {
    if (cal) {
      draw_tft_cal();
      gTftCalDrawn = true;
    } else {
      gTftCalDrawn = false;
      draw_tft_pond();
    }
    gLastTftMs = millis();
    drew = true;
  }
  if (oled_on && gOledOk && (cal ? !gOledCalDrawn : now - gLastOledMs >= pond::kOledFrameMs)) {
    if (cal) {
      draw_oled_cal();
      gOledCalDrawn = true;
    } else {
      gOledCalDrawn = false;
      draw_oled_pond();
    }
    gLastOledMs = millis();
    drew = true;
  }
  if (!drew) {
    delay(2);
  }
}
