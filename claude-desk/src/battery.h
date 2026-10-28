#pragma once

#include <stdint.h>

void battery_init(void);
/** Percent 0–100, or -1 when the cell node reads open. */
int battery_percent(void);
