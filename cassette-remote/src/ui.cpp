#include "ui.h"

// Match U8g2lib.h. The draw functions are compiled with a 16-bit coordinate
// type; including the C header without this typedefs a different one.
#ifndef U8G2_16BIT
#define U8G2_16BIT
#endif
#include <clib/u8g2.h>

namespace {

constexpr int kLeftX = 34;
constexpr int kRightX = 94;
constexpr int kReelY = 25;
constexpr int kHubR = 6;
constexpr int kRMin = 8;
constexpr int kRMax = 16;
constexpr int kSpokeHalf = 4200;

// Quarter sine, Q14. Index 0..64 is 0..90 degrees.
constexpr int16_t kSinQ[65] = {
    0,     402,   804,   1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,
    4370,  4756,  5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,
    8423,  8765,  9102,  9434,  9760,  10080, 10394, 10702, 11003, 11297, 11585,
    11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256,
    14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893,
    15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};

// 5x7 glyphs, bit 4 is the left pixel. Only the pairing caption needs them.
constexpr uint8_t kGlyphP[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
constexpr uint8_t kGlyphA[7] = {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x11, 0x0F};
constexpr uint8_t kGlyphI[7] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E};
constexpr uint8_t kGlyphR[7] = {0x00, 0x0E, 0x09, 0x08, 0x08, 0x08, 0x08};
constexpr uint8_t kGlyphN[7] = {0x00, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
constexpr uint8_t kGlyphG[7] = {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E};
constexpr uint8_t kGlyphDot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};

int16_t isin(uint16_t angle) {
  const uint16_t quadrant = angle >> 14;
  const uint16_t remainder = angle & 0x3FFF;
  int index = (remainder * 64 + 8191) >> 14;
  if (index > 64) {
    index = 64;
  }
  int16_t value = kSinQ[index];
  if (quadrant == 1) {
    value = kSinQ[64 - index];
  } else if (quadrant == 2) {
    value = (int16_t)(-kSinQ[index]);
  } else if (quadrant == 3) {
    value = (int16_t)(-kSinQ[64 - index]);
  }
  return value;
}

int16_t icos(uint16_t angle) {
  return isin((uint16_t)(angle + 16384));
}

void polar(int cx, int cy, int radius, uint16_t angle, int *x, int *y) {
  *x = cx + (radius * icos(angle)) / 16384;
  *y = cy + (radius * isin(angle)) / 16384;
}

int reel_radius(int amount) {
  if (amount < 0) {
    amount = 0;
  }
  if (amount > 1000) {
    amount = 1000;
  }
  const int r2min = kRMin * kRMin;
  const int r2max = kRMax * kRMax;
  const int r2 = r2min + (r2max - r2min) * amount / 1000;
  int radius = kRMin;
  while ((radius + 1) * (radius + 1) <= r2 && radius < kRMax) {
    radius += 1;
  }
  return radius;
}

void draw_shell(u8g2_t *u8g2, int x, int y, int w, int h, int chamfer) {
  u8g2_DrawHLine(u8g2, (u8g2_uint_t)(x + chamfer), (u8g2_uint_t)y, (u8g2_uint_t)(w - 2 * chamfer));
  u8g2_DrawHLine(u8g2, (u8g2_uint_t)(x + chamfer), (u8g2_uint_t)(y + h - 1), (u8g2_uint_t)(w - 2 * chamfer));
  u8g2_DrawVLine(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)(y + chamfer), (u8g2_uint_t)(h - 2 * chamfer));
  u8g2_DrawVLine(u8g2, (u8g2_uint_t)(x + w - 1), (u8g2_uint_t)(y + chamfer), (u8g2_uint_t)(h - 2 * chamfer));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)(y + chamfer), (u8g2_uint_t)(x + chamfer), (u8g2_uint_t)y);
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + w - 1 - chamfer), (u8g2_uint_t)y, (u8g2_uint_t)(x + w - 1),
                (u8g2_uint_t)(y + chamfer));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)(y + h - 1 - chamfer), (u8g2_uint_t)(x + chamfer),
                (u8g2_uint_t)(y + h - 1));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + w - 1 - chamfer), (u8g2_uint_t)(y + h - 1), (u8g2_uint_t)(x + w - 1),
                (u8g2_uint_t)(y + h - 1 - chamfer));
}

void draw_glyph(u8g2_t *u8g2, int x, int y, const uint8_t *rows) {
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = rows[row];
    for (int col = 0; col < 5; ++col) {
      if ((bits & (0x10 >> col)) != 0) {
        u8g2_DrawPixel(u8g2, (u8g2_uint_t)(x + col), (u8g2_uint_t)(y + row));
      }
    }
  }
}

