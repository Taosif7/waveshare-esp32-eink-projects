#include "app.h"

#include <Preferences.h>
#include <string.h>

#include "stocks.h"

static Preferences prefs;

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
  }
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
