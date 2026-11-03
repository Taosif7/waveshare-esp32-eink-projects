#include "ui.h"
#include "shell_xbm.h"

#include <cmath>

#ifndef U8G2_16BIT
#define U8G2_16BIT
#endif
#include <clib/u8g2.h>

namespace {

// Centres from the photo, in 128x64 pixels. The outlines stay put; the thick
// rings and the tape that leaves them move with the amount of tape.
constexpr int kLeftCx = 40;
constexpr int kLeftCy = 22;
constexpr int kRightCx = 96;
constexpr int kRightCy = 22;
constexpr int kThick = 3;
constexpr int kRailX = 8;
constexpr int kRailTop = 7;
constexpr int kRailBot = 43;
constexpr int kNodeLx = 44;
constexpr int kNodeLy = 45;
constexpr int kNodeRx = 89;
constexpr int kNodeRy = 45;
constexpr int kStepX = 75;
constexpr int kSegCount = 4;
constexpr int kSegX = 9;
constexpr int kSegW = 25;
constexpr int kSegGap = 3;
constexpr int kSegY = 55;
constexpr int kSegH = 6;

constexpr int16_t kSinQ[65] = {
    0,     402,   804,   1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,
    4370,  4756,  5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,
    8423,  8765,  9102,  9434,  9760,  10080, 10394, 10702, 11003, 11297, 11585,
    11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256,
    14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893,
    15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};

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
    value = static_cast<int16_t>(-kSinQ[index]);
  } else if (quadrant == 3) {
    value = static_cast<int16_t>(-kSinQ[64 - index]);
  }
  return value;
}

int16_t icos(uint16_t angle) {
  return isin(static_cast<uint16_t>(angle + 16384));
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

int left_ring(int amount) {
  return clamp_amount(amount) >= 200 ? 16 : 15;
}

int right_ring(int amount) {
  return clamp_amount(amount) >= 500 ? 12 : 11;
}

void line(u8g2_t *u8g2, int x0, int y0, int x1, int y1) {
  if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0) {
    return;
  }
  u8g2_DrawLine(u8g2, static_cast<u8g2_uint_t>(x0), static_cast<u8g2_uint_t>(y0), static_cast<u8g2_uint_t>(x1),
                static_cast<u8g2_uint_t>(y1));
}

int div_round(int64_t num, int64_t den) {
  if (den < 0) {
    num = -num;
    den = -den;
  }
  if (num >= 0) {
    return static_cast<int>((num + den / 2) / den);
  }
  return static_cast<int>((num - den / 2) / den);
}

int isqrt64(int64_t n) {
  if (n <= 0) {
    return 0;
  }
  int64_t x = n;
  int64_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return static_cast<int>(x);
}

void touch_from_point(int cx, int cy, int radius, int px, int py, int side, int *tx, int *ty) {
  const int dx = px - cx;
  const int dy = py - cy;
  const int64_t d2 = static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy;
  const int64_t r2 = static_cast<int64_t>(radius) * radius;
  if (d2 <= r2) {
    *tx = cx;
    *ty = cy + radius;
    return;
  }
  const int length = isqrt64(d2 - r2);
  const int64_t nx = r2 * dx - static_cast<int64_t>(side) * radius * length * dy;
  const int64_t ny = r2 * dy + static_cast<int64_t>(side) * radius * length * dx;
  *tx = cx + div_round(nx, d2);
  *ty = cy + div_round(ny, d2);
}

bool lit(u8g2_t *u8g2, int x, int y) {
  if (x < 0 || y < 0 || x >= 128 || y >= 64) {
    return false;
  }
  const uint8_t *buffer = u8g2_GetBufferPtr(u8g2);
  return (buffer[(y >> 3) * 128 + x] >> (y & 7)) & 1;
}