const uint8_t *glyph_for(char letter) {
  switch (letter) {
    case 'p':
      return kGlyphP;
    case 'a':
      return kGlyphA;
    case 'i':
      return kGlyphI;
    case 'r':
      return kGlyphR;
    case 'n':
      return kGlyphN;
    case 'g':
      return kGlyphG;
    default:
      return kGlyphDot;
  }
}

void draw_pairing(u8g2_t *u8g2, int y, int dots) {
  const char *word = "pairing";
  const int wordW = 7 * 6 - 1;
  int x = (128 - wordW) / 2;
  for (int i = 0; word[i] != '\0'; ++i) {
    draw_glyph(u8g2, x, y, glyph_for(word[i]));
    x += 6;
  }
  for (int i = 0; i < dots; ++i) {
    draw_glyph(u8g2, x, y, kGlyphDot);
    x += 4;
  }
}

void draw_bt(u8g2_t *u8g2, int x, int y) {
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + 3), (u8g2_uint_t)y, (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 10));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + 3), (u8g2_uint_t)y, (u8g2_uint_t)(x + 6), (u8g2_uint_t)(y + 3));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + 6), (u8g2_uint_t)(y + 3), (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 5));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 5), (u8g2_uint_t)(x + 6), (u8g2_uint_t)(y + 8));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + 6), (u8g2_uint_t)(y + 8), (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 10));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)(y + 3), (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 5));
  u8g2_DrawLine(u8g2, (u8g2_uint_t)(x + 3), (u8g2_uint_t)(y + 5), (u8g2_uint_t)x, (u8g2_uint_t)(y + 8));
}

void draw_play(u8g2_t *u8g2, int x, int y) {
  u8g2_DrawTriangle(u8g2, (int16_t)x, (int16_t)(y - 4), (int16_t)x, (int16_t)(y + 4), (int16_t)(x + 7), (int16_t)y);
}

void draw_pause(u8g2_t *u8g2, int x, int y) {
  u8g2_DrawBox(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)(y - 4), 2, 9);
  u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 4), (u8g2_uint_t)(y - 4), 2, 9);
}

void draw_seek(u8g2_t *u8g2, int x, int y, int direction) {
  if (direction > 0) {
    u8g2_DrawTriangle(u8g2, (int16_t)x, (int16_t)(y - 4), (int16_t)x, (int16_t)(y + 4), (int16_t)(x + 5),
                      (int16_t)y);
    u8g2_DrawTriangle(u8g2, (int16_t)(x + 5), (int16_t)(y - 4), (int16_t)(x + 5), (int16_t)(y + 4),
                      (int16_t)(x + 10), (int16_t)y);
    return;
  }
  u8g2_DrawTriangle(u8g2, (int16_t)(x + 10), (int16_t)(y - 4), (int16_t)(x + 10), (int16_t)(y + 4),
                    (int16_t)(x + 5), (int16_t)y);
  u8g2_DrawTriangle(u8g2, (int16_t)(x + 5), (int16_t)(y - 4), (int16_t)(x + 5), (int16_t)(y + 4), (int16_t)x,
                    (int16_t)y);
}

void draw_spokes(u8g2_t *u8g2, int cx, int cy, uint16_t angle) {
  for (int blade = 0; blade < 3; ++blade) {
    const uint16_t mid = (uint16_t)(angle + blade * 21845);
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    polar(cx, cy, kHubR, (uint16_t)(mid - kSpokeHalf), &x1, &y1);
    polar(cx, cy, kHubR, (uint16_t)(mid + kSpokeHalf), &x2, &y2);
    u8g2_DrawTriangle(u8g2, (int16_t)cx, (int16_t)cy, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2);
  }
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, 2, U8G2_DRAW_ALL);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawCircle(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, 2, U8G2_DRAW_ALL);
}

void draw_reel_filled(u8g2_t *u8g2, int cx, int cy, int radius, uint16_t angle) {
  int hole = radius - 3;
  if (hole < kHubR) {
    hole = kHubR;
  }
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)radius, U8G2_DRAW_ALL);
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)hole, U8G2_DRAW_ALL);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawCircle(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)hole, U8G2_DRAW_ALL);
  u8g2_DrawCircle(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)kHubR, U8G2_DRAW_ALL);
  draw_spokes(u8g2, cx, cy, angle);
}

void draw_reel_outline(u8g2_t *u8g2, int cx, int cy, int radius, uint16_t angle) {
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawCircle(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)radius, U8G2_DRAW_ALL);
  u8g2_DrawCircle(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)(radius - 2), U8G2_DRAW_ALL);
  u8g2_DrawCircle(u8g2, (u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)kHubR, U8G2_DRAW_ALL);
  draw_spokes(u8g2, cx, cy, angle);
}

