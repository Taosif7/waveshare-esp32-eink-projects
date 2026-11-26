#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <math.h>

#include "koi_model.h"
#include "pond_decor.h"
#include "pond_world.h"
#include "port_display.h"
#include "radio.h"

// E-paper command helpers live in the board BSP (not the public header).
void EPD_SendCommand(uint8_t command);
void EPD_SendData(uint8_t data);

namespace {

constexpr int kBootPin = 9;
constexpr int kPwrPin = 2;
constexpr int kSdaPin = 18;
constexpr int kSclPin = 8;
constexpr gpio_num_t kBootGpio = GPIO_NUM_9;
constexpr gpio_num_t kPwrGpio = GPIO_NUM_2;

// GPIO12 and GPIO13 are USB D−/D+ on this board. Leave them alone.

constexpr uint8_t kTcaAddress = 0x20;
constexpr uint8_t kTcaOutputReg = 0x01;
constexpr uint8_t kTcaConfigReg = 0x03;
constexpr uint8_t kBatteryBit = 1 << 5;
constexpr uint8_t kEpdBit = 1 << 0;
constexpr uint8_t kAudioBit = 1 << 1;
constexpr uint8_t kAmpBit = 1 << 3;
constexpr uint8_t kRailMask = kBatteryBit | kEpdBit | kAudioBit | kAmpBit;

enum class Mode : uint8_t { Cal, Swim, Wait, Pause };

pond::PondWorld gWorld;
pond::PondSim gSim;
pond::RippleField gRipples;
Mode gMode = Mode::Cal;
bool gAsleep = false;
bool gCalDrawn = false;
bool gHandoffSent = false;
uint32_t gCalUntil = 0;
uint32_t gLastFrameMs = 0;
uint32_t gHandoffMs = 0;
uint32_t gPauseUntil = 0;
uint32_t gPass = 0;

volatile uint32_t gBootShorts = 0;
volatile uint32_t gBootLongs = 0;
volatile bool gPwrOff = false;
uint32_t gSeenShorts = 0;
uint32_t gSeenLongs = 0;

bool write_reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kTcaAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool write_rails(bool battery_on, bool epd_on) {
  uint8_t output = 0;
  if (battery_on) {
    output |= kBatteryBit;
  }
  if (epd_on) {
    output |= kEpdBit | kAudioBit | kAmpBit;
  }
  const uint8_t config = static_cast<uint8_t>(~kRailMask);
  return write_reg(kTcaOutputReg, output) && write_reg(kTcaConfigReg, config);
}

void hold_battery() {
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (write_rails(true, true)) {
      Serial.println("Battery hold on (TCA9554 EXIO5). E-paper powered.");
      return;
    }
    delay(30);
  }
  Serial.println("TCA9554 0x20 did not latch. Battery drops when PWR is released.");
}

void epd_sleep() {
  EPD_SendCommand(0x10);
  EPD_SendData(0x01);
  gAsleep = true;
}

void epd_wake() {
  EPD_Init();
  EPD_Init_Partial();
  gAsleep = false;
}