void snap_near(u8g2_t *u8g2, int cx, int cy, int radius, int tx, int ty, int *ox, int *oy) {
  int best = 1000000000;
  int bx = tx;
  int by = ty;
  const int target = radius * radius;
  for (int y = ty - 3; y <= ty + 3; ++y) {
    for (int x = tx - 3; x <= tx + 3; ++x) {
      if (!lit(u8g2, x, y)) {
        continue;
      }
      const int dx = x - cx;
      const int dy = y - cy;
      int radial = dx * dx + dy * dy - target;
      if (radial < 0) {
        radial = -radial;
      }
      if (radial > radius * 3) {
        continue;
      }
      const int ddx = x - tx;
      const int ddy = y - ty;
      const int score = ddx * ddx + ddy * ddy + radial;
      if (score < best) {
        best = score;
        bx = x;
        by = y;
      }
    }
  }
  *ox = bx;
  *oy = by;
}

void draw_ngon(u8g2_t *u8g2, int cx, int cy, int radius, int sides, uint16_t rotation) {
  const uint16_t step = static_cast<uint16_t>(65536 / sides);
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  polar(cx, cy, radius, rotation, &x0, &y0);
  for (int i = 1; i <= sides; ++i) {
    polar(cx, cy, radius, static_cast<uint16_t>(rotation + step * i), &x1, &y1);
    line(u8g2, x0, y0, x1, y1);
    x0 = x1;
    y0 = y1;
  }
}

void draw_dashed_circle(u8g2_t *u8g2, int cx, int cy, int radius) {
  // Short dashes. 192 steps is finer than a pixel, and 3-on 2-off reads as a dotted ring.
  int prevX = -1;
  int prevY = -1;
  int drawn = 0;
  for (int i = 0; i < 192; ++i) {
    int x = 0;
    int y = 0;
    polar(cx, cy, radius, static_cast<uint16_t>(i * (65536 / 192)), &x, &y);
    if (x == prevX && y == prevY) {
      continue;
    }
    prevX = x;
    prevY = y;
    if ((drawn % 5) < 3) {
      u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
    }
    ++drawn;
  }
}

void draw_node(u8g2_t *u8g2, int x, int y) {
  // Hollow ring about 3px across.
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x - 1), static_cast<u8g2_uint_t>(y));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(y));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y - 1));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y + 1));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x - 1), static_cast<u8g2_uint_t>(y - 1));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(y - 1));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x - 1), static_cast<u8g2_uint_t>(y + 1));
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(y + 1));
}

void draw_shell_art(u8g2_t *u8g2) {
  // Slider rail and the small foot. The knob is drawn later, on top.
  line(u8g2, kRailX, kRailTop, kRailX, kRailBot);
  line(u8g2, kRailX - 1, kRailBot + 1, kRailX + 1, kRailBot + 1);

  // Nearly round 16-gon, a couple of pixels outside the thick ring. A vertex
  // points left, so the outline starts at x=22.
  draw_ngon(u8g2, kLeftCx, kLeftCy, 19, 16, 0);

  // Dotted outer circle around the right reel. The thick ring inside it moves.
  draw_dashed_circle(u8g2, kRightCx, kRightCy, 19);

  draw_node(u8g2, kNodeLx, kNodeLy);
  draw_node(u8g2, kNodeRx, kNodeRy);

  // Filled head, with a 1px trapezoid roof that meets the block so the
  // roof does not enclose a hole. The tape line runs along the roof.
  u8g2_DrawBox(u8g2, 62, 41, 12, 4);
  line(u8g2, 56, 40, 80, 40);
  line(u8g2, 56, 40, 62, 41);
  line(u8g2, 80, 40, 74, 41);

  for (int i = 0; i < kSegCount; ++i) {
    const int x = kSegX + i * (kSegW + kSegGap);
    u8g2_DrawFrame(u8g2, static_cast<u8g2_uint_t>(x), kSegY, kSegW, kSegH);
  }
}

void draw_knob(u8g2_t *u8g2, int volumePos) {
  if (volumePos < 0) {
    volumePos = 0;
  }
  if (volumePos > 1000) {
    volumePos = 1000;
  }
  // 5x2 bar. Full sits on the top of the rail.
  const int travel = kRailBot - kRailTop - 1;
  const int y = kRailBot - 1 - volumePos * travel / 1000;
  u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(kRailX - 2), static_cast<u8g2_uint_t>(y), 5, 2);
}

