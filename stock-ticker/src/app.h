#pragma once

#include <stdint.h>

#include "fun_config.h"

static constexpr uint8_t kMaxPoints = 180;
static constexpr uint32_t kPollMs = 1000;
static constexpr uint8_t kGhostCleanEvery = 60;

enum class Screen : uint8_t {
  Wifi = 0,
  WifiFail = 1,
  Menu = 2,
  Chart = 3,
  Settings = 4,
  FunResult = 5,
};

enum class FunResultKind : uint8_t {
  None = 0,
  Bought = 1,
  SoldUp = 2,
  SoldDown = 3,
};

struct RangeOption {
  const char *label;
  uint32_t seconds;
  const char *yahoo_range;
  const char *yahoo_interval;
};

static const RangeOption kRanges[] = {
    {"3D", 3 * 86400, "5d", "15m"},
    {"1D", 86400, "5d", "5m"},
    {"12H", 12 * 3600, "5d", "5m"},
    {"6H", 6 * 3600, "1d", "1m"},
    {"3H", 3 * 3600, "1d", "1m"},
    {"2H", 2 * 3600, "1d", "1m"},
    {"1H", 1 * 3600, "1d", "1m"},
    {"30M", 30 * 60, "1d", "1m"},
    {"10M", 10 * 60, "1d", "1m"},
    {"5M", 5 * 60, "1d", "1m"},
    {"1M", 60, "1d", "1m"},
};

static constexpr uint8_t kRangeCount = static_cast<uint8_t>(sizeof(kRanges) / sizeof(kRanges[0]));

inline const RangeOption &range_at(uint8_t index) {
  return kRanges[index % kRangeCount];
}

struct Quote {
  bool valid;
  float price;
  float prev_close;
  float change_pct;
  uint16_t n_points;
  float points[kMaxPoints];
  uint32_t fetched_ms;
};

struct AppState {
  Screen screen;
  uint8_t menu_index;
  uint8_t stock_index;
  uint8_t range_index;
  uint8_t settings_index;
  Quote quote;
  bool dirty;
  bool full_refresh;
  bool base_refresh;
  bool fetching;
  uint8_t chart_refreshes;
  uint32_t last_poll_ms;
  char status[16];
  bool fun_holding;
  uint8_t fun_stock;
  float fun_cash;
  float fun_entry;
  float fun_qty;
  float fun_last_pnl;
  float fun_last_pct;
  FunResultKind fun_result;
};

void app_init(AppState *state);
void app_mark_dirty(AppState *state, bool full);
void app_set_screen(AppState *state, Screen screen);
void app_on_button_a(AppState *state);
void app_on_button_b(AppState *state);
void app_on_fun_buy(AppState *state);
void app_on_fun_sell(AppState *state);
void app_wifi_ok(AppState *state);
void app_wifi_fail(AppState *state);
void app_quote_loaded(AppState *state, bool ok);
bool app_should_poll(const AppState *state, uint32_t now_ms);
float app_fun_unrealized(const AppState *state);
