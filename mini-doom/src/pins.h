#pragma once

// ESP32-C3 Super Mini. ADC1 is GPIO0–GPIO4. GPIO2, GPIO8, and GPIO9 are
// straps. GPIO18 and GPIO19 are USB. GPIO4 is the touch pad so it does not
// share the joystick click on GPIO10. See component-specifications/esp32-c3-mini-1.md.

constexpr int kOledSda = 6;
constexpr int kOledScl = 7;
constexpr int kJoyXPin = 0;
constexpr int kJoyYPin = 1;
constexpr int kJoySwPin = 10;
constexpr int kTouchPin = 4;
constexpr int kFirePin = 3;