void draw_glyph(int x, int y, char c, int scale) {
  uint8_t rows[6] = {0, 0, 0, 0, 0, 0};
  switch (c) {
    case '0': rows[0] = 0xE; rows[1] = 0xA; rows[2] = 0xA; rows[3] = 0xA; rows[4] = 0xA; rows[5] = 0xE; break;
    case '1': rows[0] = 0x4; rows[1] = 0xC; rows[2] = 0x4; rows[3] = 0x4; rows[4] = 0x4; rows[5] = 0xE; break;
    case '5': rows[0] = 0xE; rows[1] = 0x8; rows[2] = 0xE; rows[3] = 0x2; rows[4] = 0x2; rows[5] = 0xE; break;
    case '+': rows[0] = 0x0; rows[1] = 0x4; rows[2] = 0xE; rows[3] = 0x4; rows[4] = 0x0; rows[5] = 0x0; break;
    case '-': rows[0] = 0x0; rows[1] = 0x0; rows[2] = 0xE; rows[3] = 0x0; rows[4] = 0x0; rows[5] = 0x0; break;
    case 'E': rows[0] = 0xE; rows[1] = 0x8; rows[2] = 0xE; rows[3] = 0x8; rows[4] = 0x8; rows[5] = 0xE; break;
    case 'I': rows[0] = 0xE; rows[1] = 0x4; rows[2] = 0x4; rows[3] = 0x4; rows[4] = 0x4; rows[5] = 0xE; break;
    case 'N': rows[0] = 0xA; rows[1] = 0xE; rows[2] = 0xE; rows[3] = 0xA; rows[4] = 0xA; rows[5] = 0xA; break;
    case 'K': rows[0] = 0xA; rows[1] = 0xA; rows[2] = 0xC; rows[3] = 0xA; rows[4] = 0xA; rows[5] = 0xA; break;
    case 'O': rows[0] = 0xE; rows[1] = 0xA; rows[2] = 0xA; rows[3] = 0xA; rows[4] = 0xA; rows[5] = 0xE; break;
    case 'F': rows[0] = 0xE; rows[1] = 0x8; rows[2] = 0xE; rows[3] = 0x8; rows[4] = 0x8; rows[5] = 0x8; break;
    default: return;
  }
  for (int row = 0; row < 6; ++row) {
    for (int col = 0; col < 4; ++col) {
      if ((rows[row] & (0x8 >> col)) == 0) {
        continue;
      }
      EPD_FillRect(static_cast<uint16_t>(x + col * scale), static_cast<uint16_t>(y + row * scale),
                   static_cast<uint16_t>(scale), static_cast<uint16_t>(scale), DRIVER_COLOR_BLACK);
    }
  }
}

void draw_text(int x, int y, const char *text, int scale) {
  while (*text != '\0') {
    if (*text != ' ') {
      draw_glyph(x, y, *text, scale);
    }
    x += 5 * scale;
    ++text;
  }
}

void draw_ticks(const pond::DisplaySpan &span) {
  const int y0 = pond::pond_screen_y(span, 0.f);
  if (y0 >= 0 && y0 < span.px_h) {
    EPD_DrawHLine(0, static_cast<uint16_t>(y0), static_cast<uint16_t>(span.px_w), DRIVER_COLOR_BLACK);
  }
  const float marks[] = {10.f, 5.f, -5.f, -10.f};
  for (float mark : marks) {
    if (fabsf(mark) > span.height_mm * 0.5f) {
      continue;
    }
    const int y = pond::pond_screen_y(span, mark);
    const int half = fabsf(mark) > 7.f ? 14 : 8;
    const int top = y - half;
    if (top < 0 || y + half >= span.px_h) {
      continue;
    }
    EPD_DrawVLine(static_cast<uint16_t>(span.px_w / 2), static_cast<uint16_t>(top),
                  static_cast<uint16_t>(half * 2), DRIVER_COLOR_BLACK);
  }
}

void draw_calibration() {
  EPD_Clear();
  draw_ticks(gWorld.eink);
  draw_text(8, 8, "EINK", 2);
}

uint8_t eink_ink(pond::KoiMat mat) {
  switch (mat) {
    case pond::KoiMat::Body:
    case pond::KoiMat::BodyShade:
    case pond::KoiMat::Fin:
    case pond::KoiMat::FinTip:
      return DRIVER_COLOR_WHITE;
    default:
      return DRIVER_COLOR_BLACK;
  }
}

void plot_if(int x, int y) {
  if (x < 0 || y < 0 || x >= pond::kEinkPx || y >= pond::kEinkPx) {
    return;
  }
  EPD_DrawColorPixel(static_cast<uint16_t>(x), static_cast<uint16_t>(y), DRIVER_COLOR_BLACK);
}

