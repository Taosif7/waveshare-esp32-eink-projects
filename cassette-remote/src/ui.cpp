#include "ui.h"

// Match U8g2lib.h. The draw functions are compiled with a 16-bit coordinate
// type; including the C header without this typedefs a different one.
#ifndef U8G2_16BIT
#define U8G2_16BIT
#endif
#include <clib/u8g2.h>

namespace {

// Centres from the close-up. Outer octagon and circle stay put; only the
// thick rings change size with the tape.
constexpr int kLeftCx = 34;
constexpr int kLeftCy = 22;
constexpr int kRightCx = 98;
constexpr int kRightCy = 22;

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

// Fixed shell. Tuned so the thick rings sit inside with a few pixels of gap.
constexpr int kOct[8][2] = {
    {26, 3}, {44, 3}, {54, 12}, {54, 32}, {44, 41}, {24, 41}, {14, 32}, {14, 12},
};

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

// The left ring stays near radius 16 and the right near 11, so the playing
// frame matches the close-up even after some tape has moved across.
int left_ring(int amount) {
  return clamp_amount(amount) >= 200 ? 16 : 15;
}

int right_ring(int amount) {
  return clamp_amount(amount) >= 800 ? 12 : 11;
}

void line(u8g2_t *u8g2, int x0, int y0, int x1, int y1) {
  if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0) {
    return;
  }
  u8g2_DrawLine(u8g2, static_cast<u8g2_uint_t>(x0), static_cast<u8g2_uint_t>(y0), static_cast<u8g2_uint_t>(x1),
                static_cast<u8g2_uint_t>(y1));
}

void draw_volume_slider(u8g2_t *u8g2, int volumePos) {
  if (volumePos < 0) {
    volumePos = 0;
  }
  if (volumePos > 1000) {
    volumePos = 1000;
  }
  line(u8g2, 4, 6, 4, 50);
  line(u8g2, 2, 6, 6, 6);
  line(u8g2, 2, 50, 6, 50);
  // Full is near the top cap, empty sits just above the bottom cap.
  const int thumbY = 46 - volumePos * 38 / 1000;
  u8g2_DrawBox(u8g2, 2, static_cast<u8g2_uint_t>(thumbY), 5, 3);
}

void draw_octagon(u8g2_t *u8g2) {
  for (int i = 0; i < 8; ++i) {
    const int next = (i + 1) % 8;
    line(u8g2, kOct[i][0], kOct[i][1], kOct[next][0], kOct[next][1]);
  }
}

