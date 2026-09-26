# Dino on OLED

The Chrome T-Rex runner from [`dino-game`](../dino-game/), drawn on a **128×64 SH1106** I2C OLED. The panel refreshes every frame, so the run uses the browser clock instead of the e-paper slowdown. Sellers often label this module SSD1306; an SSD1306 driver paints the wrong columns and the picture turns to noise.

Same rules: speeding scroll, grouped cacti, pterodactyls, score flash every 100, night invert every 700, and the high score kept in NVS (same `dino` namespace as the e-paper build).

Sprites are scaled to fit the 64px height. The trex, cacti, horizon, cloud, digits, and restart icon are still the Chromium 1x assets.

## Wiring

On the ESP32-C6-ePaper-1.54 header. Power the module from **3V3**, not VSYS. Leave the TF slot empty.

| OLED | Header |
|------|--------|
| VCC | 3V3 |
| GND | GND |
| SDA | **SDA** (GPIO18) |
| SCL | **SCL** (GPIO8) |

Address `0x3C`, or `0x3D` if that is what answers. The boot log prints an I2C scan when neither answers.

The button is the same one as the e-paper game.

| Module | Header |
|--------|--------|
| VCC | 3V3 |
| SIG | **GP3** (active-high) |
| GND | GND |

**BOOT** does the same thing. Do not use GP12 or GP13.

| Gesture | Action |
|---------|--------|
| Tap | Jump. On the idle screen, start. After a crash, restart. |
| Hold about 1 s | Pause, or resume |
| Hold about 2.5 s | Back to the idle screen. The saved high score stays. |

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/dino-oled
pio run -t upload
pio device monitor
```
