#include "player.h"

#include <Arduino.h>
#include <string.h>

#include "frames.h"
#include "oled.h"

namespace {

constexpr uint32_t kTailMs = 4000;
constexpr uint32_t kWipeMs = 480;
constexpr int kWipeTravel = kFrameWidth + 15;

struct TextBox {
  uint8_t x;
  uint8_t y;
  uint8_t w;
  uint8_t h;
};

// Frames 5, 11, and 15 are open on the left half. The others use the open
// bottom band, the open centre band, or the open top band when the drawing
// sits along the bottom.
static const TextBox kTextBox[kFrameCount] = {
    {0, 28, 128, 36},  // 1 bottom
    {0, 14, 128, 36},  // 2 centre
    {0, 14, 128, 36},  // 3 centre
    {0, 0, 128, 34},   // 4 top
    {0, 0, 64, 64},    // 5 left half
    {0, 10, 128, 36},  // 6 centre
    {0, 30, 128, 34},  // 7 bottom
    {0, 32, 128, 32},  // 8 bottom
    {0, 16, 128, 32},  // 9 centre
    {0, 16, 128, 32},  // 10 centre
    {0, 0, 64, 64},    // 11 left half
    {0, 0, 128, 34},   // 12 top
    {0, 32, 128, 32},  // 13 bottom
    {0, 16, 128, 32},  // 14 centre
    {0, 0, 64, 64},    // 15 left half
    {0, 32, 128, 32},  // 16 bottom
};

static const uint8_t kBayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static const int8_t kHalo[8][2] = {
    {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1},
};

const LyricScript *gScript = nullptr;
uint32_t gOriginMs = 0;
int gCommitted = -1;
int gWipeFrom = 1;
int gWipeTo = 1;
uint32_t gWipeStart = 0;
bool gWiping = false;

uint8_t gFrom[kFrameBytes];
uint8_t gTo[kFrameBytes];
uint8_t gComposite[kFrameBytes];

int clamp_frame(int frame) {
  if (frame < 1) {
    return 1;
  }
  if (frame > kFrameCount) {
    return kFrameCount;
  }
  return frame;
}

bool bit_on(const uint8_t *buf, int x, int y) {
  const uint8_t byte = buf[y * kFrameStride + (x >> 3)];
  return (byte & static_cast<uint8_t>(1u << (x & 7))) != 0;
}

void set_bit(uint8_t *buf, int x, int y) {
  buf[y * kFrameStride + (x >> 3)] |= static_cast<uint8_t>(1u << (x & 7));
}

void go_frame(int frame, uint32_t now_ms, bool snap) {
  frame = clamp_frame(frame);
  if (gCommitted < 0 || snap) {
    gCommitted = frame;
    gWipeFrom = frame;
    gWipeTo = frame;
    gWiping = false;
    return;
  }
  if (gWiping) {
    if (frame == gWipeTo) {
      return;
    }
    gWipeFrom = gWipeTo;
  } else if (frame == gCommitted) {
    return;
  } else {
    gWipeFrom = gCommitted;
  }
  gWipeTo = frame;
  gCommitted = frame;
  gWipeStart = now_ms;
  gWiping = true;
}

int wipe_head(uint32_t now_ms) {
  if (!gWiping) {
    return kWipeTravel;
  }
  const uint32_t elapsed = now_ms - gWipeStart;
  if (elapsed >= kWipeMs) {
    gWiping = false;
    gWipeFrom = gWipeTo;
    return kWipeTravel;
  }
  return static_cast<int>((static_cast<uint64_t>(elapsed) * kWipeTravel) / kWipeMs);
}

void blit_frames(int head) {
  memcpy_P(gFrom, kFrameTable[gWipeFrom - 1], kFrameBytes);
  memcpy_P(gTo, kFrameTable[gWipeTo - 1], kFrameBytes);
  if (head <= 0) {
    memcpy(gComposite, gFrom, kFrameBytes);
    return;
  }
  if (head >= kWipeTravel || gWipeFrom == gWipeTo) {
    memcpy(gComposite, gTo, kFrameBytes);
    return;
  }
  memset(gComposite, 0, kFrameBytes);
  for (int y = 0; y < kFrameHeight; ++y) {
    for (int x = 0; x < kFrameWidth; ++x) {
      const int thresh = x + kBayer[y & 3][x & 3];
      const bool on = (head > thresh) ? bit_on(gTo, x, y) : bit_on(gFrom, x, y);
      if (on) {
        set_bit(gComposite, x, y);
      }
    }
  }
}

int active_index(uint32_t play_ms) {
  int index = -1;
  for (int i = 0; i < gScript->count; ++i) {
    if (gScript->lines[i].time_ms <= play_ms) {
      index = i;
    } else {
      break;
    }
  }
  return index;
}

uint32_t line_duration(int index) {
  if (index + 1 < gScript->count) {
    const uint32_t next = gScript->lines[index + 1].time_ms;
    const uint32_t start = gScript->lines[index].time_ms;
    if (next > start) {
      return next - start;
    }
    return 0;
  }
  return kTailMs;
}

// Longest prefix of src that fits max_w, breaking on a space when the rest
// of the string does not fit. Returns how many source bytes were consumed,
// including one skipped break space.
int take_line(const char *src, int max_w, char *dest, int cap) {
  dest[0] = '\0';
  if (src[0] == '\0' || cap < 2 || max_w <= 0) {
    return 0;
  }
  const int length = static_cast<int>(strlen(src));
  const int limit = length < (cap - 1) ? length : (cap - 1);
  int best = 0;
  int break_at = -1;
  char trial[72];
  for (int n = 1; n <= limit; ++n) {
    memcpy(trial, src, static_cast<size_t>(n));
    trial[n] = '\0';
    if (oled.getStrWidth(trial) > max_w) {
      break;
    }
    best = n;
    if (src[n - 1] == ' ') {
      break_at = n - 1;
    }
  }
  int cut = best;
  if (best < length && break_at > 0) {
    cut = break_at;
  }
  if (cut <= 0) {
    cut = 1;
  }
  memcpy(dest, src, static_cast<size_t>(cut));
  dest[cut] = '\0';
  int consumed = cut;
  if (src[cut] == ' ') {
    consumed = cut + 1;
  }
  return consumed;
}

void wrap_two(const char *text, int max_w, char *line1, char *line2, int cap) {
  const int used = take_line(text, max_w, line1, cap);
  take_line(text + used, max_w, line2, cap);
}

void copy_prefix(const char *src, int count, char *dest, int cap) {
  if (count < 0) {
    count = 0;
  }
  if (count >= cap) {
    count = cap - 1;
  }
  const int length = static_cast<int>(strlen(src));
  if (count > length) {
    count = length;
  }
  memcpy(dest, src, static_cast<size_t>(count));
  dest[count] = '\0';
}

void draw_halo(int x, int y, const char *text) {
  if (text[0] == '\0') {
    return;
  }
  oled.setDrawColor(0);
  for (int i = 0; i < 8; ++i) {
    oled.drawStr(x + kHalo[i][0], y + kHalo[i][1], text);
  }
  oled.setDrawColor(1);
  oled.drawStr(x, y, text);
}

void draw_cursor(int x, int baseline, int ascent) {
  if (((millis() / 400) % 2) != 0) {
    return;
  }
  const int top = baseline - ascent;
  const int h = ascent > 0 ? ascent : 1;
  oled.setDrawColor(0);
  oled.drawBox(x - 1, top - 1, 4, h + 2);
  oled.setDrawColor(1);
  oled.drawBox(x, top, 2, h);
}

void draw_caption(const TextBox &box, const char *text, uint32_t elapsed, uint32_t duration) {
  oled.setFont(u8g2_font_lubBI12_tr);
  oled.setFontMode(1);
  oled.setFontPosBaseline();

  const int pad = 2;
  const int max_w = box.w - pad * 2;
  char full1[72];
  char full2[72];
  wrap_two(text, max_w, full1, full2, 72);

  const int n1 = static_cast<int>(strlen(full1));
  const int n2 = static_cast<int>(strlen(full2));
  const int total = n1 + n2;
  int shown = total;
  if (duration > 0 && total > 0 && elapsed < duration) {
    shown = static_cast<int>((static_cast<uint64_t>(elapsed) * static_cast<uint32_t>(total)) / duration);
  }
  if (shown > total) {
    shown = total;
  }
  if (shown < 0) {
    shown = 0;
  }

  char vis1[72];
  char vis2[72];
  const int take1 = shown < n1 ? shown : n1;
  copy_prefix(full1, take1, vis1, 72);
  copy_prefix(full2, shown - take1, vis2, 72);

  int ascent = oled.getAscent();
  int below = oled.getDescent();
  if (below < 0) {
    below = -below;
  }
  const int glyph_h = ascent + below;
  const bool two = n2 > 0;
  int gap = 1;
  if (two && glyph_h * 2 + gap > box.h) {
    gap = 0;
  }
  const int block = two ? glyph_h * 2 + gap : glyph_h;
  int top = box.y + (static_cast<int>(box.h) - block) / 2;
  if (top < box.y) {
    top = box.y;
  }
  const int base1 = top + ascent;
  const int base2 = base1 + glyph_h + gap;
  const int text_x = box.x + pad;

  oled.setClipWindow(box.x, box.y, box.x + box.w - 1, box.y + box.h - 1);
  draw_halo(text_x, base1, vis1);
  if (two) {
    draw_halo(text_x, base2, vis2);
  }
  if (shown < total) {
    const bool on_second = two && take1 >= n1;
    const char *vis = on_second ? vis2 : vis1;
    const int baseline = on_second ? base2 : base1;
    int cursor_x = text_x + oled.getStrWidth(vis);
    const int limit = box.x + box.w - 4;
    if (cursor_x > limit) {
      cursor_x = limit;
    }
    draw_cursor(cursor_x, baseline, ascent);
  }
  oled.setMaxClipWindow();
  oled.setDrawColor(1);
}

}  // namespace