void draw_tape_window(u8g2_t *u8g2, uint16_t angle, bool moving) {
  // A slot under the packs, with two strands of tape and the head opening.
  // No drop-lines down from a small reel: those read as poles, not tape.
  const int y = 43;
  const int x = 22;
  const int w = 84;
  u8g2_DrawFrame(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, 8);
  u8g2_DrawHLine(u8g2, (u8g2_uint_t)(x + 2), (u8g2_uint_t)(y + 2), (u8g2_uint_t)(w - 4));
  u8g2_DrawHLine(u8g2, (u8g2_uint_t)(x + 2), (u8g2_uint_t)(y + 5), (u8g2_uint_t)(w - 4));
  if (moving) {
    const int gap = static_cast<int>((angle / 1800) % static_cast<unsigned>(w - 10));
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 2 + gap), (u8g2_uint_t)(y + 2), 2, 4);
    u8g2_SetDrawColor(u8g2, 1);
  }
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, 56, (u8g2_uint_t)(y + 1), 16, 6);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawFrame(u8g2, 56, (u8g2_uint_t)(y + 1), 16, 6);
}

void draw_bottom_bars(u8g2_t *u8g2) {
  u8g2_DrawBox(u8g2, 12, 54, 104, 3);
  u8g2_DrawBox(u8g2, 12, 59, 104, 3);
}

void draw_volume(u8g2_t *u8g2, int pos, int pop) {
  if (pop < 1) {
    return;
  }
  if (pos < 0) {
    pos = 0;
  }
  if (pos > 1000) {
    pos = 1000;
  }
  const int fullH = 11;
  int h = fullH * pop / 255;
  if (h < 1) {
    h = 1;
  }
  const int w = 112;
  const int x = 8;
  const int y = 62 - h;
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawFrame(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
  if (h < 5) {
    return;
  }
  const int innerW = w - 4;
  const int fill = pos * innerW / 1000;
  const int fy = y + 2;
  const int fh = h - 4;
  if (fill > 0 && fh > 0) {
    u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 2), (u8g2_uint_t)fy, (u8g2_uint_t)fill, (u8g2_uint_t)fh);
  }
  // Segment gaps, so the bar reads as a meter while the edge still tracks the knob.
  u8g2_SetDrawColor(u8g2, 0);
  for (int i = 1; i < 16; ++i) {
    const int tick = x + 2 + i * innerW / 16;
    u8g2_DrawVLine(u8g2, (u8g2_uint_t)tick, (u8g2_uint_t)fy, (u8g2_uint_t)fh);
  }
  u8g2_SetDrawColor(u8g2, 1);
}

void draw_status(u8g2_t *u8g2, const UiView *view) {
  if (view->seek > 0) {
    draw_seek(u8g2, 59, 14, 1);
  } else if (view->seek < 0) {
    draw_seek(u8g2, 59, 14, -1);
  } else if (view->playing) {
    draw_play(u8g2, 61, 14);
  } else {
    draw_pause(u8g2, 61, 14);
  }
}

}  // namespace

void ui_render(u8g2_t *u8g2, const UiView *view) {
  u8g2_ClearBuffer(u8g2);
  u8g2_SetDrawColor(u8g2, 1);
  draw_shell(u8g2, 0, 0, 128, 64, 7);
  draw_shell(u8g2, 1, 1, 126, 62, 6);

  const bool pairing = !view->connected;
  const uint16_t spin = pairing ? (uint16_t)(view->nowMs * 14) : view->angle;
  const int leftAmount = pairing ? 650 : (1000 - view->tape);
  const int rightAmount = pairing ? 350 : view->tape;
  const int leftR = reel_radius(leftAmount);
  const int rightR = reel_radius(rightAmount);
  // Offset so the two hubs don't flash the same pose.
  const uint16_t rightSpin = (uint16_t)(spin + 14000);

  if (pairing) {
    draw_reel_outline(u8g2, kLeftX, kReelY, leftR, spin);
    draw_reel_outline(u8g2, kRightX, kReelY, rightR, rightSpin);
    const int dots = (int)((view->nowMs / 350) % 4);
    draw_pairing(u8g2, 52, dots);
  } else {
    draw_reel_filled(u8g2, kLeftX, kReelY, leftR, spin);
    draw_reel_filled(u8g2, kRightX, kReelY, rightR, rightSpin);
    const bool moving = view->playing || view->seek != 0;
    draw_tape_window(u8g2, spin, moving);
    if (view->volume) {
      draw_volume(u8g2, view->volumePos, view->volumePop);
    } else {
      draw_bottom_bars(u8g2);
    }
    draw_status(u8g2, view);
  }

  const bool blinkOn = ((view->nowMs / 320) % 2) == 0;
  if (view->connected || blinkOn) {
    draw_bt(u8g2, 116, 4);
  }
}
