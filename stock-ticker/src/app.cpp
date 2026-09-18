#include "app.h"

#include <Preferences.h>
#include <string.h>

#include "fun_config.h"
#include "stocks.h"

static Preferences prefs;

static void save_fun(const AppState *state) {
  if (!FUN_TRADING_ENABLED) {
    return;
  }
  prefs.begin("fun", false);
  prefs.putFloat("cash", state->fun_cash);
  prefs.putBool("hold", state->fun_holding);
  prefs.putUChar("stk", state->fun_stock);
  prefs.putFloat("entry", state->fun_entry);
  prefs.putFloat("qty", state->fun_qty);
  prefs.end();
}

static void load_fun(AppState *state) {
  state->fun_holding = false;
  state->fun_stock = 0;
  state->fun_cash = FUN_STARTING_CASH;
  state->fun_entry = 0;
  state->fun_qty = 0;
  state->fun_last_pnl = 0;
  state->fun_last_pct = 0;
  state->fun_result = FunResultKind::None;
  if (!FUN_TRADING_ENABLED) {
    return;
  }
  prefs.begin("fun", true);
  state->fun_cash = prefs.getFloat("cash", FUN_STARTING_CASH);
  state->fun_holding = prefs.getBool("hold", false);
  state->fun_stock = prefs.getUChar("stk", 0);
  state->fun_entry = prefs.getFloat("entry", 0);
  state->fun_qty = prefs.getFloat("qty", 0);
  prefs.end();
  if (state->fun_stock >= kStockCount) {
    state->fun_holding = false;
    state->fun_stock = 0;
  }
}

static void load_range(AppState *state) {
  prefs.begin("ticker", true);
  uint32_t sec = prefs.getUInt("range_s", 0);
  if (sec == 0) {
    const uint8_t old = prefs.getUChar("range", 255);
    static const uint32_t kLegacySec[] = {3 * 3600, 2 * 3600, 3600, 30 * 60, 10 * 60, 5 * 60, 60};
    if (old < sizeof(kLegacySec) / sizeof(kLegacySec[0])) {
      sec = kLegacySec[old];
    } else {
      sec = 3 * 3600;
    }
  }
  prefs.end();

  state->range_index = 4;  // 3H in the new list
  for (uint8_t i = 0; i < kRangeCount; i++) {
    if (kRanges[i].seconds == sec) {
      state->range_index = i;
      break;
    }
  }
  state->settings_index = state->range_index;
}

static void save_range(AppState *state) {
  state->range_index = state->settings_index;
  prefs.begin("ticker", false);
  prefs.putUInt("range_s", range_at(state->range_index).seconds);
  prefs.end();
}

void app_mark_dirty(AppState *state, bool full) {
  state->dirty = true;
  if (full) {
    state->full_refresh = true;
  }
}

void app_set_screen(AppState *state, Screen screen) {
  state->screen = screen;
  state->fetching = false;
  state->chart_refreshes = 0;
  if (screen == Screen::Chart) {
    state->full_refresh = false;
    state->base_refresh = true;
  } else {
    state->full_refresh = true;
    state->base_refresh = false;
  }
  state->dirty = true;
}

void app_init(AppState *state) {
  memset(state, 0, sizeof(*state));
  load_range(state);
  load_fun(state);
  app_set_screen(state, Screen::Wifi);
  strncpy(state->status, "WIFI", sizeof(state->status) - 1);
}

void app_on_button_a(AppState *state) {
  switch (state->screen) {
    case Screen::Wifi:
      break;
    case Screen::WifiFail:
      app_set_screen(state, Screen::Wifi);
      strncpy(state->status, "WIFI", sizeof(state->status) - 1);
      break;
    case Screen::Menu:
      state->menu_index = static_cast<uint8_t>((state->menu_index + 1) % kMenuCount);
      app_mark_dirty(state, false);
      break;
    case Screen::Chart:
      state->stock_index = static_cast<uint8_t>((state->stock_index + 1) % kStockCount);
      state->menu_index = state->stock_index;
      state->quote.valid = false;
      state->last_poll_ms = 0;
      state->chart_refreshes = 0;
      state->base_refresh = true;
      state->dirty = true;
      strncpy(state->status, "LOAD", sizeof(state->status) - 1);
      break;
    case Screen::Settings:
      state->settings_index = static_cast<uint8_t>((state->settings_index + 1) % kRangeCount);
      app_mark_dirty(state, false);
      break;
    case Screen::FunResult:
      state->fun_result = FunResultKind::None;
      app_set_screen(state, Screen::Chart);
      break;
  }
}

void app_on_button_b(AppState *state) {
  switch (state->screen) {
    case Screen::Wifi:
      break;
    case Screen::WifiFail:
      app_set_screen(state, Screen::Wifi);
      strncpy(state->status, "WIFI", sizeof(state->status) - 1);
      break;
    case Screen::Menu:
      if (state->menu_index == kMenuSettingsIndex) {
        state->settings_index = state->range_index;
        app_set_screen(state, Screen::Settings);
        break;
      }
      state->stock_index = state->menu_index;
      state->quote.valid = false;
      state->last_poll_ms = 0;
      strncpy(state->status, "LOAD", sizeof(state->status) - 1);
      app_set_screen(state, Screen::Chart);
      break;
    case Screen::Chart:
      strncpy(state->status, "MENU", sizeof(state->status) - 1);
      app_set_screen(state, Screen::Menu);
      break;
    case Screen::Settings:
      save_range(state);
      strncpy(state->status, "MENU", sizeof(state->status) - 1);
      app_set_screen(state, Screen::Menu);
      break;
    case Screen::FunResult:
      state->fun_result = FunResultKind::None;
      app_set_screen(state, Screen::Chart);
      break;
  }
}