bool lit(u8g2_t *u8g2, int x, int y) {
  if (x < 0 || y < 0 || x >= 128 || y >= 64) {
    return false;
  }
  const uint8_t *buffer = u8g2_GetBufferPtr(u8g2);
  return (buffer[(y >> 3) * 128 + x] >> (y & 7)) & 1;
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

// Tangent from an outside point to the circle. `side` is +1 or -1.
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

// Outer (external) tangent. Radii point the same way, so the tape stays outside both packs.
void outer_tangent(int x1, int y1, int r1, int x2, int y2, int r2, int side, int *ax, int *ay, int *bx,
                   int *by) {
  const int dx = x2 - x1;
  const int dy = y2 - y1;
  const int64_t d2 = static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy;
  const int rd = r1 - r2;
  const int64_t gap = d2 - static_cast<int64_t>(rd) * rd;
  const int hnum = gap > 0 ? isqrt64(gap) : 0;
  const int64_t nx = static_cast<int64_t>(rd) * dx - static_cast<int64_t>(side) * hnum * dy;
  const int64_t ny = static_cast<int64_t>(rd) * dy + static_cast<int64_t>(side) * hnum * dx;
  *ax = x1 + div_round(static_cast<int64_t>(r1) * nx, d2);
  *ay = y1 + div_round(static_cast<int64_t>(r1) * ny, d2);
  *bx = x2 + div_round(static_cast<int64_t>(r2) * nx, d2);
  *by = y2 + div_round(static_cast<int64_t>(r2) * ny, d2);
}

// The drawn ring is a Bresenham circle, so land on the pixel of that stroke nearest the ideal point.
void snap_ring(u8g2_t *u8g2, int cx, int cy, int radius, int tx, int ty, int *ox, int *oy) {
  const int target = radius * radius;
  int best = 1000000000;
  int bx = tx;
  int by = ty;
  for (int y = cy - radius - 2; y <= cy + radius + 2; ++y) {
    for (int x = cx - radius - 2; x <= cx + radius + 2; ++x) {
      if (!lit(u8g2, x, y)) {
        continue;
      }
      const int dx = x - cx;
      const int dy = y - cy;
      int radial = dx * dx + dy * dy - target;
      if (radial < 0) {
        radial = -radial;
      }
      // Bresenham's outer stroke wanders by about a radius in r^2. The next
      // circle in, and the decorative shell, sit past that.
      if (radial > radius) {
        continue;
      }
      const int ddx = x - tx;
      const int ddy = y - ty;
      const int score = ddx * ddx + ddy * ddy;
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

void draw_node(u8g2_t *u8g2, int x, int y) {
  u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x - 1), static_cast<u8g2_uint_t>(y - 1), 3, 3);
}

void draw_tape_lines(u8g2_t *u8g2, int leftR, int rightR) {
  // Rings are already on the buffer. Tangents use this frame's outer radii.
  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  int cx = 0;
  int cy = 0;
  int dx = 0;
  int dy = 0;
  outer_tangent(kLeftCx, kLeftCy, leftR, kRightCx, kRightCy, rightR, 1, &ax, &ay, &bx, &by);
  outer_tangent(kLeftCx, kLeftCy, leftR, kRightCx, kRightCy, rightR, -1, &cx, &cy, &dx, &dy);
  if (cy + dy < ay + by) {
    ax = cx;
    ay = cy;
    bx = dx;
    by = dy;
  }
  int topLx = 0;
  int topLy = 0;
  int topRx = 0;
  int topRy = 0;
  snap_ring(u8g2, kLeftCx, kLeftCy, leftR, ax, ay, &topLx, &topLy);
  snap_ring(u8g2, kRightCx, kRightCy, rightR, bx, by, &topRx, &topRy);
  line(u8g2, topLx, topLy, topRx, topRy);

  // Guide nodes and the head stay put. Each lower run is the outer tangent down to its node.
  constexpr int kNodeLx = 40;
  constexpr int kNodeLy = 48;
  constexpr int kNodeRx = 88;
  constexpr int kNodeRy = 48;
  constexpr int kLipL = 57;
  constexpr int kLipR = 70;
  constexpr int kLipY = 40;

  int l0x = 0;
  int l0y = 0;
  int l1x = 0;
  int l1y = 0;
  touch_from_point(kLeftCx, kLeftCy, leftR, kNodeLx, kNodeLy, 1, &l0x, &l0y);
  touch_from_point(kLeftCx, kLeftCy, leftR, kNodeLx, kNodeLy, -1, &l1x, &l1y);
  if (l1y > l0y) {
    l0x = l1x;
    l0y = l1y;
  }
  int lowLx = 0;
  int lowLy = 0;
  snap_ring(u8g2, kLeftCx, kLeftCy, leftR, l0x, l0y, &lowLx, &lowLy);
  line(u8g2, lowLx, lowLy, kNodeLx, kNodeLy);

  line(u8g2, kNodeLx, kNodeLy, kLipL, kLipY);
  u8g2_DrawHLine(u8g2, kLipL, kLipY, kLipR - kLipL + 1);
  u8g2_DrawBox(u8g2, 59, 41, 10, 6);
  line(u8g2, kLipR, kLipY, kNodeRx, kNodeRy);

  int r0x = 0;
  int r0y = 0;
  int r1x = 0;
  int r1y = 0;
  touch_from_point(kRightCx, kRightCy, rightR, kNodeRx, kNodeRy, 1, &r0x, &r0y);
  touch_from_point(kRightCx, kRightCy, rightR, kNodeRx, kNodeRy, -1, &r1x, &r1y);
  if (r1y > r0y) {
    r0x = r1x;
    r0y = r1y;
  }
  int lowRx = 0;
  int lowRy = 0;
  snap_ring(u8g2, kRightCx, kRightCy, rightR, r0x, r0y, &lowRx, &lowRy);
  line(u8g2, kNodeRx, kNodeRy, lowRx, lowRy);
  draw_node(u8g2, kNodeLx, kNodeLy);
  draw_node(u8g2, kNodeRx, kNodeRy);
}

void draw_chevron(u8g2_t *u8g2, int direction) {
  if (direction < 0) {
    line(u8g2, 66, 19, 63, 22);
    line(u8g2, 63, 22, 66, 25);
    return;
  }
  line(u8g2, 62, 19, 65, 22);
  line(u8g2, 65, 22, 62, 25);
}

void draw_pause_mark(u8g2_t *u8g2) {
  u8g2_DrawBox(u8g2, 61, 19, 2, 7);
  u8g2_DrawBox(u8g2, 65, 19, 2, 7);
}

void draw_bar_spoke(u8g2_t *u8g2, int cx, int cy, int r0, int r1, int width, uint16_t angle) {
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
  for (int offset = -half; offset <= width - half - 1; ++offset) {
    line(u8g2, x0 + ox * offset, y0 + oy * offset, x1 + ox * offset, y1 + oy * offset);
  }
}

void draw_hub(u8g2_t *u8g2, int cx, int cy) {
  u8g2_DrawFrame(u8g2, static_cast<u8g2_uint_t>(cx - 2), static_cast<u8g2_uint_t>(cy - 2), 5, 5);
  u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy));
}

