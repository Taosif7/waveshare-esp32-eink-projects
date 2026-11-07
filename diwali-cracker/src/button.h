#pragma once

#include <stdint.h>

void button_begin();
// Returns true once per clean press (active low, debounced).
bool button_was_pressed();
