#include "battery.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>

// Waveshare 01_ADC_Test: ADC1 channel 0 (GPIO0 on the ESP32-C6), 12-bit,
// 12 dB attenuation, pin millivolts times 2. Empty 3.0 V, full 4.12 V.

static adc_oneshot_unit_handle_t adc_handle = nullptr;
static adc_cali_handle_t cali_handle = nullptr;
static bool adc_ready = false;

void battery_init(void) {
  adc_cali_curve_fitting_config_t cali = {};
  cali.unit_id = ADC_UNIT_1;
  cali.atten = ADC_ATTEN_DB_12;
  cali.bitwidth = ADC_BITWIDTH_12;
  if (adc_cali_create_scheme_curve_fitting(&cali, &cali_handle) != ESP_OK) {
    cali_handle = nullptr;
  }

  adc_oneshot_unit_init_cfg_t unit = {};
  unit.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&unit, &adc_handle) != ESP_OK) {
    return;
  }
  adc_oneshot_chan_cfg_t chan = {};
  chan.bitwidth = ADC_BITWIDTH_12;
  chan.atten = ADC_ATTEN_DB_12;
  if (adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan) != ESP_OK) {
    return;
  }
  adc_ready = true;
}

int battery_percent(void) {
  if (!adc_ready) {
    return -1;
  }
  int raw_sum = 0;
  int got = 0;
  for (int i = 0; i < 4; i++) {
    int raw = 0;
    if (adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw) == ESP_OK) {
      raw_sum += raw;
      got++;
    }
  }
  if (got == 0) {
    return -1;
  }
  const int raw = raw_sum / got;
  int pin_mv = 0;
  if (cali_handle == nullptr || adc_cali_raw_to_voltage(cali_handle, raw, &pin_mv) != ESP_OK) {
    pin_mv = raw * 3300 / 4095;
  }
  const float volts = 0.001f * static_cast<float>(pin_mv) * 2.0f;
  if (volts < 1.0f) {
    return -1;
  }
  constexpr float kEmpty = 3.0f;
  constexpr float kFull = 4.12f;
  if (volts <= kEmpty) {
    return 0;
  }
  if (volts >= kFull) {
    return 100;
  }
  return static_cast<int>((volts - kEmpty) / (kFull - kEmpty) * 100.0f);
}