void draw_ring(u8g2_t *u8g2, int cx, int cy, int outer, int thick) {
  // Concentric Bresenham circles, so the ring stays round instead of a filled blob.
  for (int i = 0; i < thick; ++i) {
    const int radius = outer - i;
    if (radius > 0) {
      u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy),
                      static_cast<u8g2_uint_t>(radius), U8G2_DRAW_ALL);
    }
  }
}

// The 5x5 hub reaches about 3px from the centre on its corners, and a 3px bar
// adds another pixel beside the centreline. Keep both of those inside the gap.
void spoke_radii(int ring_outer, int thick, int bar_half, int *r0, int *r1) {
  const int hub_edge = 3;
  const int ring_inner = ring_outer - thick + 1;
  // Extra pixel beyond the 2px gap because the bar is wider than its centreline
  // and polar rounding can step one pixel inward.
  const int inner_limit = hub_edge + 3 + bar_half;
  const int outer_limit = ring_inner - 3 - bar_half;
  const int span = ring_inner - hub_edge;
  int length = span / 2;
  if (length < 1) {
    length = 1;
  }
  if (outer_limit > inner_limit && length > outer_limit - inner_limit) {
    length = outer_limit - inner_limit;
  }
  if (outer_limit <= inner_limit) {
    // The hole is too small for a full gap. Park a stub in the middle.
    const int mid = (hub_edge + ring_inner) / 2;
    *r0 = mid;
    *r1 = mid + 1;
    return;
  }
  const int mid = (inner_limit + outer_limit) / 2;
  *r0 = mid - length / 2;
  *r1 = *r0 + length;
  if (*r0 < inner_limit) {
    *r1 += inner_limit - *r0;
    *r0 = inner_limit;
  }
  if (*r1 > outer_limit) {
    *r0 -= *r1 - outer_limit;
    *r1 = outer_limit;
  }
  if (*r0 < inner_limit) {
    *r0 = inner_limit;
  }
  if (*r1 <= *r0) {
    *r1 = *r0 + 1;
  }
}

