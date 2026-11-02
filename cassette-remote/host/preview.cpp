#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include <clib/u8g2.h>

#include "transport.h"
#include "ui.h"

namespace {

uint8_t gpio_dummy(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8;
  (void)msg;
  (void)arg_int;
  (void)arg_ptr;
  return 1;
}

u8g2_t panel;

void panel_begin() {
  uint8_t height = 0;
  // Same full-buffer SH1106 memory the firmware draws into. The byte
  // callback is empty, so nothing tries to talk to a panel.
  u8g2_SetupDisplay(&panel, u8x8_d_sh1106_128x64_noname, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_empty, gpio_dummy);
  uint8_t *buffer = u8g2_m_16_8_f(&height);
  u8g2_SetupBuffer(&panel, buffer, height, u8g2_ll_hvline_vertical_top_lsb, &u8g2_cb_r0);
}

int lit(int x, int y) {
  const uint8_t *buffer = u8g2_GetBufferPtr(&panel);
  return (buffer[(y >> 3) * 128 + x] >> (y & 7)) & 1;
}

void write_pbm(const char *path) {
  FILE *file = fopen(path, "wb");
  if (file == nullptr) {
    perror(path);
    return;
  }
  fputs("P4\n128 64\n", file);
  for (int y = 0; y < 64; ++y) {
    uint8_t bits = 0;
    for (int x = 0; x < 128; ++x) {
      bits = static_cast<uint8_t>((bits << 1) | (lit(x, y) ? 1 : 0));
      if ((x & 7) == 7) {
        fputc(bits, file);
        bits = 0;
      }
    }
  }
  fclose(file);
}

int count_lit() {
  int count = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      count += lit(x, y);
    }
  }
  return count;
}

void render(const char *path, const UiView *view) {
  ui_render(&panel, view);
  const int pixels = count_lit();
  if (pixels < 80) {
    fprintf(stderr, "%s only lit %d pixels\n", path, pixels);
  }
  write_pbm(path);
}

void ensure_dir(const char *path) {
  mkdir(path, 0755);
}

UiView connected_view(uint16_t angle, int tape, bool playing, int8_t seek, bool volume, int volumePos) {
  UiView view{};
  view.connected = true;
  view.playing = playing;
  view.seek = seek;
  view.angle = angle;
  view.tape = tape;
  view.volume = volume;
  view.volumePos = volumePos;
  view.volumePop = volume ? 255 : 0;
  view.nowMs = 2000;
  return view;
}

}  // namespace

int main() {
  panel_begin();
  ensure_dir("build");
  ensure_dir("build/spin");
  ensure_dir("build/whip");

  UiView pairing{};
  pairing.connected = false;
  pairing.nowMs = 1100;
  render("build/pairing.pbm", &pairing);

  const UiView paused = connected_view(8000, 0, false, 0, false, 880);
  render("build/paused.pbm", &paused);

  const UiView playing = connected_view(20000, 640, true, 0, false, 880);
  render("build/playing.pbm", &playing);

  const UiView late = connected_view(40000, 860, true, 0, false, 0);
  render("build/late.pbm", &late);

  const UiView forward = connected_view(12000, 300, true, 1, false, 0);
  render("build/forward.pbm", &forward);

  const UiView rewind = connected_view(48000, 700, false, -1, false, 0);
  render("build/rewind.pbm", &rewind);

  const UiView volume = connected_view(22000, 500, true, 0, true, 680);
  render("build/volume.pbm", &volume);

  for (int frame = 0; frame < 24; ++frame) {
    char path[64];
    snprintf(path, sizeof(path), "build/spin/%02d.pbm", frame);
    const UiView view = connected_view(static_cast<uint16_t>(frame * (65536 / 24)), 640, true, 0, false, 880);
    render(path, &view);
  }

  Transport deck;
  transport_init(&deck);
  transport_toggle_play(&deck);
  for (int step = 0; step < 30; ++step) {
    transport_tick(&deck, 33);
  }
  transport_track(&deck, 1);
  for (int frame = 0; frame < 28; ++frame) {
    char path[64];
    snprintf(path, sizeof(path), "build/whip/%02d.pbm", frame);
    UiView view{};
    view.connected = true;
    view.playing = deck.playing;
    view.seek = deck.seek;
    view.angle = static_cast<uint16_t>(deck.angle);
    view.tape = deck.tape;
    view.nowMs = 3000 + static_cast<uint32_t>(frame) * 33;
    render(path, &view);
    transport_tick(&deck, 33);
  }

  printf("preview frames written\n");
  return 0;
}
