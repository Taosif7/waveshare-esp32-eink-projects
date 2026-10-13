#pragma once

// Selected by the PlatformIO environment. Pins and the missing onboard parts
// (e-paper, ES8311, TCA9554) stay out of the shared game code.

#if defined(BOARD_C3_MINI)

// ESP32-C3-MINI-1 / Super Mini. ADC1 is GPIO0–GPIO4; GPIO2, GPIO8, and GPIO9
// are straps, and GPIO18/GPIO19 are USB. See component-specifications/esp32-c3-mini-1.md.
#define BOARD_HAS_EINK 0
#define BOARD_HAS_SOUND 0
#define BOARD_HAS_RAILS 0
#define BOARD_HAS_PWR 0
// Onboard BOOT is GPIO9, active-low, and it is a download strap. The external
// fire button on GPIO3 is enough if BOOT is not wired.
#define BOARD_HAS_BOOT_FIRE 1

constexpr int kOledSda = 6;
constexpr int kOledScl = 7;
constexpr int kJoyXPin = 0;
constexpr int kJoyYPin = 1;
constexpr int kJoySwPin = 10;
constexpr int kFirePin = 3;
constexpr int kBootPin = 9;

#elif defined(BOARD_C6_EPAPER)

#define BOARD_HAS_EINK 1
#define BOARD_HAS_SOUND 1
#define BOARD_HAS_RAILS 1
#define BOARD_HAS_PWR 1
#define BOARD_HAS_BOOT_FIRE 1

constexpr int kOledSda = 18;
constexpr int kOledScl = 8;
constexpr int kJoyXPin = 4;
constexpr int kJoyYPin = 3;
constexpr int kJoySwPin = 16;
constexpr int kFirePin = 17;
constexpr int kBootPin = 9;
constexpr int kPwrPin = 2;

#else
#error "Define BOARD_C3_MINI or BOARD_C6_EPAPER"
#endif
