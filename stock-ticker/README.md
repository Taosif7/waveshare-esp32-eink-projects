# Stock Ticker

Live prices and a selectable-range chart on the **Waveshare ESP32-C6-ePaper-1.54** (200×200 B/W). Polls every **1 second** over Wi-Fi.

## Screens

1. **Menu** — pre-configured list: NASDAQ, Nifty 50, Sensex, Reliance, Tata Motors, TCS, Infosys, HDFC Bank, Apple, Tesla, plus **SETTINGS**.
2. **Chart** — last N minutes/hours of 1-minute closes, current price, and day change. Refreshes every 1 second.
3. **Settings** — chart duration: 3D, 1D, 12h, 6h, 3h, 2h, 1h, 30m, 10m, 5m, 1m. Saved in flash.

## Controls

| Input | Menu | Chart | Settings |
|-------|------|-------|----------|
| **BOOT** or external **A** (GP3) | Next item | Next chart | Next duration |
| **PWR** or external **B** (GP4) | Open chart / settings | Back to menu | Save and back |

External buttons are the same active-HIGH 3-pin modules (`VCC` / `SIG` / `GND`) used on this board. Leave the TF slot empty.

| Module | Header |
|--------|--------|
| A SIG | **GP3** |
| B SIG | **GP4** |
| VCC | **3V3** (not VSYS) |
| GND | GND |

## Wi-Fi

Edit `src/wifi_config.h` before flashing (copy from `src/wifi_config.example.h`):

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
```

The ESP32-C6 has Wi-Fi 6 onboard; no extra radio is required.

## Price API

Default source is **Yahoo Finance** `v8/finance/chart` (no API key):

`https://query1.finance.yahoo.com/v8/finance/chart/{SYMBOL}?range=1d&interval=1m`

It returns 1-minute bars plus `regularMarketPrice`. The firmware keeps closes for the selected duration (default 3 hours) and overwrites the latest point with the live price every 1 second.

This endpoint is unofficial (Yahoo can change it). It is the best zero-signup option for a small e-paper ticker.

### Paid / trial alternatives (if Yahoo rate-limits you)

| API | Free tier | Notes |
|-----|-----------|-------|
| [Finnhub](https://finnhub.io) | 60 REST calls/min after signup | Official trial can keep up with 1 s polling. Quote + candle endpoints. Needs a token. |
| [Twelve Data](https://twelvedata.com) | 8 calls/min, 800/day | Too slow for 1 s polls on the free plan. |
| [Alpha Vantage](https://www.alphavantage.co) | 25 requests/day | Not usable for live refresh. |

Finnhub is the drop-in upgrade if you want a documented SLA: put a token in `wifi_config.h` and swap `yahoo.cpp`. Keep Yahoo unless you hit HTTP 429.

Indian names use NSE Yahoo symbols (`RELIANCE.NS`, `^NSEI`). US names use the plain ticker (`AAPL`, `^IXIC`).

## E-paper notes

A 1-second cadence uses **partial refresh** (~0.3 s). The HTTP fetch often takes longer than 1 s, so the panel updates as soon as each request finishes. Every 60 updates the firmware rewrites both RAM planes so ghost trails do not pile up. Menu and Wi-Fi screens use a full refresh.

## Build & flash

```bash
# 1. Set SSID/password in src/wifi_config.h
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/stock-ticker
pio run -t upload
pio device monitor
```

If serial permission fails: `sg dialout -c 'pio run -t upload'`.

See the repo [technical specification](../ESP32-C6-ePaper-1.54-Technical-Specification.md) for pinout and USB caveats. Do **not** wire buttons to GP12/GP13.