void draw_pad_outlines() {
  const pond::Pad *pads = pond::pond_pads(pond::DecorScreen::Eink);
  const int count = pond::pond_pad_count(pond::DecorScreen::Eink);
  for (int i = 0; i < count; ++i) {
    const pond::Pad &pad = pads[i];
    const int samples = static_cast<int>(pad.r_mm * pond::kEinkPxPerMm * 3.2f);
    for (int s = 0; s < samples; ++s) {
      const float angle = 2.f * pond::kPi * static_cast<float>(s) / static_cast<float>(samples);
      if (fabsf(pond::pond_wrap_pi(angle - pad.notch)) < 0.55f) {
        continue;
      }
      for (float inset = 0.f; inset <= 0.30f; inset += 0.30f) {
        const float gx = pad.x_mm + cosf(angle) * (pad.r_mm - inset);
        const float gy = pad.y_mm + sinf(angle) * (pad.r_mm - inset);
        const int sx = pond::pond_round(pond::kEinkPx * 0.5f + gx * pond::kEinkPxPerMm);
        const int sy = pond::pond_round(pond::kEinkPx * 0.5f - gy * pond::kEinkPxPerMm);
        plot_if(sx, sy);
      }
    }
  }
}

void draw_ripples() {
  for (int i = 0; i < 4; ++i) {
    const pond::RippleField::Item &item = gRipples.items[i];
    if (!item.live) {
      continue;
    }
    const float age = gRipples.time_s - item.born_s;
    const float radius = pond::ripple_radius_mm(age);
    if (radius < 0.4f) {
      continue;
    }
    const int samples = 36;
    for (int s = 0; s < samples; s += 2) {
      const float angle = 2.f * pond::kPi * static_cast<float>(s) / static_cast<float>(samples);
      const float x = item.x_mm + cosf(angle) * radius;
      const float y = item.y_mm + sinf(angle) * radius;
      const int sx = pond::pond_screen_x(gWorld.eink, x);
      const int sy = pond::pond_screen_y(gWorld.eink, y);
      plot_if(sx, sy);
    }
  }
}

void draw_stipple() {
  for (int y = 2; y < pond::kEinkPx; y += 4) {
    for (int x = 1; x < pond::kEinkPx; x += 5) {
      const uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
      if (((h ^ (h >> 13)) & 0x3F) == 0) {
        plot_if(x, y);
      }
    }
  }
}

void draw_koi(const pond::KoiPose &pose) {
  const float step = 1.f / pond::kEinkPxPerMm;
  const float x0 = pose.x_nose_mm - 0.8f;
  const float x1 = pose.x_nose_mm + pond::kKoiLengthMm + 1.5f;
  const float y0 = pose.y_mm - 6.9f;
  const float y1 = pose.y_mm + 6.9f;
  for (float y = y0; y <= y1; y += step) {
    for (float x = x0; x <= x1; x += step) {
      const pond::KoiMat mat = pond::koi_material_world(pose, x, y);
      if (mat == pond::KoiMat::None) {
        continue;
      }
      const int sx = pond::pond_screen_x(gWorld.eink, x);
      const int sy = pond::pond_screen_y(gWorld.eink, y);
      if (sx < 0 || sy < 0 || sx >= pond::kEinkPx || sy >= pond::kEinkPx) {
        continue;
      }
      EPD_DrawColorPixel(static_cast<uint16_t>(sx), static_cast<uint16_t>(sy), eink_ink(mat));
    }
  }
}

void draw_swim() {
  EPD_Clear();
  draw_stipple();
  draw_pad_outlines();
  draw_ripples();
  draw_koi(pond::koi_pose_from(gSim.x_nose_mm, gSim.phase));
}

pond::Packet make_packet(pond::Event event) {
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
  packet.loop_index = gPass;
  return packet;
}

void send_event(pond::Event event) { radio_send(make_packet(event)); }

