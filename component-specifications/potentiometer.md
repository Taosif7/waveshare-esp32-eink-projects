# Potentiometer module (3-pin)

Breakout with pins labeled `VCC`, `GND`, and `SIG` / `IO`. Internally the track ends sit on VCC and GND and the wiper is SIG, so the ESP32 reads an analog voltage. Bring-up sketch: `touch-pot-test`.

## Reverse sense (lab-checked)

On this module the **value is opposite** to the usual “clockwise = higher” expectation:

| Knob turn that feels like… | Raw `analogRead` (0…4095) |
|----------------------------|---------------------------|
| More / fuller | Goes **down** toward 0 |
| Less / emptier | Goes **up** toward 4095 |

So a progress bar or game “amount” must invert the raw count, for example `level = 4095 - raw` or `map(raw, 0, 4095, full, empty)`. Do not treat rising ADC as “more” without that invert.

`touch-pot-test` inverts for both the OLED number and the bar (`value = 4095 - raw`). Serial prints `pot_raw=` (ADC) and `pot=` (inverted display value). Some flicker from ADC noise is normal.

`cocktail-lab` maps the same inverted percent to tap **flow rate** (low = closed, higher = faster pour).

## Wiring — ESP32-C3 Super Mini

| Module | Board pin | GPIO | Notes |
|--------|-----------|------|--------|
| VCC | **3.3** | — | Never the 5V pin |
| GND | **G** | — | |
| SIG / IO (wiper) | **3** | 3 | ADC1_CH3 |

GPIO3 is ADC1, so the reading stays valid with Wi-Fi on. Do not put this wiper on GPIO5 (ADC2; radio takes it). Leave GPIO2, GPIO8, GPIO9, GPIO18, and GPIO19 alone.

## Electrical behavior

Firmware in `touch-pot-test`:

- `pinMode(INPUT)` — no pull; the pot drives the pin
- `analogReadResolution(12)` so a sample is **0…4095**
- OLED number and bar both use `4095 - raw` so they track knob feel

Real modules often stop short of the rails (tens of counts above 0, a few hundred below 4095). A bar stuck at empty or full is almost always SIG on the wrong pin, VCC on 5V, or a missing ground.

## How the sketch uses it

`touch-pot-test` draws a progress bar under the touch indicator and prints `pot_raw=` / `pot=` every 200 ms. For any later app, invert for UI/game level; keep raw ADC only if you care about the electrical reading.

## Conflict

On the Waveshare C6 header, GPIO3 is also joystick VRy / external button / SD chip-select. On the C3 Super Mini used here, GPIO3 is only this potentiometer SIG.
