#include "icons.h"

#include <cstddef>

#include "port_display.h"

static void ink(uint8_t color = DRIVER_COLOR_BLACK) {
  (void)color;
}

static void draw_rock(int16_t cx, int16_t cy, int16_t s) {
  // Faceted boulder silhouette
  const int16_t pts[][2] = {
      {-18, 6},  {-12, -10}, {-2, -16}, {10, -12}, {18, -2},
      {16, 10},  {6, 16},    {-8, 14},  {-18, 6},
  };
  for (size_t i = 0; i + 1 < sizeof(pts) / sizeof(pts[0]); i++) {
    EPD_DrawLine(cx + pts[i][0] * s / 10, cy + pts[i][1] * s / 10,
                 cx + pts[i + 1][0] * s / 10, cy + pts[i + 1][1] * s / 10,
                 DRIVER_COLOR_BLACK);
  }
  // Inner facets
  EPD_DrawLine(cx - 8 * s / 10, cy - 2 * s / 10, cx + 4 * s / 10, cy - 8 * s / 10, DRIVER_COLOR_BLACK);
  EPD_DrawLine(cx - 8 * s / 10, cy - 2 * s / 10, cx + 2 * s / 10, cy + 8 * s / 10, DRIVER_COLOR_BLACK);
  EPD_DrawLine(cx + 4 * s / 10, cy - 8 * s / 10, cx + 12 * s / 10, cy + 2 * s / 10, DRIVER_COLOR_BLACK);
  // Speckle fill
  for (int16_t y = -12; y <= 12; y += 3) {
    for (int16_t x = -12; x <= 12; x += 3) {
      if (x * x + y * y < 140) {
        if (((x + y) & 1) == 0) {
          EPD_DrawColorPixel(cx + x * s / 10, cy + y * s / 10, DRIVER_COLOR_BLACK);
        }
      }
    }
  }
}

static void draw_paper(int16_t cx, int16_t cy, int16_t s) {
  int16_t w = 28 * s / 10;
  int16_t h = 34 * s / 10;
  int16_t x = cx - w / 2;
  int16_t y = cy - h / 2;
  // Page with folded corner
  EPD_DrawRect(x, y, w, h, DRIVER_COLOR_BLACK);
  EPD_DrawRect(x + 1, y + 1, w - 2, h - 2, DRIVER_COLOR_BLACK);
  int16_t fold = 8 * s / 10;
  EPD_DrawLine(x + w - fold, y, x + w - 1, y + fold, DRIVER_COLOR_BLACK);
  EPD_DrawLine(x + w - fold, y, x + w - fold, y + fold, DRIVER_COLOR_BLACK);
  EPD_DrawLine(x + w - fold, y + fold, x + w - 1, y + fold, DRIVER_COLOR_BLACK);
  // Ruled lines
  for (int i = 0; i < 4; i++) {
    int16_t ly = y + 10 * s / 10 + i * 5 * s / 10;
    EPD_DrawHLine(x + 4 * s / 10, ly, w - 10 * s / 10, DRIVER_COLOR_BLACK);
  }
}

static void draw_scissors(int16_t cx, int16_t cy, int16_t s) {
  // Twin rings + crossing blades
  int16_t r = 7 * s / 10;
  EPD_DrawCircle(cx - 10 * s / 10, cy + 10 * s / 10, r, DRIVER_COLOR_BLACK);
  EPD_DrawCircle(cx - 10 * s / 10, cy + 10 * s / 10, r - 2, DRIVER_COLOR_BLACK);
  EPD_DrawCircle(cx + 10 * s / 10, cy + 10 * s / 10, r, DRIVER_COLOR_BLACK);
  EPD_DrawCircle(cx + 10 * s / 10, cy + 10 * s / 10, r - 2, DRIVER_COLOR_BLACK);

  EPD_DrawLine(cx - 6 * s / 10, cy + 4 * s / 10, cx + 14 * s / 10, cy - 16 * s / 10, DRIVER_COLOR_BLACK);
  EPD_DrawLine(cx - 5 * s / 10, cy + 5 * s / 10, cx + 15 * s / 10, cy - 15 * s / 10, DRIVER_COLOR_BLACK);
  EPD_DrawLine(cx + 6 * s / 10, cy + 4 * s / 10, cx - 14 * s / 10, cy - 16 * s / 10, DRIVER_COLOR_BLACK);
  EPD_DrawLine(cx + 5 * s / 10, cy + 5 * s / 10, cx - 15 * s / 10, cy - 15 * s / 10, DRIVER_COLOR_BLACK);

  // Pivot
  EPD_FillCircle(cx, cy + 2 * s / 10, 2 * s / 10, DRIVER_COLOR_BLACK);
}

void icons_draw_move(Move move, int16_t cx, int16_t cy, int16_t scale) {
  ink();
  switch (move) {
    case Move::Rock:
      draw_rock(cx, cy, scale);
      break;
    case Move::Paper:
      draw_paper(cx, cy, scale);
      break;
    case Move::Scissors:
      draw_scissors(cx, cy, scale);
      break;
  }
}