void app_on_fun_buy(AppState *state) {
  if (!FUN_TRADING_ENABLED || state->screen != Screen::Chart) {
    return;
  }
  if (!state->quote.valid || state->quote.price <= 0.0f) {
    strncpy(state->status, "NO PX", sizeof(state->status) - 1);
    app_mark_dirty(state, false);
    return;
  }
  if (state->fun_holding) {
    strncpy(state->status, "HELD", sizeof(state->status) - 1);
    app_mark_dirty(state, false);
    return;
  }
  float spend = FUN_ORDER_VALUE;
  if (spend > state->fun_cash) {
    spend = state->fun_cash;
  }
  if (spend < 1.0f) {
    strncpy(state->status, "NO CASH", sizeof(state->status) - 1);
    app_mark_dirty(state, false);
    return;
  }
  const float qty = spend / state->quote.price;
  state->fun_holding = true;
  state->fun_stock = state->stock_index;
  state->fun_entry = state->quote.price;
  state->fun_qty = qty;
  state->fun_cash -= spend;
  state->fun_last_pnl = 0;
  state->fun_last_pct = 0;
  state->fun_result = FunResultKind::Bought;
  save_fun(state);
  strncpy(state->status, "BOUGHT", sizeof(state->status) - 1);
  app_set_screen(state, Screen::FunResult);
}

void app_on_fun_sell(AppState *state) {
  if (!FUN_TRADING_ENABLED || state->screen != Screen::Chart) {
    return;
  }
  if (!state->fun_holding) {
    strncpy(state->status, "NO POS", sizeof(state->status) - 1);
    app_mark_dirty(state, false);
    return;
  }
  if (state->fun_stock != state->stock_index) {
    strncpy(state->status, "OPEN POS", sizeof(state->status) - 1);
    app_mark_dirty(state, false);
    return;
  }
  if (!state->quote.valid || state->quote.price <= 0.0f) {
    strncpy(state->status, "NO PX", sizeof(state->status) - 1);
    app_mark_dirty(state, false);
    return;
  }
  const float raw_pnl = (state->quote.price - state->fun_entry) * state->fun_qty;
  float pct = 0;
  if (state->fun_entry > 0.0f) {
    pct = (state->quote.price - state->fun_entry) * 100.0f / state->fun_entry;
  }
  pct += FUN_LUCKY_BIAS_PCT;
  const float pnl = raw_pnl * FUN_PROFIT_MULT +
                    (state->fun_entry * state->fun_qty) * (FUN_LUCKY_BIAS_PCT / 100.0f);
  state->fun_cash += state->fun_entry * state->fun_qty + pnl;
  state->fun_last_pnl = pnl;
  state->fun_last_pct = pct * FUN_PROFIT_MULT;
  state->fun_holding = false;
  state->fun_qty = 0;
  state->fun_entry = 0;
  state->fun_result = pnl >= 0.0f ? FunResultKind::SoldUp : FunResultKind::SoldDown;
  save_fun(state);
  strncpy(state->status, pnl >= 0.0f ? "PROFIT" : "LOSS", sizeof(state->status) - 1);
  app_set_screen(state, Screen::FunResult);
}

float app_fun_unrealized(const AppState *state) {
  if (!state->fun_holding || !state->quote.valid || state->fun_stock != state->stock_index) {
    return 0.0f;
  }
  return (state->quote.price - state->fun_entry) * state->fun_qty * FUN_PROFIT_MULT;
}

void app_wifi_ok(AppState *state) {
  strncpy(state->status, "OK", sizeof(state->status) - 1);
  app_set_screen(state, Screen::Menu);
}

void app_wifi_fail(AppState *state) {
  strncpy(state->status, "FAIL", sizeof(state->status) - 1);
  app_set_screen(state, Screen::WifiFail);
}

void app_quote_loaded(AppState *state, bool ok) {
  state->fetching = false;
  if (!ok && !state->quote.valid) {
    strncpy(state->status, "ERR", sizeof(state->status) - 1);
  } else if (ok) {
    strncpy(state->status, "LIVE", sizeof(state->status) - 1);
  } else {
    strncpy(state->status, "STALE", sizeof(state->status) - 1);
  }
  if (state->screen != Screen::Chart) {
    return;
  }
  state->chart_refreshes++;
  if (state->chart_refreshes == 1 || (state->chart_refreshes % kGhostCleanEvery) == 0) {
    state->base_refresh = true;
  }
  state->dirty = true;
}

bool app_should_poll(const AppState *state, uint32_t now_ms) {
  if (state->screen != Screen::Chart || state->fetching) {
    return false;
  }
  if (state->last_poll_ms == 0) {
    return true;
  }
  return (now_ms - state->last_poll_ms) >= kPollMs;
}