void start_pass() {
  if (gAsleep) {
    epd_wake();
  }
  ++gPass;
  pond::pond_sim_reset(&gSim, gWorld.eink.x1_mm - 0.4f, 0.37f * static_cast<float>(gPass));
  pond::ripples_reset(&gRipples);
  gHandoffSent = false;
  gLastFrameMs = millis();
  gMode = Mode::Swim;
  Serial.printf("Pass %lu  nose %.1f mm\n", static_cast<unsigned long>(gPass), gSim.x_nose_mm);
}

void enter_cal() {
  send_event(pond::Event::CalOn);
  send_event(pond::Event::PassReset);
  gMode = Mode::Cal;
  gCalDrawn = false;
  gCalUntil = millis() + 600000;
  Serial.println("Calibration on");
}

void leave_cal() {
  send_event(pond::Event::CalOff);
  Serial.println("Calibration off");
  start_pass();
}

void finish_eink() {
  if (!gHandoffSent) {
    send_event(pond::Event::Handoff);
    gHandoffSent = true;
    gHandoffMs = millis();
  }
  EPD_Clear();
  const bool ghost = (gPass % pond::kGhostEvery) == 0;
  if (ghost) {
    Serial.println("Full ghost-clear while the koi is off the e-ink");
    EPD_FlushFull();
  } else {
    EPD_FlushPartial();
  }
  epd_sleep();
  gMode = Mode::Wait;
  Serial.println("E-ink asleep, waiting for CYCLE_DONE");
}

void swim_frame() {
  draw_swim();
  EPD_FlushPartial();
  const uint32_t now = millis();
  float dt = static_cast<float>(now - gLastFrameMs) / 1000.f;
  gLastFrameMs = now;
  if (dt > 0.45f) {
    dt = 0.45f;
  }
  const float previous = gSim.x_nose_mm;
  pond::pond_sim_step(&gSim, dt, gWorld);
  float tail_x = 0.f;
  float tail_y = 0.f;
  const pond::KoiPose pose = pond::koi_pose_from(gSim.x_nose_mm, gSim.phase);
  pond::koi_tail_point(pose, &tail_x, &tail_y);
  pond::ripples_advance(&gRipples, dt, tail_x, tail_y, true);
  if (!gHandoffSent && previous > gWorld.eink.x0_mm && gSim.x_nose_mm <= gWorld.eink.x0_mm) {
    send_event(pond::Event::Handoff);
    gHandoffSent = true;
    gHandoffMs = millis();
    Serial.printf("HANDOFF x %.2f y %.2f phase %.2f\n", gSim.x_nose_mm, pose.y_mm, gSim.phase);
  }
  if (gSim.x_nose_mm + pond::kKoiLengthMm < gWorld.eink.x0_mm) {
    finish_eink();
  }
}

void on_packet(const pond::Packet &packet) {
  if (packet.event == static_cast<uint8_t>(pond::Event::CycleDone) && gMode == Mode::Wait) {
    Serial.printf("CYCLE_DONE loop %lu\n", static_cast<unsigned long>(packet.loop_index));
    gMode = Mode::Pause;
    gPauseUntil = millis() + pond::kPassPauseMs;
  }
}

void service_buttons() {
  const uint32_t shorts = gBootShorts;
  const uint32_t longs = gBootLongs;
  if (shorts != gSeenShorts) {
    gSeenShorts = shorts;
    if (gMode == Mode::Cal) {
      leave_cal();
    } else {
      send_event(pond::Event::PassReset);
      start_pass();
    }
  }
  if (longs != gSeenLongs) {
    gSeenLongs = longs;
    if (gMode == Mode::Cal) {
      leave_cal();
    } else {
      enter_cal();
    }
  }
  if (gPwrOff) {
    gPwrOff = false;
    Serial.println("PWR off");
    EPD_Clear();
    if (gAsleep) {
      epd_wake();
    }
    draw_text(78, 90, "OFF", 3);
    EPD_FlushFull();
    write_rails(false, true);
    bool saw_release = false;
    while (true) {
      const bool pressed = digitalRead(kPwrPin) == LOW;
      if (!pressed) {
        saw_release = true;
      } else if (saw_release) {
        esp_restart();
      }
      delay(20);
    }
  }
}

