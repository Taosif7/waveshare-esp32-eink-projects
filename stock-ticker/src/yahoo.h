#pragma once

#include "app.h"
#include "stocks.h"

bool yahoo_fetch_chart(const Stock &stock, Quote *out, const RangeOption &range);
