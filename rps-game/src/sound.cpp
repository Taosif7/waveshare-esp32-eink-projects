#include "sound.h"

#include <math.h>
#include <string.h>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "port_codec.h"

// Embedded PCM (16 kHz, mono, int16) from Downloads MP3s via platformio embed_files
extern const uint8_t sfx_win_pcm_start[] asm("_binary_assets_sfx_win_pcm_start");
extern const uint8_t sfx_win_pcm_end[] asm("_binary_assets_sfx_win_pcm_end");
extern const uint8_t sfx_lose_pcm_start[] asm("_binary_assets_sfx_lose_pcm_start");
extern const uint8_t sfx_lose_pcm_end[] asm("_binary_assets_sfx_lose_pcm_end");
extern const uint8_t sfx_draw_pcm_start[] asm("_binary_assets_sfx_draw_pcm_start");
extern const uint8_t sfx_draw_pcm_end[] asm("_binary_assets_sfx_draw_pcm_end");

namespace {
constexpr int kSampleRate = 16000;
constexpr int kChannels = 2;
constexpr float kPi = 3.14159265f;
constexpr float kVolume = 0.28f;
constexpr size_t kChunkFrames = 256;

QueueHandle_t sfx_queue = nullptr;
bool sound_ready = false;
volatile bool sfx_cancel = false;

void write_tone(float freq_hz, uint16_t ms) {
  if (sfx_cancel) {
    return;
  }
  const size_t frames = (size_t)kSampleRate * ms / 1000;
  const size_t samples = frames * kChannels;
  int16_t *buf = (int16_t *)malloc(samples * sizeof(int16_t));
  if (buf == nullptr) {
    return;
  }
  for (size_t i = 0; i < frames; i++) {
    float t = (float)i / (float)kSampleRate;
    float env = 1.0f;
    const size_t attack = frames / 20 + 1;
    const size_t release = frames / 8 + 1;
    if (i < attack) {
      env = (float)i / (float)attack;
    } else if (i + release >= frames) {
      env = (float)(frames - i) / (float)release;
    }
    float s = sinf(2.0f * kPi * freq_hz * t) * kVolume * env;
    int16_t v = (int16_t)(s * 32767.0f);
    buf[i * 2] = v;
    buf[i * 2 + 1] = v;
  }
  if (!sfx_cancel) {
    Codec_PlaybackData((uint8_t *)buf, samples * sizeof(int16_t));
  }
  free(buf);
}

void write_rest(uint16_t ms) {
  if (sfx_cancel) {
    return;
  }
  const size_t bytes = (size_t)kSampleRate * kChannels * sizeof(int16_t) * ms / 1000;
  uint8_t *silence = (uint8_t *)calloc(1, bytes);
  if (silence == nullptr) {
    return;
  }
  Codec_PlaybackData(silence, bytes);
  free(silence);
}

void play_pcm_mono(const int16_t *mono, size_t frames) {
  int16_t stereo[kChunkFrames * 2];
  size_t i = 0;
  while (i < frames) {
    if (sfx_cancel) {
      return;
    }
    const size_t n = (frames - i) > kChunkFrames ? kChunkFrames : (frames - i);
    for (size_t j = 0; j < n; j++) {
      const int16_t v = mono[i + j];
      stereo[j * 2] = v;
      stereo[j * 2 + 1] = v;
    }
    Codec_PlaybackData(reinterpret_cast<uint8_t *>(stereo), n * kChannels * sizeof(int16_t));
    i += n;
  }
}

void play_embedded(const uint8_t *start, const uint8_t *end) {
  const size_t bytes = static_cast<size_t>(end - start);
  if (bytes < 2 || (bytes & 1) != 0) {
    return;
  }
  play_pcm_mono(reinterpret_cast<const int16_t *>(start), bytes / 2);
}

void play_sfx(Sfx sfx) {
  sfx_cancel = false;
  switch (sfx) {
    case Sfx::Click:
      write_tone(880.0f, 40);
      break;
    case Sfx::Confirm:
      write_tone(660.0f, 60);
      write_rest(20);
      write_tone(990.0f, 80);
      break;
    case Sfx::Win:
      play_embedded(sfx_win_pcm_start, sfx_win_pcm_end);
      break;
    case Sfx::Lose:
      play_embedded(sfx_lose_pcm_start, sfx_lose_pcm_end);
      break;
    case Sfx::Draw:
      play_embedded(sfx_draw_pcm_start, sfx_draw_pcm_end);
      break;
    case Sfx::Title:
      write_tone(392.0f, 80);
      write_tone(523.25f, 80);
      write_tone(659.25f, 120);
      break;
    case Sfx::Reset:
      write_tone(300.0f, 60);
      write_tone(220.0f, 100);
      break;
    case Sfx::None:
      break;
  }
}

void audio_task(void *arg) {
  (void)arg;
  Sfx sfx = Sfx::None;
  while (true) {
    if (xQueueReceive(sfx_queue, &sfx, portMAX_DELAY) == pdTRUE) {
      play_sfx(sfx);
    }
  }
}
}  // namespace

bool sound_init(void) {
  Codec_StartInit();
  sfx_queue = xQueueCreate(1, sizeof(Sfx));
  if (sfx_queue == nullptr) {
    return false;
  }
  BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "sfx", 8192, nullptr, 3, nullptr, 0);
  sound_ready = (ok == pdPASS);
  return sound_ready;
}

void sound_stop(void) {
  sfx_cancel = true;
}

void sound_play(Sfx sfx) {
  if (!sound_ready || sfx == Sfx::None || sfx_queue == nullptr) {
    return;
  }
  // Abort whatever is streaming so the new clip can start promptly
  sfx_cancel = true;
  xQueueOverwrite(sfx_queue, &sfx);
}
