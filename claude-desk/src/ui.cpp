#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "epaper_config.h"
#include "port_display.h"

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
    const uint8_t bits = g[col];
    for (int row = 0; row < 7; row++) {
      if ((bits & (1 << row)) != 0) {
        EPD_FillRect(static_cast<uint16_t>(x + col * scale), static_cast<uint16_t>(y + row * scale), scale, scale,
                     color);
      }
    }
  }
}

static int text_cols(uint8_t scale) {
  const int cw = 6 * scale;
  return (EPD_WIDTH - 8) / cw;
}

static void draw_text(int16_t x, int16_t y, const char *text, int max_chars, uint8_t scale, uint8_t color) {
  if (text == nullptr) {
    return;
  }
  const int cw = 6 * scale;
  for (int i = 0; text[i] != '\0' && i < max_chars; i++) {
    draw_char(static_cast<int16_t>(x + i * cw), y, text[i], scale, color);
  }
}

static void draw_status(const UiModel *model) {
  const char *link = model->link ? "LINK" : "DOWN";
  draw_text(4, 2, link, 4, 1, DRIVER_COLOR_BLACK);
  if (model->more > 0) {
    char more[8];
    snprintf(more, sizeof(more), "+%d", model->more > 9 ? 9 : model->more);
    const int cols = text_cols(1);
    const int x = 4 + ((cols - static_cast<int>(strlen(more))) / 2) * 6;
    draw_text(static_cast<int16_t>(x), 2, more, 4, 1, DRIVER_COLOR_BLACK);
  }
  char bat[8];
  if (model->battery < 0) {
    snprintf(bat, sizeof(bat), "--");
  } else {
    snprintf(bat, sizeof(bat), "%d%%", model->battery);
  }
  const int bw = static_cast<int>(strlen(bat)) * 6;
  draw_text(static_cast<int16_t>(EPD_WIDTH - 4 - bw), 2, bat, 4, 1, DRIVER_COLOR_BLACK);
  EPD_DrawHLine(0, 12, EPD_WIDTH, DRIVER_COLOR_BLACK);
}

static void draw_list(const UiModel *model) {
  constexpr int kRow = 18;
  constexpr int kTop = 16;
  const int visible = (EPD_HEIGHT - kTop) / kRow;
  if (model->session_count <= 0) {
    draw_text(4, 40, "No sessions", 16, 2, DRIVER_COLOR_BLACK);
    return;
  }
  int start = model->highlight - visible + 1;
  if (start < 0) {
    start = 0;
  }
  if (start > model->session_count - visible && model->session_count > visible) {
    start = model->session_count - visible;
  }
  if (model->session_count <= visible) {
    start = 0;
  }
  for (int i = 0; i < visible && start + i < model->session_count; i++) {
    const int index = start + i;
    const int y = kTop + i * kRow;
    const bool on = index == model->highlight;
    const uint8_t ink = on ? DRIVER_COLOR_WHITE : DRIVER_COLOR_BLACK;
    if (on) {
      EPD_FillRect(0, static_cast<uint16_t>(y - 1), EPD_WIDTH, kRow, DRIVER_COLOR_BLACK);
    }
    char row[20];
    snprintf(row, sizeof(row), "%c %.14s", model->marks[index], model->names[index]);
    draw_text(4, static_cast<int16_t>(y + 1), row, 16, 2, ink);
  }
}

static int wrap_prompt(const char *text, char lines[][34], int max_lines) {
  int count = 0;
  int col = 0;
  if (text == nullptr) {
    return 0;
  }
  lines[0][0] = '\0';
  for (const char *p = text; *p != '\0' && count < max_lines; p++) {
    if (col >= 32) {
      lines[count][col] = '\0';
      count++;
      col = 0;
      if (count >= max_lines) {
        break;
      }
      lines[count][0] = '\0';
    }
    lines[count][col++] = *p;
    lines[count][col] = '\0';
  }
  if (count < max_lines && col > 0) {
    count++;
  }
  return count;
}

static void draw_question(const UiModel *model) {
  int y = 16;
  if (model->header != nullptr && model->header[0] != '\0') {
    draw_text(4, static_cast<int16_t>(y), model->header, 16, 2, DRIVER_COLOR_BLACK);
    y += 18;
  }
  char lines[4][34];
  const int nlines = wrap_prompt(model->prompt, lines, 4);
  for (int i = 0; i < nlines; i++) {
    draw_text(4, static_cast<int16_t>(y), lines[i], 32, 1, DRIVER_COLOR_BLACK);
    y += 9;
  }
  y += 4;
  constexpr int kRow = 18;
  int visible = (EPD_HEIGHT - y) / kRow;
  if (visible < 1) {
    visible = 1;
    y = EPD_HEIGHT - kRow;
  }
  int start = 0;
  if (model->option_count > visible) {
    start = model->highlight - visible + 1;
    if (start < 0) {
      start = 0;
    }
    if (start > model->option_count - visible) {
      start = model->option_count - visible;
    }
  }
  for (int i = 0; i < visible && start + i < model->option_count; i++) {
    const int index = start + i;
    const int row_y = y + i * kRow;
    const bool on = index == model->highlight;
    const uint8_t ink = on ? DRIVER_COLOR_WHITE : DRIVER_COLOR_BLACK;
    if (on) {
      EPD_FillRect(0, static_cast<uint16_t>(row_y), EPD_WIDTH, kRow - 1, DRIVER_COLOR_BLACK);
    }
    draw_text(4, static_cast<int16_t>(row_y + 1), model->options[index], 16, 2, ink);
  }
}

void ui_draw(const UiModel *model, bool full) {
  EPD_Clear();
  draw_status(model);
  if (model->question) {
    draw_question(model);
  } else {
    draw_list(model);
  }
  if (full) {
    EPD_FlushFull();
  } else {
    EPD_FlushPartial();
  }
}
