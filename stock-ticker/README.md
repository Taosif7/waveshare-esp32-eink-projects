# Stock Ticker

Live prices and a selectable-range chart on the **Waveshare ESP32-C6-ePaper-1.54** (200×200 B/W). Polls every **1 second** over Wi-Fi.

## Screens

1. **Menu** — pre-configured list: NASDAQ, Nifty 50, Sensex, Reliance, Tata Motors, TCS, Infosys, HDFC Bank, Apple, Tesla, plus **SETTINGS**.
2. **Chart** — last N minutes/hours of 1-minute closes, current price, and day change. Refreshes every 1 second. Optional paper-trade HUD when fun mode is on.
3. **Settings** — chart duration: 3D, 1D, 12h, 6h, 3h, 2h, 1h, 30m, 10m, 5m, 1m. Saved in flash.

## Controls

One rule on every screen: **red = next**, **green = confirm**. On the chart, a hold is a fake buy/sell when fun mode is enabled.

| Control | Pin | Menu | Chart | Settings |
|---------|-----|------|-------|----------|
| **Red** | GP4 | Next item | Tap: next chart. Hold: sell | Next duration |
| **Green** | GP3 | Open | Tap: menu. Hold: buy | Save |
| **BOOT** | onboard | Next item | Tap: next chart. Hold: sell | Next duration |
| **PWR** | onboard | Open | Tap: menu. Hold: buy | Save |

## Paper trading (fake)

This is a toy wallet for the chart screen — not a broker. Toggle and tune it in **one file**: `src/fun_config.h`.

```c
#define FUN_TRADING_ENABLED 1   // 0 = hide HUD, disable buy/sell
#define FUN_STARTING_CASH 10000.0f
#define FUN_ORDER_VALUE 2000.0f
#define FUN_PROFIT_MULT 1.0f
#define FUN_LUCKY_BIAS_PCT 0.0f
#define FUN_CURRENCY "$"
#define FUN_LONG_PRESS_MS 600
```

Hold **green** to buy ~`FUN_ORDER_VALUE` of the open chart; hold **red** to sell that position. Single taps keep their usual meaning and fire on release. The hold is timed from raw pin levels, since GP3/GP4 also carry EPD/SD SPI traffic that would otherwise cancel it mid-press. A splash shows **BOUGHT**, **PROFIT** (starburst + coins), or **LOSS**. Cash and the open position persist in flash. Rebuild after editing the config.

3-pin modules (`VCC` / `SIG` / `GND`), press = 3V3 on SIG. Leave the TF slot empty.

| Button | Header |
|--------|--------|
| Green SIG | **GP3** |
| Red SIG | **GP4** |
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
