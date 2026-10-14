#pragma once

// Selected by the PlatformIO environment. Pins and the missing onboard parts
// (e-paper, TCA9554) stay out of the shared player code.

#if defined(BOARD_C3_MINI)

// ESP32-C3-MINI-1 / Super Mini. GPIO9 is the BOOT strap. GPIO18/GPIO19 are USB.
// See component-specifications/esp32-c3-mini-1.md.
#define BOARD_HAS_EINK 0
#define BOARD_HAS_RAILS 0

constexpr int kOledSda = 6;
constexpr int kOledScl = 7;
constexpr int kBootPin = 9;

#elif defined(BOARD_C6_EPAPER)

#define BOARD_HAS_EINK 1
#define BOARD_HAS_RAILS 1

constexpr int kOledSda = 18;
constexpr int kOledScl = 8;
constexpr int kBootPin = 9;

#else
#error "Define BOARD_C3_MINI or BOARD_C6_EPAPER"
#endif