void button_task(void *) {
  bool boot_raw_last = digitalRead(kBootPin) == LOW;
  bool boot_stable = boot_raw_last;
  uint32_t boot_change = millis();
  uint32_t boot_down = 0;
  bool boot_long_sent = false;
  bool boot_armed = false;

  bool pwr_raw_last = digitalRead(kPwrPin) == LOW;
  bool pwr_stable = pwr_raw_last;
  uint32_t pwr_change = millis();
  uint32_t pwr_down = 0;
  bool pwr_armed = false;
  bool pwr_long_sent = false;

  while (true) {
    const uint32_t now = millis();
    const bool boot_raw = digitalRead(kBootPin) == LOW;
    if (boot_raw != boot_raw_last) {
      boot_raw_last = boot_raw;
      boot_change = now;
    }
    if (!boot_raw) {
      boot_armed = true;
    }
    if (boot_armed && (now - boot_change) >= 25 && boot_raw != boot_stable) {
      boot_stable = boot_raw;
      if (boot_stable) {
        boot_down = now;
        boot_long_sent = false;
      } else if (!boot_long_sent && (now - boot_down) >= 40) {
        gBootShorts = gBootShorts + 1;
      }
    }
    if (boot_armed && boot_stable && !boot_long_sent && (now - boot_down) >= pond::kBootLongMs) {
      boot_long_sent = true;
      gBootLongs = gBootLongs + 1;
    }

    const bool pwr_raw = digitalRead(kPwrPin) == LOW;
    if (pwr_raw != pwr_raw_last) {
      pwr_raw_last = pwr_raw;
      pwr_change = now;
    }
    if (!pwr_raw) {
      pwr_armed = true;
    }
    if (pwr_armed && (now - pwr_change) >= 25 && pwr_raw != pwr_stable) {
      pwr_stable = pwr_raw;
      if (pwr_stable) {
        pwr_down = now;
        pwr_long_sent = false;
      }
    }
    if (pwr_armed && pwr_stable && !pwr_long_sent && (now - pwr_down) >= pond::kBootLongMs) {
      pwr_long_sent = true;
      gPwrOff = true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("fish-trio e-ink");

  gpio_reset_pin(kBootGpio);
  gpio_reset_pin(kPwrGpio);
  pinMode(kBootPin, INPUT_PULLUP);
  pinMode(kPwrPin, INPUT_PULLUP);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);
  hold_battery();

  gWorld = pond::pond_world();
  xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 4, nullptr, 0);
  radio_begin();
  send_event(pond::Event::CalOn);

  PortDisplay_Init();
  draw_calibration();
  EPD_FlushBaseThenPartial();
  gCalDrawn = true;
  gCalUntil = millis() + pond::kCalBootMs;
  gMode = Mode::Cal;
  Serial.println("Centerline up. Short BOOT swims, hold BOOT toggles the line.");
}

void loop() {
  radio_poll(on_packet);
  service_buttons();

  switch (gMode) {
    case Mode::Cal:
      if (!gCalDrawn) {
        if (gAsleep) {
          epd_wake();
        }
        draw_calibration();
        EPD_FlushBaseThenPartial();
        gCalDrawn = true;
      }
      if ((int32_t)(millis() - gCalUntil) >= 0) {
        leave_cal();
      } else {
        delay(20);
      }
      break;
    case Mode::Swim:
      swim_frame();
      break;
    case Mode::Wait:
      if (gHandoffSent && millis() - gHandoffMs >
                              static_cast<uint32_t>(pond::pond_safety_timeout_s(gWorld) * 1000.f)) {
        Serial.println("CYCLE_DONE timeout, launching the next pass");
        gMode = Mode::Pause;
        gPauseUntil = millis() + pond::kPassPauseMs;
      } else {
        delay(10);
      }
      break;
    case Mode::Pause:
      if ((int32_t)(millis() - gPauseUntil) >= 0) {
        start_pass();
      } else {
        delay(10);
      }
      break;
  }
}
