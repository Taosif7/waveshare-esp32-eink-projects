#include "engine_audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

namespace {

constexpr uint8_t kEs8311 = 0x18;
constexpr int kSampleRate = 16000;
constexpr int kFrames = 256;

constexpr int kMclkPin = 19;
constexpr int kBclkPin = 21;
constexpr int kWsPin = 22;
constexpr int kDoutPin = 23;
constexpr int kDinPin = 20;

i2s_chan_handle_t gTx = nullptr;
volatile float gWarp = 1.0f;

bool writeCodec(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kEs8311);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initCodec() {
  // Slave DAC, 16 kHz, MCLK = 256 * Fs = 4.096 MHz. Register order follows
  // the Espressif ES8311 driver: open, then sample format, then start.
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
  channel.dma_desc_num = 6;
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

void engineTask(void *) {
  int16_t samples[kFrames * 2];
  float phase = 0.0f;
  float harmonic = 0.0f;
  uint32_t noise = 0xA5A5u;
  while (true) {
    float warp = gWarp;
    if (warp < 1.0f) {
      warp = 1.0f;
    }
    float blend = (warp - 1.0f) / 17.0f;
    if (blend > 1.0f) {
      blend = 1.0f;
    }
    const float freq = 68.0f + blend * 320.0f;
    const float tone = 0.10f + blend * 0.06f;
    const float hiss = 0.015f + blend * 0.11f;
    for (int i = 0; i < kFrames; ++i) {
      phase += freq / static_cast<float>(kSampleRate);
      harmonic += (freq * 2.02f) / static_cast<float>(kSampleRate);
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      if (harmonic >= 1.0f) {
        harmonic -= 1.0f;
      }
      noise = noise * 1664525u + 1013904223u;
      const float hash = static_cast<float>(static_cast<int16_t>(noise >> 16)) / 32768.0f;
      float sample = sinf(phase * 6.2831853f) * tone;
      sample += sinf(harmonic * 6.2831853f) * tone * 0.28f;
      sample += hash * hiss;
      if (sample > 0.9f) {
        sample = 0.9f;
      } else if (sample < -0.9f) {
        sample = -0.9f;
      }
      const int16_t value = static_cast<int16_t>(sample * 32767.0f);
      samples[i * 2] = value;
      samples[i * 2 + 1] = value;
    }
    size_t written = 0;
    i2s_channel_write(gTx, samples, sizeof(samples), &written, portMAX_DELAY);
  }
}

}  // namespace

void engineAudioBegin() {
  if (!initI2s()) {
    Serial.println("I2S did not start; speaker stays quiet.");
    return;
  }
  if (!initCodec()) {
    Serial.println("ES8311 did not ack; speaker stays quiet.");
    return;
  }
  if (xTaskCreatePinnedToCore(engineTask, "engine", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
    Serial.println("Engine audio task did not start.");
    return;
  }
  Serial.println("Engine audio on.");
}

void engineAudioSetWarp(float warp) {
  gWarp = warp;
}
