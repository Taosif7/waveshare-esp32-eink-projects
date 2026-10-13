#include "sound.h"

#include "board_pins.h"

#include <math.h>

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t kEs8311 = 0x18;
constexpr int kSampleRate = 16000;
constexpr int kFrames = 256;
constexpr float kPi = 3.14159265f;
constexpr float kVolume = 0.22f;

constexpr int kMclkPin = 19;
constexpr int kBclkPin = 21;
constexpr int kWsPin = 22;
constexpr int kDoutPin = 23;
constexpr int kDinPin = 20;

i2s_chan_handle_t gTx = nullptr;
QueueHandle_t gQueue = nullptr;
bool gReady = false;
bool gEnabled = true;
volatile bool gCancel = false;
uint32_t gBusyUntil = 0;
int gRank = 0;

bool writeCodec(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kEs8311);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initCodec() {
  // Slave DAC, 16 kHz, MCLK = 256 * Fs. Same register order as adxl-horizon.
  const uint8_t seq[][2] = {
      {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24},
      {0x04, 0x10}, {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
      {0x00, 0x80}, {0x01, 0x3F}, {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58},
      {0x09, 0x0C}, {0x0A, 0x0C}, {0x02, 0x00}, {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x10},
      {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03}, {0x00, 0x80}, {0x01, 0x3F}, {0x09, 0x0C},
      {0x0A, 0x4C}, {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01},
      {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00}, {0x31, 0x00}, {0x32, 0xA8},
  };
  for (const auto &pair : seq) {
    if (!writeCodec(pair[0], pair[1])) {
      return false;
    }
  }
  return true;
}

bool initI2s() {
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = 4;
  channel.dma_frame_num = kFrames;
  if (i2s_new_channel(&channel, &gTx, nullptr) != ESP_OK) {
    return false;
  }
  i2s_std_config_t config = {};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = static_cast<gpio_num_t>(kMclkPin);
  config.gpio_cfg.bclk = static_cast<gpio_num_t>(kBclkPin);
  config.gpio_cfg.ws = static_cast<gpio_num_t>(kWsPin);
  config.gpio_cfg.dout = static_cast<gpio_num_t>(kDoutPin);
  config.gpio_cfg.din = static_cast<gpio_num_t>(kDinPin);
  if (i2s_channel_init_std_mode(gTx, &config) != ESP_OK) {
    return false;
  }
  return i2s_channel_enable(gTx) == ESP_OK;
}

void writeSamples(const int16_t *stereo, int frames) {
  size_t written = 0;
  i2s_channel_write(gTx, stereo, static_cast<size_t>(frames) * 2 * sizeof(int16_t), &written,
                    portMAX_DELAY);
}

void writeTone(float freq, int ms) {
  int total = kSampleRate * ms / 1000;
  if (total < 1) {
    total = 1;
  }
  int done = 0;
  float phase = 0.0f;
  int16_t buf[kFrames * 2];
  while (done < total && !gCancel) {
    const int n = (total - done) > kFrames ? kFrames : (total - done);
    for (int i = 0; i < n; ++i) {
      const int at = done + i;
      float env = 1.0f;
      const int attack = total / 12 + 1;
      const int release = total / 6 + 1;
      if (at < attack) {
        env = static_cast<float>(at) / static_cast<float>(attack);
      } else if (at + release >= total) {
        env = static_cast<float>(total - at) / static_cast<float>(release);
      }
      float sample = sinf(phase * 2.0f * kPi) * kVolume * env;
      phase += freq / static_cast<float>(kSampleRate);
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      const int16_t value = static_cast<int16_t>(sample * 32767.0f);
      buf[i * 2] = value;
      buf[i * 2 + 1] = value;
    }
    writeSamples(buf, n);
    done += n;
  }
}

void writeNoise(int ms) {
  int total = kSampleRate * ms / 1000;
  int done = 0;
  uint32_t noise = 0xA5F17u;
  int16_t buf[kFrames * 2];
  while (done < total && !gCancel) {
    const int n = (total - done) > kFrames ? kFrames : (total - done);
    for (int i = 0; i < n; ++i) {
      noise = noise * 1664525u + 1013904223u;
      float env = 1.0f - static_cast<float>(done + i) / static_cast<float>(total);
      float sample = static_cast<float>(static_cast<int16_t>(noise >> 16)) / 32768.0f;
      sample *= kVolume * 1.4f * env;
      const int16_t value = static_cast<int16_t>(sample * 32767.0f);
      buf[i * 2] = value;
      buf[i * 2 + 1] = value;
    }
    writeSamples(buf, n);
    done += n;
  }
}

void playSfx(Sfx sfx) {
  gCancel = false;
  switch (sfx) {
    case Sfx::Click:
      writeTone(880.0f, 30);
      break;
    case Sfx::Confirm:
      writeTone(660.0f, 50);
      writeTone(990.0f, 70);
      break;
    case Sfx::Shoot:
      writeTone(1480.0f, 18);
      break;
    case Sfx::Explode:
      writeNoise(70);
      writeTone(180.0f, 40);
      break;
    case Sfx::Hurt:
      writeTone(220.0f, 70);
      writeTone(110.0f, 90);
      break;
    case Sfx::Power:
      writeTone(740.0f, 40);
      writeTone(1180.0f, 60);
      break;
    case Sfx::Wave:
      writeTone(523.0f, 50);
      writeTone(659.0f, 50);
      writeTone(784.0f, 70);
      break;
    case Sfx::Boss:
      writeTone(196.0f, 80);
      writeTone(146.0f, 100);
      break;
    case Sfx::Over:
      writeTone(440.0f, 80);
      writeTone(330.0f, 80);
      writeTone(220.0f, 140);
      break;
    case Sfx::None:
      break;
  }
}

void audioTask(void *) {
  Sfx sfx = Sfx::None;
  while (true) {
    if (xQueueReceive(gQueue, &sfx, portMAX_DELAY) == pdTRUE) {
      playSfx(sfx);
    }
  }
}

int rankOf(Sfx sfx) {
  switch (sfx) {
    case Sfx::Shoot:
    case Sfx::Click:
      return 1;
    case Sfx::Confirm:
    case Sfx::Wave:
      return 2;
    case Sfx::Explode:
    case Sfx::Power:
    case Sfx::Boss:
      return 3;
    case Sfx::Hurt:
      return 4;
    case Sfx::Over:
      return 5;
    case Sfx::None:
      return 0;
  }
  return 0;
}

uint32_t durationMs(Sfx sfx) {
  switch (sfx) {
    case Sfx::Click:
      return 30;
    case Sfx::Confirm:
      return 120;
    case Sfx::Shoot:
      return 18;
    case Sfx::Explode:
      return 110;
    case Sfx::Hurt:
      return 160;
    case Sfx::Power:
      return 100;
    case Sfx::Wave:
      return 170;
    case Sfx::Boss:
      return 180;
    case Sfx::Over:
      return 300;
    case Sfx::None:
      return 0;
  }
  return 0;
}

}  // namespace