void draw_ring(u8g2_t *u8g2, int cx, int cy, int outer) {
  // Solid 3px band. Concentric Bresenham circles leave holes, which reads as a thin ring.
  const int inner = outer - kThick + 1;
  const int outer2 = outer * outer + outer;
  const int inner2 = inner * inner - inner;
  for (int y = cy - outer - 1; y <= cy + outer + 1; ++y) {
    for (int x = cx - outer - 1; x <= cx + outer + 1; ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      const int d2 = dx * dx + dy * dy;
      if (d2 <= outer2 && d2 >= inner2) {
        u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
      }
    }
  }
}

void draw_soft_square_ring(u8g2_t *u8g2, int cx, int cy, int outer) {
  // Superellipse, only a little squarer than a circle. n=2 is round, n=2.4 flattens the sides.
  constexpr float kPower = 2.15f;
  const int inner = outer - kThick + 1;
  for (int y = cy - outer - 1; y <= cy + outer + 1; ++y) {
    for (int x = cx - outer - 1; x <= cx + outer + 1; ++x) {
      const float ax = std::fabs(static_cast<float>(x - cx));
      const float ay = std::fabs(static_cast<float>(y - cy));
      const float scale = std::pow(std::pow(ax, kPower) + std::pow(ay, kPower), 1.f / kPower);
      if (scale <= static_cast<float>(outer) + 0.45f && scale >= static_cast<float>(inner) - 0.45f) {
        u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
      }
    }
  }
}

int hub_gap(int x, int y, int cx, int cy) {
  const int ax = x >= cx ? x - cx : cx - x;
  const int ay = y >= cy ? y - cy : cy - y;
  const int dx = ax > 2 ? ax - 2 : 0;
  const int dy = ay > 2 ? ay - 2 : 0;
  return dx > dy ? dx : dy;
}

void draw_bar_spoke(u8g2_t *u8g2, int cx, int cy, int r0, int r1, int ring_outer, int width, uint16_t angle) {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  polar(cx, cy, r0, angle, &x0, &y0);
  polar(cx, cy, r1, angle, &x1, &y1);
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int adx = dx < 0 ? -dx : dx;
  const int ady = dy < 0 ? -dy : dy;
  const int ox = ady >= adx ? 1 : 0;
  const int oy = ox == 0 ? 1 : 0;
  const int half = width / 2;
  const int ring_inner = ring_outer - kThick + 1;
  // One pixel inside the inner stroke. Combined with the bar width this stays
  // off the ring, and the requested length still fits the smaller reel.
  const int ring_limit = ring_inner - 1;
  const int ring_limit2 = ring_limit * ring_limit;
  for (int offset = -half; offset <= width - half - 1; ++offset) {
    int xa = x0 + ox * offset;
    int ya = y0 + oy * offset;
    int xb = x1 + ox * offset;
    int yb = y1 + oy * offset;
    const int steps_x = xb >= xa ? xb - xa : xa - xb;
    const int steps_y = yb >= ya ? yb - ya : ya - yb;
    int steps = steps_x > steps_y ? steps_x : steps_y;
    if (steps < 1) {
      steps = 1;
    }
    for (int s = 0; s <= steps; ++s) {
      const int x = xa + (xb - xa) * s / steps;
      const int y = ya + (yb - ya) * s / steps;
      // Two clear pixels outside the hub square.
      if (hub_gap(x, y, cx, cy) < 3) {
        continue;
      }
      const int rdx = x - cx;
      const int rdy = y - cy;
      if (rdx * rdx + rdy * rdy >= ring_limit2) {
        continue;
      }
      if (x >= 0 && y >= 0) {
        u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
      }
    }
  }
}

void spoke_radii(int ring_outer, int length, int *r0, int *r1) {
  // The bar is drawn at this length. Pixels that would land within 2px of the
  // hub or the inner edge of the ring are dropped, so the bar stays floating.
  (void)ring_outer;
  *r0 = 5;
  *r1 = 5 + length - 1;
  if (*r1 < *r0) {
    *r1 = *r0;
  }
}

