#pragma once

// ESP32-C3 Super Mini perfboard. GPIO9 is the boot strap (SCL). GPIO8 is also
// the onboard LED, active low, so it flickers while the panel is written.
constexpr int kOledSda = 8;
constexpr int kOledScl = 9;
constexpr int kPotPin = 3;

// Left to right across the row of touch modules.
constexpr int kPadPins[] = {4, 10, 20, 21, 1};
constexpr int kPadCount = 5;

constexpr int kPadPrev = 0;
constexpr int kPadRewind = 1;
constexpr int kPadPlay = 2;
constexpr int kPadForward = 3;
constexpr int kPadNext = 4;
