#pragma once

#include <stdint.h>

// ESP32-C3 Super Mini — both pins are non-strap, ADC1-safe with Wi-Fi on.
static constexpr uint8_t PIN_LED_DATA = 4;   // WS2812 DIN (via ~330 Ω)
static constexpr uint8_t PIN_BUTTON = 3;     // Momentary to GND, internal pull-up

static constexpr uint16_t LED_COUNT = 57;
// ~32 keeps USB 5V (VBUS) under ~0.4 A with warm chase colors + Wi-Fi.
static constexpr uint8_t LED_BRIGHTNESS = 32;

static constexpr uint16_t CHASE_MS_PER_LED = 28;
static constexpr uint32_t COOLDOWN_MS = 300;
static constexpr uint16_t BUTTON_DEBOUNCE_MS = 40;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