void draw_spokes(u8g2_t *u8g2, int cx, int cy, int ring, int length, int width, uint16_t angle) {
  int r0 = 0;
  int r1 = 0;
  spoke_radii(ring, length, &r0, &r1);
  for (int blade = 0; blade < 3; ++blade) {
    draw_bar_spoke(u8g2, cx, cy, r0, r1, ring, width, static_cast<uint16_t>(angle + blade * 21845));
  }
}

void draw_hub(u8g2_t *u8g2, int cx, int cy) {
  u8g2_DrawFrame(u8g2, static_cast<u8g2_uint_t>(cx - 2), static_cast<u8g2_uint_t>(cy - 2), 5, 5);
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy));
}

void draw_tape(u8g2_t *u8g2, int leftR, int rightR) {
  const int leftTop = kLeftCy - leftR;
  // Horizontal run, then a step at x=75 down onto the right pack.
  line(u8g2, kLeftCx, leftTop, kStepX, leftTop);
  int landX = 0;
  int landY = 0;
  int otherX = 0;
  int otherY = 0;
  touch_from_point(kRightCx, kRightCy, rightR, kStepX, leftTop, 1, &landX, &landY);
  touch_from_point(kRightCx, kRightCy, rightR, kStepX, leftTop, -1, &otherX, &otherY);
  if (otherY < landY) {
    landX = otherX;
    landY = otherY;
  }
  int snapX = 0;
  int snapY = 0;
  snap_near(u8g2, kRightCx, kRightCy, rightR, landX, landY, &snapX, &snapY);
  line(u8g2, kStepX, leftTop, snapX, snapY);

  // Left tape leaves the pack at 8 o'clock and stays outside it on the way to the node.
  int leftX = 0;
  int leftY = 0;
  polar(kLeftCx, kLeftCy, leftR, 27306, &leftX, &leftY);
  int leftSnapX = 0;
  int leftSnapY = 0;
  snap_near(u8g2, kLeftCx, kLeftCy, leftR, leftX, leftY, &leftSnapX, &leftSnapY);
  const int bendX = leftSnapX + 3;
  const int bendY = leftSnapY + 6;
  line(u8g2, leftSnapX, leftSnapY, bendX, bendY);
  line(u8g2, bendX, bendY, kNodeLx, kNodeLy);

  line(u8g2, kNodeLx, kNodeLy, 56, 40);
  line(u8g2, 56, 40, 80, 40);
  line(u8g2, 80, 40, kNodeRx, kNodeRy);

  int rightX = 0;
  int rightY = 0;
  int right2X = 0;
  int right2Y = 0;
  touch_from_point(kRightCx, kRightCy, rightR, kNodeRx, kNodeRy, 1, &rightX, &rightY);
  touch_from_point(kRightCx, kRightCy, rightR, kNodeRx, kNodeRy, -1, &right2X, &right2Y);
  if (right2X > rightX) {
    rightX = right2X;
    rightY = right2Y;
  }
  int lowX = 0;
  int lowY = 0;
  snap_near(u8g2, kRightCx, kRightCy, rightR, rightX, rightY, &lowX, &lowY);
  line(u8g2, kNodeRx, kNodeRy, lowX, lowY);

  draw_node(u8g2, kNodeLx, kNodeLy);
  draw_node(u8g2, kNodeRx, kNodeRy);
}

void draw_chevron(u8g2_t *u8g2, int direction) {
  if (direction < 0) {
    line(u8g2, 72, 19, 69, 22);
    line(u8g2, 69, 22, 72, 25);
    return;
  }
  line(u8g2, 66, 19, 69, 22);
  line(u8g2, 69, 22, 66, 25);
}

void draw_pause_mark(u8g2_t *u8g2) {
  u8g2_DrawBox(u8g2, 66, 19, 2, 7);
  u8g2_DrawBox(u8g2, 70, 19, 2, 7);
}

void draw_playhead(u8g2_t *u8g2, int cx) {
  // 7px wide, two-pixel strokes, point one pixel above the segment.
  line(u8g2, cx - 3, 50, cx, 53);
  line(u8g2, cx + 3, 50, cx, 53);
  line(u8g2, cx - 3, 49, cx, 52);
  line(u8g2, cx + 3, 49, cx, 52);
}

