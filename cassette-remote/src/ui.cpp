#include "ui.h"

// Match U8g2lib.h. The draw functions are compiled with a 16-bit coordinate
// type; including the C header without this typedefs a different one.
#ifndef U8G2_16BIT
#define U8G2_16BIT
#endif
#include <clib/u8g2.h>

namespace {

// Fixed shell from the norns cassette: octagon on the left, rounded housing
// on the right, both bottoms at y=42 so the tape path can join them.
constexpr int kLeftCx = 34;
constexpr int kRightCx = 95;
constexpr int kReelCy = 25;
constexpr int kRadMin = 10;
constexpr int kRadMax = 15;
constexpr int kSpokeTip = 8;
constexpr int kRightX = 74;
constexpr int kRightY = 6;
constexpr int kRightW = 42;
constexpr int kRightH = 37;
constexpr int kRightR = 13;

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
  return kRadMin + (kRadMax - kRadMin) * amount / 1000;
}

void line(u8g2_t *u8g2, int x0, int y0, int x1, int y1) {
  if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0) {
    return;
  }
  u8g2_DrawLine(u8g2, static_cast<u8g2_uint_t>(x0), static_cast<u8g2_uint_t>(y0), static_cast<u8g2_uint_t>(x1),
                static_cast<u8g2_uint_t>(y1));
}

// Far-left cassette spine: a vertical bar with caps.
void draw_spine(u8g2_t *u8g2) {
  line(u8g2, 1, 8, 8, 8);
  line(u8g2, 2, 9, 7, 9);
  line(u8g2, 4, 10, 4, 45);
  line(u8g2, 5, 10, 5, 45);
  line(u8g2, 2, 46, 7, 46);
  line(u8g2, 1, 47, 8, 47);
}

// Angular housing. The top-right corner is where the roof bridge leaves.
void draw_left_housing(u8g2_t *u8g2) {
  line(u8g2, 18, 6, 48, 6);
  line(u8g2, 48, 6, 56, 14);
  line(u8g2, 56, 14, 56, 34);
  line(u8g2, 56, 34, 48, 42);
  line(u8g2, 48, 42, 18, 42);
  line(u8g2, 18, 42, 10, 34);
  line(u8g2, 10, 34, 10, 14);
  line(u8g2, 10, 14, 18, 6);
}

void draw_right_housing(u8g2_t *u8g2) {
  const int x = kRightX;
  const int y = kRightY;
  const int w = kRightW;
  const int h = kRightH;
  const int r = kRightR;
  u8g2_DrawHLine(u8g2, static_cast<u8g2_uint_t>(x + r), static_cast<u8g2_uint_t>(y),
                 static_cast<u8g2_uint_t>(w - 2 * r));
  u8g2_DrawHLine(u8g2, static_cast<u8g2_uint_t>(x + r), static_cast<u8g2_uint_t>(y + h - 1),
                 static_cast<u8g2_uint_t>(w - 2 * r));
  u8g2_DrawVLine(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y + r),
                 static_cast<u8g2_uint_t>(h - 2 * r));
  u8g2_DrawVLine(u8g2, static_cast<u8g2_uint_t>(x + w - 1), static_cast<u8g2_uint_t>(y + r),
                 static_cast<u8g2_uint_t>(h - 2 * r));
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(x + r), static_cast<u8g2_uint_t>(y + r), static_cast<u8g2_uint_t>(r),
                  U8G2_DRAW_UPPER_LEFT);
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(x + w - 1 - r), static_cast<u8g2_uint_t>(y + r),
                  static_cast<u8g2_uint_t>(r), U8G2_DRAW_UPPER_RIGHT);
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(x + r), static_cast<u8g2_uint_t>(y + h - 1 - r),
                  static_cast<u8g2_uint_t>(r), U8G2_DRAW_LOWER_LEFT);
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(x + w - 1 - r), static_cast<u8g2_uint_t>(y + h - 1 - r),
                  static_cast<u8g2_uint_t>(r), U8G2_DRAW_LOWER_RIGHT);
}

// One path: roof between the housings, then the floor that drops into the head notch.
void draw_tape_path(u8g2_t *u8g2) {
  const int roofL = 48;
  const int roofR = kRightX + kRightR;
  const int floorY = kRightY + kRightH - 1;
  line(u8g2, roofL, kRightY, roofR, kRightY);
  line(u8g2, 48, floorY, 57, floorY);
  line(u8g2, 57, floorY, 57, floorY + 6);
  line(u8g2, 57, floorY + 6, 73, floorY + 6);
  line(u8g2, 73, floorY + 6, 73, floorY);
  line(u8g2, 73, floorY, roofR, floorY);
  // Small v under the head.
  const int vx = 65;
  const int vy = floorY + 8;
  line(u8g2, vx - 3, vy, vx, vy + 3);
  line(u8g2, vx, vy + 3, vx + 3, vy);
}

