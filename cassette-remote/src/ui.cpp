#include "ui.h"

// Match U8g2lib.h. The draw functions are compiled with a 16-bit coordinate
// type; including the C header without this typedefs a different one.
#ifndef U8G2_16BIT
#define U8G2_16BIT
#endif
#include <clib/u8g2.h>

namespace {

// The right reel sits lower and smaller, so the roof line slopes down to it.
constexpr int kLeftCx = 32;
constexpr int kLeftCy = 22;
constexpr int kRightCx = 100;
constexpr int kRightCy = 33;

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

int clamp_amount(int amount) {
  if (amount < 0) {
    return 0;
  }
  if (amount > 1000) {
    return 1000;
  }
  return amount;
}

// Left stays larger than the right even when the tape has run all the way across.
int left_outer(int amount) {
  return 14 + clamp_amount(amount) * 3 / 1000;
}

int right_outer(int amount) {
  return 10 + clamp_amount(amount) * 2 / 1000;
}

int isqrt(int n) {
  if (n <= 0) {
    return 0;
  }
  int x = n;
  int y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

// Angle whose cosine is num/den, in the same 65536-per-turn units as isin.
uint16_t acos_turn(int num, int den) {
  if (den <= 0 || num >= den) {
    return 0;
  }
  if (num <= -den) {
    return 32768;
  }
  uint16_t lo = 0;
  uint16_t hi = 32768;
  while (hi - lo > 1) {
    const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    const int64_t cosine = static_cast<int64_t>(isin(static_cast<uint16_t>(mid + 16384))) * den;
    const int64_t target = static_cast<int64_t>(num) * 16384;
    if (cosine > target) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

void rotate_vec(int dx, int dy, uint16_t angle, int *ox, int *oy) {
  const int c = icos(angle);
  const int s = isin(angle);
  *ox = static_cast<int>((static_cast<int64_t>(dx) * c - static_cast<int64_t>(dy) * s) / 16384);
  *oy = static_cast<int>((static_cast<int64_t>(dx) * s + static_cast<int64_t>(dy) * c) / 16384);
}

// Upper tangent from a point on the left outline to the smaller right circle.
bool tangent_from_point(int px, int py, int cx, int cy, int radius, int *tx, int *ty) {
  const int dx = px - cx;
  const int dy = py - cy;
  const int dist = isqrt(dx * dx + dy * dy);
  if (dist <= radius) {
    return false;
  }
  const uint16_t beta = acos_turn(radius, dist);
  int best = 9999;
  int bx = cx;
  int by = cy;
  for (int side = 0; side < 2; ++side) {
    const uint16_t angle = side == 0 ? beta : static_cast<uint16_t>(-static_cast<int>(beta));
    int rx = 0;
    int ry = 0;
    rotate_vec(dx, dy, angle, &rx, &ry);
    const int x = cx + static_cast<int>(static_cast<int64_t>(rx) * radius / dist);
    const int y = cy + static_cast<int>(static_cast<int64_t>(ry) * radius / dist);
    if (y < best) {
      best = y;
      bx = x;
      by = y;
    }
  }
  *tx = bx;
  *ty = by;
  return true;
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
  line(u8g2, 1, 6, 8, 6);
  line(u8g2, 2, 7, 7, 7);
  line(u8g2, 4, 8, 4, 44);
  line(u8g2, 5, 8, 5, 44);
  line(u8g2, 2, 45, 7, 45);
  line(u8g2, 1, 46, 8, 46);
}

// Octagon around the left reel, open on the side that faces the tape path.
void draw_left_housing(u8g2_t *u8g2, int radius, int *joinX, int *joinY, int *lowX, int *lowY) {
  int vx[8];
  int vy[8];
  for (int i = 0; i < 8; ++i) {
    const uint16_t angle = static_cast<uint16_t>(4096 + i * 8192);
    polar(kLeftCx, kLeftCy, radius, angle, &vx[i], &vy[i]);
  }
  // Vertex 6 sits near the top-right. The sloping tape line leaves from there,
  // so the upper-right facet is the tape path instead of a flat roof.
  // Vertex 1 is the lower-right corner the bottom diagonal leaves from.
  for (int i = 1; i < 6; ++i) {
    line(u8g2, vx[i], vy[i], vx[i + 1], vy[i + 1]);
  }
  *joinX = vx[6];
  *joinY = vy[6];
  *lowX = vx[1];
  *lowY = vy[1];
}

void draw_shell(u8g2_t *u8g2, int leftHousing, int rightHousing) {
  int joinX = 0;
  int joinY = 0;
  int lowX = 0;
  int lowY = 0;
  draw_left_housing(u8g2, leftHousing, &joinX, &joinY, &lowX, &lowY);
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(kRightCx), static_cast<u8g2_uint_t>(kRightCy),
                  static_cast<u8g2_uint_t>(rightHousing), U8G2_DRAW_ALL);

  int touchX = 0;
  int touchY = 0;
  if (tangent_from_point(joinX, joinY, kRightCx, kRightCy, rightHousing, &touchX, &touchY)) {
    line(u8g2, joinX, joinY, touchX, touchY);
  }

  // Diagonals fall inward to the head notch instead of running flat under the reels.
  const int notchL = 58;
  const int notchR = 74;
  const int notchTop = 46;
  const int notchBot = 52;
  int lowRx = 0;
  int lowRy = 0;
  polar(kRightCx, kRightCy, rightHousing, 24576, &lowRx, &lowRy);
  line(u8g2, lowX, lowY, notchL, notchTop);
  line(u8g2, lowRx, lowRy, notchR, notchTop);
  line(u8g2, notchL, notchTop, notchL, notchBot);
  line(u8g2, notchL, notchBot, notchR, notchBot);
  line(u8g2, notchR, notchBot, notchR, notchTop);
  const int vx = (notchL + notchR) / 2;
  line(u8g2, vx - 3, notchBot + 2, vx, notchBot + 5);
  line(u8g2, vx, notchBot + 5, vx + 3, notchBot + 2);
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

void draw_thick_spoke(u8g2_t *u8g2, int cx, int cy, int length, uint16_t angle) {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  polar(cx, cy, 1, angle, &x0, &y0);
  polar(cx, cy, length, angle, &x1, &y1);
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

void draw_reel(u8g2_t *u8g2, int cx, int cy, int outer, uint16_t angle) {
  // Thin outer circle, a 2px gap, then a 2–3px ring. Spokes stay inside that ring.
  const int ringOuter = outer - 3;
  int thick = outer >= 14 ? 3 : 2;
  int hole = ringOuter - thick;
  if (hole < 4) {
    hole = 4;
  }
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy), static_cast<u8g2_uint_t>(outer),
                  U8G2_DRAW_ALL);
  if (ringOuter > hole) {
    u8g2_DrawDisc(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy), static_cast<u8g2_uint_t>(ringOuter),
                  U8G2_DRAW_ALL);
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawDisc(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy), static_cast<u8g2_uint_t>(hole),
                  U8G2_DRAW_ALL);
    u8g2_SetDrawColor(u8g2, 1);
  }
  const int spokeLen = hole - 1;
  for (int blade = 0; blade < 3; ++blade) {
    draw_thick_spoke(u8g2, cx, cy, spokeLen, static_cast<uint16_t>(angle + blade * 21845));
  }
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
  const int x = (kLeftCx + kRightCx) / 2 - 4;
  const int y = 34;
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
  const int leftAmount = pairing ? 800 : (1000 - view->tape);
  const int rightAmount = pairing ? 250 : view->tape;
  const int leftR = left_outer(leftAmount);
  const int rightR = right_outer(rightAmount);
  const uint16_t rightSpin = static_cast<uint16_t>(spin + 14000);

  draw_spine(u8g2);
  draw_shell(u8g2, leftR + 3, rightR + 3);
  draw_reel(u8g2, kLeftCx, kLeftCy, leftR, spin);
  draw_reel(u8g2, kRightCx, kRightCy, rightR, rightSpin);

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