void draw_segments(u8g2_t *u8g2, int tape) {
  if (tape < 0) {
    tape = 0;
  }
  if (tape > 1000) {
    tape = 1000;
  }
  const int span = 1000 / kSegCount;
  int active = tape / span;
  if (active > kSegCount - 1) {
    active = kSegCount - 1;
  }
  const int into = tape - active * span;

  for (int i = 0; i <= active; ++i) {
    const int x = kSegX + i * (kSegW + kSegGap);
    u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(kSegY + 1),
                 static_cast<u8g2_uint_t>(kSegW - 2), static_cast<u8g2_uint_t>(kSegH - 2));
    // 4x2 hole centred in the fill, with white pixels on every side of it.
    int window = (kSegW - 4) / 2;
    if (i == active) {
      window = 3 + into * (kSegW - 10) / span;
    }
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x + window), static_cast<u8g2_uint_t>(kSegY + 2), 4, 2);
    u8g2_SetDrawColor(u8g2, 1);
  }

  const int vx = kSegX + active * (kSegW + kSegGap) + kSegW / 2;
  draw_playhead(u8g2, vx);
}

void draw_glyph(u8g2_t *u8g2, int x, int y, const uint8_t *rows) {
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = rows[row];
    for (int col = 0; col < 5; ++col) {
      if ((bits & (0x10 >> col)) != 0) {
        u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x + col), static_cast<u8g2_uint_t>(y + row));
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
  line(u8g2, x + 3, y, x + 3, y + 10);
  line(u8g2, x + 3, y, x + 6, y + 3);
  line(u8g2, x + 6, y + 3, x + 3, y + 5);
  line(u8g2, x + 3, y + 5, x + 6, y + 8);
  line(u8g2, x + 6, y + 8, x + 3, y + 10);
  line(u8g2, x, y + 3, x + 3, y + 5);
  line(u8g2, x + 3, y + 5, x, y + 8);
}

}  // namespace

void ui_draw_static(u8g2_t *u8g2) {
  draw_shell_art(u8g2);
}

void ui_render(u8g2_t *u8g2, const UiView *view) {
  u8g2_ClearBuffer(u8g2);
  u8g2_SetDrawColor(u8g2, 1);
  // Baked from ui_draw_static. Regenerate src/shell_xbm.h from build/shell.pbm after changing it.
  u8g2_DrawXBMP(u8g2, 0, 0, kShellWidth, kShellHeight, kShellBits);

  const bool pairing = !view->connected;
  const uint16_t spin = pairing ? static_cast<uint16_t>(view->nowMs * 14) : view->angle;
  const int leftAmount = pairing ? 1000 : (1000 - view->tape);
  const int rightAmount = pairing ? 0 : view->tape;
  // Keeps the right pack's spokes off the left pack's phase.
  const uint16_t rightSpin = static_cast<uint16_t>(spin + 14000);
  const int leftR = left_ring(leftAmount);
  const int rightR = right_ring(rightAmount);

  draw_ring(u8g2, kLeftCx, kLeftCy, leftR);
  draw_soft_square_ring(u8g2, kRightCx, kRightCy, rightR);
  draw_spokes(u8g2, kLeftCx, kLeftCy, leftR, 8, 3, spin);
  draw_spokes(u8g2, kRightCx, kRightCy, rightR, 6, 3, rightSpin);
  draw_hub(u8g2, kLeftCx, kLeftCy);
  draw_hub(u8g2, kRightCx, kRightCy);
  draw_tape(u8g2, leftR, rightR);
  draw_knob(u8g2, view->volumePos);

  if (pairing) {
    const int dots = static_cast<int>((view->nowMs / 350) % 4);
    draw_pairing(u8g2, 56, dots);
  } else {
    draw_segments(u8g2, view->tape);
    if (view->seek < 0) {
      draw_chevron(u8g2, -1);
    } else if (!view->playing && view->seek == 0) {
      draw_pause_mark(u8g2);
    } else {
      draw_chevron(u8g2, 1);
    }
  }

  // The mark is only for pairing. A connected playing screen has no icon.
  if (!view->connected && ((view->nowMs / 320) % 2) == 0) {
    draw_bt(u8g2, 119, 0);
  }
}
