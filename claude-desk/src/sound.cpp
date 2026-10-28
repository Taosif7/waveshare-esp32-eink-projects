#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "port_codec.h"

namespace {
constexpr int kSampleRate = 16000;
constexpr int kChannels = 2;
constexpr float kPi = 3.14159265f;
constexpr float kVolume = 0.28f;

QueueHandle_t tone_queue = nullptr;
bool sound_ready = false;
volatile bool tone_cancel = false;

void write_tone(float freq_hz, uint16_t ms) {
  if (tone_cancel) {
    return;
  }
  const size_t frames = static_cast<size_t>(kSampleRate) * ms / 1000;
  const size_t samples = frames * kChannels;
  int16_t *buf = static_cast<int16_t *>(malloc(samples * sizeof(int16_t)));
  if (buf == nullptr) {
    return;
  }
  for (size_t i = 0; i < frames; i++) {
    const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
    float env = 1.0f;
    const size_t attack = frames / 20 + 1;
    const size_t release = frames / 8 + 1;
    if (i < attack) {
      env = static_cast<float>(i) / static_cast<float>(attack);
    } else if (i + release >= frames) {
      env = static_cast<float>(frames - i) / static_cast<float>(release);
    }
    const float s = sinf(2.0f * kPi * freq_hz * t) * kVolume * env;
    const int16_t v = static_cast<int16_t>(s * 32767.0f);
    buf[i * 2] = v;
    buf[i * 2 + 1] = v;
  }
  if (!tone_cancel) {
    Codec_PlaybackData(reinterpret_cast<uint8_t *>(buf), samples * sizeof(int16_t));
  }
  free(buf);
}

void write_rest(uint16_t ms) {
  if (tone_cancel) {
    return;
  }
  const size_t bytes = static_cast<size_t>(kSampleRate) * kChannels * sizeof(int16_t) * ms / 1000;
  uint8_t *silence = static_cast<uint8_t *>(calloc(1, bytes));
  if (silence == nullptr) {
    return;
  }
  Codec_PlaybackData(silence, bytes);
  free(silence);
}

void play_tone(Tone tone) {
  tone_cancel = false;
  switch (tone) {
    case Tone::Question:
      write_tone(880.0f, 90);
      write_rest(40);
      write_tone(1175.0f, 120);
      break;
    case Tone::Error:
      write_tone(330.0f, 80);
      write_tone(196.0f, 160);
      break;
    case Tone::Done:
      write_tone(660.0f, 100);
      break;
    case Tone::None:
      break;
  }
}

void audio_task(void *arg) {
  (void)arg;
  Tone tone = Tone::None;
  while (true) {
    if (xQueueReceive(tone_queue, &tone, portMAX_DELAY) == pdTRUE) {
      play_tone(tone);
    }
  }
}
}  // namespace

bool sound_init(void) {
  Codec_StartInit();
  tone_queue = xQueueCreate(1, sizeof(Tone));
  if (tone_queue == nullptr) {
    return false;
  }
  const BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "sfx", 8192, nullptr, 3, nullptr, 0);
  sound_ready = (ok == pdPASS);
  return sound_ready;
}

void sound_stop(void) {
  tone_cancel = true;
}

void sound_play(Tone tone) {
  if (!sound_ready || tone == Tone::None || tone_queue == nullptr) {
    return;
  }
  tone_cancel = true;
  xQueueOverwrite(tone_queue, &tone);
}
