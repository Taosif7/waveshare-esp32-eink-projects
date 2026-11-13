#pragma once

// ESP32-C3 Super Mini. ADC1 is GPIO0–GPIO4. GPIO2 is unused; GPIO8 is the
// onboard LED and OLED SDA; GPIO9 is OLED SCL and a boot strap. GPIO18/19 are
// USB. GPIO4 is the active-high fire button; GPIO10 is joystick click.

constexpr int kOledSda = 6;
constexpr int kOledScl = 7;
constexpr int kJoyXPin = 0;
constexpr int kJoyYPin = 1;
constexpr int kJoySwPin = 10;
constexpr int kFirePin = 4;
