#pragma once

#include <stddef.h>
#include <stdint.h>

struct Stock {
  const char *name;
  const char *symbol;
};

// Yahoo Finance symbols (NSE suffix .NS, indices use ^).
static const Stock kStocks[] = {
    {"NASDAQ", "^IXIC"},
    {"NIFTY 50", "^NSEI"},
    {"SENSEX", "^BSESN"},
    {"RELIANCE", "RELIANCE.NS"},
    {"TATA", "TATAMOTORS.NS"},
    {"TCS", "TCS.NS"},
    {"INFOSYS", "INFY.NS"},
    {"HDFC", "HDFCBANK.NS"},
    {"APPLE", "AAPL"},
    {"TESLA", "TSLA"},
};

static constexpr uint8_t kStockCount = static_cast<uint8_t>(sizeof(kStocks) / sizeof(kStocks[0]));
static constexpr uint8_t kMenuSettingsIndex = kStockCount;
static constexpr uint8_t kMenuCount = kStockCount + 1;

inline const Stock &stock_at(uint8_t index) {
  return kStocks[index % kStockCount];
}
