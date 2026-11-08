#include "leds.h"

#include <FastLED.h>

#include "config.h"

static CRGB s_leds[LED_COUNT];

void leds_begin() {
  FastLED.addLeds<WS2812, PIN_LED_DATA, GRB>(s_leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.clear(true);
}

void leds_clear() {
  FastLED.clear(true);
}

static void paint_train(uint16_t head_index) {
  if (head_index >= LED_COUNT) {
    head_index = LED_COUNT - 1;
  }
  static const uint8_t kValue[3] = {255, 160, 70};
  static const uint8_t kSat[3] = {180, 220, 240};
  for (uint8_t t = 0; t < 3; t++) {
    if (head_index < t) {
      break;
    }
    const uint16_t i = head_index - t;
    const CRGB pix = CHSV(28, kSat[t], kValue[t]);
    s_leds[i].r = qadd8(s_leds[i].r, pix.r);
    s_leds[i].g = qadd8(s_leds[i].g, pix.g);
    s_leds[i].b = qadd8(s_leds[i].b, pix.b);
  }
}

void leds_show_train(uint16_t head_index) {
  fill_solid(s_leds, LED_COUNT, CRGB::Black);
  paint_train(head_index);
  FastLED.show();
}

void leds_show_trains(const uint16_t *heads, uint8_t count) {
  fill_solid(s_leds, LED_COUNT, CRGB::Black);
  for (uint8_t i = 0; i < count; i++) {
    paint_train(heads[i]);
  }
  FastLED.show();
}

void leds_flash_finale() {
  fill_solid(s_leds, LED_COUNT, CRGB::Black);
  s_leds[LED_COUNT - 1] = CRGB(255, 220, 80);
  if (LED_COUNT >= 2) {
    s_leds[LED_COUNT - 2] = CRGB(255, 140, 40);
  }
  if (LED_COUNT >= 3) {
    s_leds[LED_COUNT - 3] = CRGB(180, 60, 10);
  }
  FastLED.show();
}
