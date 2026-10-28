#pragma once

#include <stddef.h>

void ble_init(void);
bool ble_connected(void);
void ble_send_line(const char *line);
/** Copy one complete host line, without the newline. Returns false if none is waiting. */
bool ble_take_line(char *dst, size_t len);
