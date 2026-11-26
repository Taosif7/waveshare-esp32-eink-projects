#pragma once

#include <stdint.h>

// Physical alignment. Edit these five, then rebuild both environments.
// Gaps are bezel-only: the displays sit edge to edge, and only the inactive
// rim between the active areas is counted. A positive Y offset moves that
// panel's y = 0 line upward on the glass.

#define GAP_EINK_OLED_MM 9.0f
#define GAP_OLED_TFT_MM 5.0f
#define EINK_Y_OFFSET_MM 0.0f
#define OLED_Y_OFFSET_MM 0.0f
#define TFT_Y_OFFSET_MM 0.0f

// 0..3. Use 2 if the GMT130 is mounted upside down.
#define TFT_ROTATION 0

namespace pond {

constexpr float kEinkWidthMm = 27.6f;
constexpr float kEinkHeightMm = 27.6f;
constexpr float kEinkPxPerMm = 7.25f;
constexpr int kEinkPx = 200;

constexpr float kOledWidthMm = 29.4f;
constexpr float kOledHeightMm = 14.7f;
constexpr float kOledPxPerMm = 4.35f;
constexpr int kOledPxW = 128;
constexpr int kOledPxH = 64;

constexpr float kTftWidthMm = 23.4f;
constexpr float kTftHeightMm = 23.4f;
constexpr float kTftPxPerMm = 10.26f;
constexpr int kTftPx = 240;
constexpr int kTftScale = 2;
constexpr int kTftLogical = kTftPx / kTftScale;

constexpr float kKoiLengthMm = 17.5f;
constexpr float kWaveTailAmpMm = 1.55f;
constexpr float kWaveCycles = 0.95f;
constexpr float kWaveExp = 1.32f;
constexpr float kWeaveMm = 0.35f;
constexpr float kBobMm = 1.50f;

constexpr float kEinkSpeedMmS = 19.0f;
constexpr float kOledSpeedMmS = 27.0f;
constexpr float kTftSpeedMmS = 34.0f;

constexpr float kPi = 3.14159265f;

constexpr int kWifiChannel = 1;
constexpr int kRadioRetries = 3;
constexpr int kRadioRetryMs = 10;

constexpr int kGhostEvery = 4;
constexpr int kCalBootMs = 5000;
constexpr int kPassPauseMs = 700;
constexpr int kBootLongMs = 800;
constexpr int kTftBacklightDuty = 80;
constexpr int kOledFrameMs = 33;
constexpr int kTftFrameMs = 16;
constexpr uint32_t kTftSpiHz = 40000000;

constexpr float kShadowDxMm = 0.85f;
constexpr float kShadowDyMm = -0.70f;
constexpr float kOutlineMm = 0.22f;

// Little-endian bytes on the wire are 'K' 'O' 'I' '1'.
constexpr uint32_t kMagic = 0x31494F4Bu;
constexpr uint8_t kProtoVersion = 1;

constexpr uint16_t rgb565(int r, int g, int b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace pond