bool sound_init() {
#if !BOARD_HAS_SOUND
  Serial.println("No ES8311 on this board. Sound stays off.");
  return false;
#else
  if (!initI2s()) {
    Serial.println("I2S did not start; speaker stays quiet.");
    return false;
  }
  if (!initCodec()) {
    Serial.println("ES8311 did not ack; speaker stays quiet.");
    return false;
  }
  gQueue = xQueueCreate(1, sizeof(Sfx));
  if (gQueue == nullptr) {
    return false;
  }
  if (xTaskCreatePinnedToCore(audioTask, "sfx", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
    Serial.println("SFX task did not start.");
    return false;
  }
  gReady = true;
  Serial.println("Speaker tones on.");
  return true;
#endif
}

void sound_set_enabled(bool enabled) {
  gEnabled = enabled;
  if (!enabled) {
    gCancel = true;
    gBusyUntil = 0;
    gRank = 0;
  }
}

void sound_play(Sfx sfx) {
  if (!gReady || !gEnabled || sfx == Sfx::None || gQueue == nullptr) {
    return;
  }
  const int rank = rankOf(sfx);
  if (millis() < gBusyUntil && rank < gRank) {
    return;
  }
  gRank = rank;
  gBusyUntil = millis() + durationMs(sfx);
  gCancel = true;
  xQueueOverwrite(gQueue, &sfx);
}