void draw_segments(u8g2_t *u8g2) {
  constexpr int kCount = 7;
  constexpr int kGap = 3;
  constexpr int kX0 = 16;
  constexpr int kX1 = 116;
  const int width = (kX1 - kX0 - (kCount - 1) * kGap) / kCount;
  for (int i = 0; i < kCount; ++i) {
    const int x = kX0 + i * (width + kGap);
    u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x), 58, static_cast<u8g2_uint_t>(width), 4);
  }
}

void draw_thick_spoke(u8g2_t *u8g2, int cx, int cy, uint16_t angle) {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  polar(cx, cy, 1, angle, &x0, &y0);
  polar(cx, cy, kSpokeTip, angle, &x1, &y1);
  line(u8g2, x0, y0, x1, y1);
  int dx = x1 - x0;
  int dy = y1 - y0;
  if (dx < 0) {
    dx = -dx;
  }
  if (dy < 0) {
    dy = -dy;
  }
  const int ox = dx >= dy ? 0 : 1;
  const int oy = dx >= dy ? 1 : 0;
  line(u8g2, x0 + ox, y0 + oy, x1 + ox, y1 + oy);
}

void draw_reel(u8g2_t *u8g2, int cx, int cy, int amount, uint16_t angle) {
  const int outer = reel_radius(amount);
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy), static_cast<u8g2_uint_t>(outer),
                  U8G2_DRAW_ALL);
  // A second circle turns the pack into a ring once there is enough tape.
  if (outer >= kRadMin + 3) {
    u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy),
                    static_cast<u8g2_uint_t>(kRadMin), U8G2_DRAW_ALL);
  }
  for (int blade = 0; blade < 3; ++blade) {
    draw_thick_spoke(u8g2, cx, cy, static_cast<uint16_t>(angle + blade * 21845));
  }
  // Square hub stays put. Spokes are what turn; the punch keeps them from filling the square.
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(cx - 2), static_cast<u8g2_uint_t>(cy - 2), 5, 5);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawFrame(u8g2, static_cast<u8g2_uint_t>(cx - 2), static_cast<u8g2_uint_t>(cy - 2), 5, 5);
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
  const int fullH = 8;
  int h = fullH * pop / 255;
  if (h < 1) {
    h = 1;
  }
  const int w = 104;
  const int x = 14;
  const int y = 63 - h;
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w),
               static_cast<u8g2_uint_t>(h));
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawFrame(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w),
                 static_cast<u8g2_uint_t>(h));
  if (h < 5) {
    return;
  }
  const int innerW = w - 4;
  const int fill = pos * innerW / 1000;
  const int fy = y + 2;
  const int fh = h - 4;
  if (fill > 0 && fh > 0) {
    u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x + 2), static_cast<u8g2_uint_t>(fy), static_cast<u8g2_uint_t>(fill),
                 static_cast<u8g2_uint_t>(fh));
  }
  u8g2_SetDrawColor(u8g2, 0);
  for (int i = 1; i < 16; ++i) {
    const int tick = x + 2 + i * innerW / 16;
    u8g2_DrawVLine(u8g2, static_cast<u8g2_uint_t>(tick), static_cast<u8g2_uint_t>(fy), static_cast<u8g2_uint_t>(fh));
  }
  u8g2_SetDrawColor(u8g2, 1);
}

void draw_status(u8g2_t *u8g2, const UiView *view) {
  const int x = (kLeftCx + kRightCx) / 2 - 5;
  const int y = 18;
  if (view->seek > 0) {
    draw_seek(u8g2, x, y, 1);
  } else if (view->seek < 0) {
    draw_seek(u8g2, x, y, -1);
  } else if (view->playing) {
    draw_play(u8g2, x + 2, y);
  } else {
    draw_pause(u8g2, x + 2, y);
  }
}

}  // namespace

void ui_render(u8g2_t *u8g2, const UiView *view) {
  u8g2_ClearBuffer(u8g2);
  u8g2_SetDrawColor(u8g2, 1);

  const bool pairing = !view->connected;
  const uint16_t spin = pairing ? static_cast<uint16_t>(view->nowMs * 14) : view->angle;
  const int leftAmount = pairing ? 700 : (1000 - view->tape);
  const int rightAmount = pairing ? 400 : view->tape;
  const uint16_t rightSpin = static_cast<uint16_t>(spin + 14000);

  draw_spine(u8g2);
  draw_left_housing(u8g2);
  draw_right_housing(u8g2);
  draw_tape_path(u8g2);
  draw_reel(u8g2, kLeftCx, kReelCy, leftAmount, spin);
  draw_reel(u8g2, kRightCx, kReelCy, rightAmount, rightSpin);

  if (pairing) {
    const int dots = static_cast<int>((view->nowMs / 350) % 4);
    draw_pairing(u8g2, 56, dots);
  } else if (view->volume) {
    draw_volume(u8g2, view->volumePos, view->volumePop);
    draw_status(u8g2, view);
  } else {
    draw_segments(u8g2);
    draw_status(u8g2, view);
  }

  const bool blinkOn = ((view->nowMs / 320) % 2) == 0;
  if (view->connected || blinkOn) {
    draw_bt(u8g2, 119, 8);
  }
}