void draw_spokes(u8g2_t *u8g2, int cx, int cy, int ring, int thick, int width, uint16_t angle) {
  int r0 = 0;
  int r1 = 0;
  spoke_radii(ring, thick, width / 2, &r0, &r1);
  for (int blade = 0; blade < 3; ++blade) {
    draw_bar_spoke(u8g2, cx, cy, r0, r1, width, static_cast<uint16_t>(angle + blade * 21845));
  }
}

void draw_left_reel(u8g2_t *u8g2, int ring, uint16_t angle) {
  constexpr int kThick = 3;
  draw_ring(u8g2, kLeftCx, kLeftCy, ring, kThick);
  draw_spokes(u8g2, kLeftCx, kLeftCy, ring, kThick, 3, angle);
  draw_hub(u8g2, kLeftCx, kLeftCy);
}

void draw_right_reel(u8g2_t *u8g2, int ring, uint16_t angle) {
  constexpr int kThick = 3;
  draw_ring(u8g2, kRightCx, kRightCy, ring, kThick);
  draw_spokes(u8g2, kRightCx, kRightCy, ring, kThick, 2, angle);
  draw_hub(u8g2, kRightCx, kRightCy);
}

void draw_segments(u8g2_t *u8g2, int tape) {
  constexpr int kCount = 4;
  constexpr int kWidth = 28;
  constexpr int kGap = 4;
  constexpr int kX0 = 2;
  constexpr int kY = 56;
  if (tape < 0) {
    tape = 0;
  }
  if (tape > 1000) {
    tape = 1000;
  }
  const int span = 1000 / kCount;
  int active = tape / span;
  if (active > kCount - 1) {
    active = kCount - 1;
  }
  const int into = tape - active * span;

  for (int i = 0; i < kCount; ++i) {
    const int x = kX0 + i * (kWidth + kGap);
    u8g2_DrawFrame(u8g2, static_cast<u8g2_uint_t>(x), kY, kWidth, 5);
    if (i > active) {
      continue;
    }
    int mark = kWidth - 8;
    if (i == active) {
      mark = 2 + into * (kWidth - 8) / span;
    } else if (i == 0) {
      mark = 8;
    } else if (i == 1) {
      mark = 16;
    }
    u8g2_DrawBox(u8g2, static_cast<u8g2_uint_t>(x + mark), kY + 1, 3, 3);
  }

  const int vx = kX0 + active * (kWidth + kGap) + kWidth / 2;
  // Two clear pixels between the chevron and the segment top at y=56.
  line(u8g2, vx - 3, 50, vx, 53);
  line(u8g2, vx, 53, vx + 3, 50);
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

void ui_render(u8g2_t *u8g2, const UiView *view) {
  u8g2_ClearBuffer(u8g2);
  u8g2_SetDrawColor(u8g2, 1);

  const bool pairing = !view->connected;
  const uint16_t spin = pairing ? static_cast<uint16_t>(view->nowMs * 14) : view->angle;
  const int leftAmount = pairing ? 1000 : (1000 - view->tape);
  const int rightAmount = pairing ? 0 : view->tape;
  const uint16_t rightSpin = static_cast<uint16_t>(spin + 14000);

  const int leftR = left_ring(leftAmount);
  const int rightR = right_ring(rightAmount);
  draw_volume_slider(u8g2, view->volumePos);
  draw_octagon(u8g2);
  u8g2_DrawCircle(u8g2, static_cast<u8g2_uint_t>(kRightCx), static_cast<u8g2_uint_t>(kRightCy), 18, U8G2_DRAW_ALL);
  // Rings first, so the tape can land on the pixels of this frame's thick stroke.
  draw_left_reel(u8g2, leftR, spin);
  draw_right_reel(u8g2, rightR, rightSpin);
  draw_tape_lines(u8g2, leftR, rightR);

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

  const bool blinkOn = ((view->nowMs / 320) % 2) == 0;
  if (view->connected || blinkOn) {
    draw_bt(u8g2, 119, 0);
  }
}
