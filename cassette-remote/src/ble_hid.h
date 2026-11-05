#pragma once

#include <stdint.h>

// Consumer-control bits. Matches the report map in ble_hid.cpp.
constexpr uint8_t kHidPrev = 1u << 0;
constexpr uint8_t kHidRewind = 1u << 1;
constexpr uint8_t kHidPlay = 1u << 2;
constexpr uint8_t kHidForward = 1u << 3;
constexpr uint8_t kHidNext = 1u << 4;
constexpr uint8_t kHidVolumeUp = 1u << 5;
constexpr uint8_t kHidVolumeDown = 1u << 6;

void ble_hid_begin();
void ble_hid_pulse(uint8_t bit, uint32_t nowMs, uint32_t durationMs);
void ble_hid_service(uint32_t nowMs);
bool ble_hid_connected();
void ble_hid_forget();