void player_begin(const LyricScript *script) {
  gScript = script;
}

void player_restart(uint32_t now_ms) {
  gOriginMs = now_ms;
  gCommitted = -1;
  gWiping = false;
}

void player_render(uint32_t now_ms) {
  uint32_t play_ms = now_ms - gOriginMs;
  const char *text = nullptr;
  uint32_t elapsed = 0;
  uint32_t duration = 0;
  int frame = 1;
  bool snap = gCommitted < 0;

  if (gScript == nullptr || gScript->count <= 0) {
    text = "Add lyrics.txt";
    frame = 1;
  } else {
    const int index = active_index(play_ms);
    if (index < 0) {
      frame = gScript->lines[0].frame;
      text = "";
    } else {
      const LyricLine &line = gScript->lines[index];
      frame = line.frame;
      text = line.text;
      elapsed = play_ms - line.time_ms;
      duration = line_duration(index);
    }
  }

  go_frame(frame, now_ms, snap);
  const int head = wipe_head(now_ms);
  blit_frames(head);

  oled.clearBuffer();
  oled.setDrawColor(1);
  oled.drawXBM(0, 0, kFrameWidth, kFrameHeight, gComposite);
  if (text != nullptr && text[0] != '\0') {
    draw_caption(kTextBox[clamp_frame(frame) - 1], text, elapsed, duration);
  }
  oled.sendBuffer();
}
