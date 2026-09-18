#pragma once

// Single file for the chart-screen paper-trading toy. Not real orders.
// Set FUN_TRADING_ENABLED to 0 to hide the HUD and ignore buy/sell.
// Rebuild after changing anything here.

#ifndef FUN_TRADING_ENABLED
#define FUN_TRADING_ENABLED 1
#endif

// Starting fake wallet (used when nothing is saved in flash yet).
#ifndef FUN_STARTING_CASH
#define FUN_STARTING_CASH 10000.0f
#endif

// Fake notional spent on each BUY (in FUN_CURRENCY units).
#ifndef FUN_ORDER_VALUE
#define FUN_ORDER_VALUE 2000.0f
#endif

// Multiply realized P/L for extra drama. 1.0 = honest paper math.
#ifndef FUN_PROFIT_MULT
#define FUN_PROFIT_MULT 1.0f
#endif

// Extra percentage points added to sell P/L (can be 0 or negative).
#ifndef FUN_LUCKY_BIAS_PCT
#define FUN_LUCKY_BIAS_PCT 0.0f
#endif

#ifndef FUN_CURRENCY
#define FUN_CURRENCY "$"
#endif

// Gap allowed between the two taps of a buy/sell double click. Raising this
// also delays single taps, since a tap is only final once the window closes.
#ifndef FUN_DOUBLE_CLICK_MS
#define FUN_DOUBLE_CLICK_MS 450
#endif
