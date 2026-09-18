#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "epaper_config.h"
#include "fun_config.h"
#include "port_display.h"
#include "stocks.h"

static const uint8_t FONT5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x14, 0x08, 0x3E, 0x08, 0x14},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x00, 0x41, 0x22, 0x14, 0x08},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
};

static int glyph_index(char c) {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  if (c < ' ' || c > 'Z') {
    return 0;
  }
  return c - ' ';
}

static void draw_char(int16_t x, int16_t y, char c, uint8_t scale, uint8_t color) {
  const uint8_t *g = FONT5X7[glyph_index(c)];
  for (int col = 0; col < 5; col++) {
    uint8_t bits = g[col];
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) {
        EPD_FillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

static int16_t text_width(const char *text, uint8_t scale) {
  return static_cast<int16_t>(strlen(text) * (5 + 1) * scale);
}

static void draw_text(int16_t x, int16_t y, const char *text, uint8_t scale, uint8_t color) {
  while (*text) {
    draw_char(x, y, *text, scale, color);
    x += (5 + 1) * scale;
    text++;
  }
}

static void draw_text_centered(int16_t y, const char *text, uint8_t scale) {
  int16_t x = static_cast<int16_t>((EPD_WIDTH - text_width(text, scale)) / 2);
  if (x < 0) {
    x = 0;
  }
  draw_text(x, y, text, scale, DRIVER_COLOR_BLACK);
}

static void draw_frame() {
  EPD_DrawRect(0, 0, EPD_WIDTH, EPD_HEIGHT, DRIVER_COLOR_BLACK);
  EPD_DrawRect(2, 2, EPD_WIDTH - 4, EPD_HEIGHT - 4, DRIVER_COLOR_BLACK);
}

static void draw_footer(const char *left, const char *right) {
  EPD_DrawHLine(8, 178, EPD_WIDTH - 16, DRIVER_COLOR_BLACK);
  draw_text(8, 184, left, 1, DRIVER_COLOR_BLACK);
  const int16_t w = text_width(right, 1);
  draw_text(static_cast<int16_t>(EPD_WIDTH - 8 - w), 184, right, 1, DRIVER_COLOR_BLACK);
}

static void format_money(char *buf, size_t n, float value) {
  const char *sign = value < 0 ? "-" : "";
  float mag = value < 0 ? -value : value;
  if (mag >= 10000.0f) {
    snprintf(buf, n, "%s%s%.0f", sign, FUN_CURRENCY, mag);
  } else if (mag >= 1000.0f) {
    snprintf(buf, n, "%s%s%.1f", sign, FUN_CURRENCY, mag);
  } else {
    snprintf(buf, n, "%s%s%.2f", sign, FUN_CURRENCY, mag);
  }
}

static void draw_up_arrow(int16_t cx, int16_t top) {
  const int16_t h = 46;
  const int16_t w = 36;
  EPD_DrawLine(cx, top, static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(top + h), DRIVER_COLOR_BLACK);
  EPD_DrawLine(cx, top, static_cast<int16_t>(cx + w / 2), static_cast<int16_t>(top + h), DRIVER_COLOR_BLACK);
  EPD_DrawLine(static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(top + h),
               static_cast<int16_t>(cx + w / 2), static_cast<int16_t>(top + h), DRIVER_COLOR_BLACK);
  for (int16_t y = 4; y < h - 4; y += 3) {
    const int16_t half = static_cast<int16_t>((w * (h - y)) / (2 * h));
    EPD_DrawHLine(static_cast<int16_t>(cx - half), static_cast<int16_t>(top + y), static_cast<int16_t>(half * 2),
                  DRIVER_COLOR_BLACK);
  }
}

static void draw_down_arrow(int16_t cx, int16_t top) {
  const int16_t h = 46;
  const int16_t w = 36;
  EPD_DrawLine(static_cast<int16_t>(cx - w / 2), top, static_cast<int16_t>(cx + w / 2), top, DRIVER_COLOR_BLACK);
  EPD_DrawLine(static_cast<int16_t>(cx - w / 2), top, cx, static_cast<int16_t>(top + h), DRIVER_COLOR_BLACK);
  EPD_DrawLine(static_cast<int16_t>(cx + w / 2), top, cx, static_cast<int16_t>(top + h), DRIVER_COLOR_BLACK);
  for (int16_t y = 4; y < h - 4; y += 3) {
    const int16_t half = static_cast<int16_t>((w * y) / (2 * h));
    EPD_DrawHLine(static_cast<int16_t>(cx - half), static_cast<int16_t>(top + y), static_cast<int16_t>(half * 2),
                  DRIVER_COLOR_BLACK);
  }
}

static void draw_starburst(int16_t cx, int16_t cy, int16_t r) {
  EPD_DrawLine(cx, static_cast<int16_t>(cy - r), cx, static_cast<int16_t>(cy + r), DRIVER_COLOR_BLACK);
  EPD_DrawLine(static_cast<int16_t>(cx - r), cy, static_cast<int16_t>(cx + r), cy, DRIVER_COLOR_BLACK);
  EPD_DrawLine(static_cast<int16_t>(cx - r * 3 / 4), static_cast<int16_t>(cy - r * 3 / 4),
               static_cast<int16_t>(cx + r * 3 / 4), static_cast<int16_t>(cy + r * 3 / 4), DRIVER_COLOR_BLACK);
  EPD_DrawLine(static_cast<int16_t>(cx - r * 3 / 4), static_cast<int16_t>(cy + r * 3 / 4),
               static_cast<int16_t>(cx + r * 3 / 4), static_cast<int16_t>(cy - r * 3 / 4), DRIVER_COLOR_BLACK);
  EPD_FillCircle(static_cast<int16_t>(cx - 22), static_cast<int16_t>(cy + 8), 8, DRIVER_COLOR_BLACK);
  EPD_FillCircle(cx, static_cast<int16_t>(cy + 14), 10, DRIVER_COLOR_BLACK);
  EPD_FillCircle(static_cast<int16_t>(cx + 24), static_cast<int16_t>(cy + 6), 7, DRIVER_COLOR_BLACK);
}

static void draw_fun_result(const AppState *state) {
  draw_frame();
  char line[24];
  if (state->fun_result == FunResultKind::Bought) {
    draw_up_arrow(100, 18);
    draw_text_centered(70, "BOUGHT", 2);
    draw_text_centered(92, stock_at(state->fun_stock).name, 1);
    format_money(line, sizeof(line), state->fun_entry);
    draw_text_centered(108, line, 2);
    snprintf(line, sizeof(line), "QTY %.2f", state->fun_qty);
    draw_text_centered(132, line, 1);
    format_money(line, sizeof(line), state->fun_cash);
    draw_text_centered(148, line, 1);
  } else if (state->fun_result == FunResultKind::SoldUp) {
    draw_starburst(100, 48, 36);
    draw_text_centered(92, "PROFIT", 2);
    format_money(line, sizeof(line), state->fun_last_pnl);
    draw_text_centered(114, line, 2);
    snprintf(line, sizeof(line), "%+.2f%%", state->fun_last_pct);
    draw_text_centered(138, line, 2);
  } else {
    draw_down_arrow(100, 18);
    draw_text_centered(70, "LOSS", 2);
    format_money(line, sizeof(line), state->fun_last_pnl);
    draw_text_centered(108, line, 2);
    snprintf(line, sizeof(line), "%+.2f%%", state->fun_last_pct);
    draw_text_centered(132, line, 2);
  }
  draw_text_centered(168, "PAPER TRADE", 1);
  draw_footer("ANY BTN", "BACK");
}

static void format_price(char *buf, size_t n, float price) {
  if (price >= 10000.0f) {
    snprintf(buf, n, "%.0f", price);
  } else if (price >= 1000.0f) {
    snprintf(buf, n, "%.1f", price);
  } else {
    snprintf(buf, n, "%.2f", price);
  }
}

static void draw_wifi() {
  draw_frame();
  draw_text_centered(40, "STOCKS", 3);
  draw_text_centered(88, "CONNECTING", 1);
  draw_text_centered(108, "WIFI...", 2);
  draw_text_centered(150, "WAIT FOR LINK", 1);
}

static void draw_wifi_fail() {
  draw_frame();
  draw_text_centered(36, "WIFI FAIL", 2);
  EPD_DrawHLine(24, 58, EPD_WIDTH - 48, DRIVER_COLOR_BLACK);
  draw_text_centered(76, "EDIT SSID IN", 1);
  draw_text_centered(92, "WIFI CONFIG H", 1);
  draw_text_centered(120, "THEN FLASH", 1);
  draw_text_centered(148, "ANY BTN: RETRY", 1);
}

static constexpr uint8_t kMenuVisible = 6;
static constexpr int16_t kMenuRowH = 22;
static constexpr int16_t kMenuTop = 36;

static void draw_menu(const AppState *state) {
  draw_frame();
  draw_text_centered(10, "STOCKS", 2);
  EPD_DrawHLine(16, 30, EPD_WIDTH - 32, DRIVER_COLOR_BLACK);

  uint8_t start = 0;
  if (state->menu_index >= kMenuVisible) {
    start = static_cast<uint8_t>(state->menu_index - (kMenuVisible - 1));
  }

  for (uint8_t i = 0; i < kMenuVisible; i++) {
    const uint8_t idx = static_cast<uint8_t>(start + i);
    if (idx >= kMenuCount) {
      break;
    }
    const int16_t y = static_cast<int16_t>(kMenuTop + i * kMenuRowH);
    const bool selected = idx == state->menu_index;
    const char *label = idx == kMenuSettingsIndex ? "SETTINGS" : stock_at(idx).name;
    if (selected) {
      EPD_FillRect(8, y - 3, EPD_WIDTH - 16, kMenuRowH - 2, DRIVER_COLOR_BLACK);
      draw_text(16, y, label, 2, DRIVER_COLOR_WHITE);
    } else {
      draw_text(16, y, label, 2, DRIVER_COLOR_BLACK);
    }
  }

  draw_footer("RED NEXT", "GRN OPEN");
}

static constexpr uint8_t kSettingsVisible = 6;
static constexpr int16_t kSettingsRowH = 22;
static constexpr int16_t kSettingsTop = 36;

static void draw_settings(const AppState *state) {
  draw_frame();
  draw_text_centered(10, "RANGE", 2);
  EPD_DrawHLine(16, 30, EPD_WIDTH - 32, DRIVER_COLOR_BLACK);

  uint8_t start = 0;
  if (state->settings_index >= kSettingsVisible) {
    start = static_cast<uint8_t>(state->settings_index - (kSettingsVisible - 1));
  }

  for (uint8_t i = 0; i < kSettingsVisible; i++) {
    const uint8_t idx = static_cast<uint8_t>(start + i);
    if (idx >= kRangeCount) {
      break;
    }
    const int16_t y = static_cast<int16_t>(kSettingsTop + i * kSettingsRowH);
    const bool selected = idx == state->settings_index;
    const bool saved = idx == state->range_index;
    char line[12];
    snprintf(line, sizeof(line), "%s%s", range_at(idx).label, saved ? "  ON" : "");
    if (selected) {
      EPD_FillRect(8, y - 3, EPD_WIDTH - 16, kSettingsRowH - 2, DRIVER_COLOR_BLACK);
      draw_text(16, y, line, 2, DRIVER_COLOR_WHITE);
    } else {
      draw_text(16, y, line, 2, DRIVER_COLOR_BLACK);
    }
  }

  draw_footer("RED NEXT", "GRN SAVE");
}

static void draw_polyline(const float *pts, uint16_t n, int16_t x, int16_t y, int16_t w, int16_t h, bool mark,
                          float mark_v) {
  if (n < 2 || w < 2 || h < 2) {
    return;
  }
  float lo = pts[0];
  float hi = pts[0];
  for (uint16_t i = 1; i < n; i++) {
    if (pts[i] < lo) {
      lo = pts[i];
    }
    if (pts[i] > hi) {
      hi = pts[i];
    }
  }
  float span = hi - lo;
  if (span < 0.01f) {
    span = 0.01f;
    lo -= 0.005f;
  }
  const float pad = span * 0.08f;
  lo -= pad;
  hi += pad;
  span = hi - lo;

  auto map_x = [&](uint16_t i) -> int16_t {
    return static_cast<int16_t>(x + (static_cast<int32_t>(i) * (w - 1)) / (n - 1));
  };
  auto map_y = [&](float v) -> int16_t {
    const float t = (v - lo) / span;
    return static_cast<int16_t>(y + h - 1 - t * (h - 1));
  };

  int16_t px = map_x(0);
  int16_t py = map_y(pts[0]);
  for (uint16_t i = 1; i < n; i++) {
    const int16_t nx = map_x(i);
    const int16_t ny = map_y(pts[i]);
    EPD_DrawLine(px, py, nx, ny, DRIVER_COLOR_BLACK);
    EPD_DrawLine(px, static_cast<int16_t>(py + 1), nx, static_cast<int16_t>(ny + 1), DRIVER_COLOR_BLACK);
    px = nx;
    py = ny;
  }

  if (!mark) {
    return;
  }
  int16_t my = map_y(mark_v);
  if (my < y) {
    my = y;
  } else if (my > static_cast<int16_t>(y + h - 1)) {
    my = static_cast<int16_t>(y + h - 1);
  }
  for (int16_t dx = 0; dx < w; dx += 6) {
    const int16_t seg = dx + 3 <= w ? 3 : static_cast<int16_t>(w - dx);
    EPD_DrawHLine(static_cast<int16_t>(x + dx), my, seg, DRIVER_COLOR_BLACK);
  }
}

static void draw_chart(const AppState *state) {
  draw_frame();
  const Stock &stock = stock_at(state->stock_index);
  draw_text(8, 8, stock.name, 1, DRIVER_COLOR_BLACK);

  const char *badge = state->status;
  const int16_t bw = text_width(badge, 1);
  draw_text(static_cast<int16_t>(EPD_WIDTH - 8 - bw), 8, badge, 1, DRIVER_COLOR_BLACK);
  EPD_DrawHLine(8, 20, EPD_WIDTH - 16, DRIVER_COLOR_BLACK);

  char line[24];
  if (state->quote.valid) {
    format_price(line, sizeof(line), state->quote.price);
    draw_text(8, 26, line, 2, DRIVER_COLOR_BLACK);

    const float chg = state->quote.price - state->quote.prev_close;
    snprintf(line, sizeof(line), "%+.2f  %+.2f%%", chg, state->quote.change_pct);
    draw_text(8, 46, line, 1, DRIVER_COLOR_BLACK);
  } else {
    draw_text(8, 30, "LOADING...", 2, DRIVER_COLOR_BLACK);
  }

  const int16_t plot_x = 8;
  const int16_t plot_y = 60;
  const int16_t plot_w = EPD_WIDTH - 16;
  const int16_t plot_h = FUN_TRADING_ENABLED ? 88 : 108;
  EPD_DrawRect(plot_x, plot_y, plot_w, plot_h, DRIVER_COLOR_BLACK);

  if (state->quote.valid && state->quote.n_points >= 2) {
    const bool mark = FUN_TRADING_ENABLED && state->fun_holding && state->fun_stock == state->stock_index;
    draw_polyline(state->quote.points, state->quote.n_points, static_cast<int16_t>(plot_x + 2),
                  static_cast<int16_t>(plot_y + 2), static_cast<int16_t>(plot_w - 4),
                  static_cast<int16_t>(plot_h - 4), mark, state->fun_entry);
  } else if (state->quote.valid) {
    draw_text(plot_x + 36, plot_y + 48, "NO BARS", 1, DRIVER_COLOR_BLACK);
  }

  const int16_t label_y = FUN_TRADING_ENABLED ? 164 : 172;
  char start_label[8];
  snprintf(start_label, sizeof(start_label), "-%s", range_at(state->range_index).label);
  draw_text(8, label_y, start_label, 1, DRIVER_COLOR_BLACK);
  const int16_t nw = text_width("NOW", 1);
  draw_text(static_cast<int16_t>(EPD_WIDTH - 8 - nw), label_y, "NOW", 1, DRIVER_COLOR_BLACK);

  if (FUN_TRADING_ENABLED) {
    char cash[16];
    format_money(cash, sizeof(cash), state->fun_cash);
    char hud[24];
    snprintf(hud, sizeof(hud), "CASH %s", cash);
    draw_text(8, 152, hud, 1, DRIVER_COLOR_BLACK);
    if (state->fun_holding) {
      if (state->fun_stock == state->stock_index && state->quote.valid) {
        const float u = app_fun_unrealized(state);
        char pnl[16];
        format_money(pnl, sizeof(pnl), u);
        char right[20];
        if (u >= 0.0f) {
          snprintf(right, sizeof(right), "+%s", pnl);
        } else {
          snprintf(right, sizeof(right), "%s", pnl);
        }
        const int16_t rw = text_width(right, 1);
        draw_text(static_cast<int16_t>(EPD_WIDTH - 8 - rw), 152, right, 1, DRIVER_COLOR_BLACK);
      } else {
        const char *held = stock_at(state->fun_stock).name;
        const int16_t rw = text_width(held, 1);
        draw_text(static_cast<int16_t>(EPD_WIDTH - 8 - rw), 152, held, 1, DRIVER_COLOR_BLACK);
      }
    } else {
      const int16_t rw = text_width("FLAT", 1);
      draw_text(static_cast<int16_t>(EPD_WIDTH - 8 - rw), 152, "FLAT", 1, DRIVER_COLOR_BLACK);
    }
    draw_footer("HOLD RED SELL", "HOLD GRN BUY");
  } else {
    draw_footer("RED NEXT", "GRN MENU");
  }
}

void ui_compose(const AppState *state) {
  EPD_Clear();
  switch (state->screen) {
    case Screen::Wifi:
      draw_wifi();
      break;
    case Screen::WifiFail:
      draw_wifi_fail();
      break;
    case Screen::Menu:
      draw_menu(state);
      break;
    case Screen::Chart:
      draw_chart(state);
      break;
    case Screen::Settings:
      draw_settings(state);
      break;
    case Screen::FunResult:
      draw_fun_result(state);
      break;
  }
}

static void present_blocking(const AppState *state) {
  if (state->full_refresh) {
    EPD_FlushFull();
  } else if (state->base_refresh) {
    EPD_FlushBaseThenPartial();
  } else {
    EPD_FlushPartial();
  }
}

void ui_present(const AppState *state) {
  present_blocking(state);
}

void ui_present_begin(const AppState *state) {
  if (state->full_refresh || state->base_refresh) {
    present_blocking(state);
    return;
  }
  EPD_FlushPartialBegin();
}
