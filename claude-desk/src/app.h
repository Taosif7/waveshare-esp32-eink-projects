#pragma once

#include <stdint.h>

enum class BtnEvent : uint8_t {
  Next = 0,
  Confirm = 1,
  Dismiss = 2,
};

void app_init(void);
void app_on_line(const char *line);
void app_on_button(BtnEvent event);
void app_set_battery(int percent);
void app_tick(void);
