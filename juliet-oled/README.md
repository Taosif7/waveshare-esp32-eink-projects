# Juliet on OLED

Loops the 2414-frame clip from `juliet-frames.h` on a **128×64 SH1106**. Each frame is 128×64, 1-bit. The header's delay is 0 ms, so the loop runs as fast as the I2C bus can push a full frame, then starts over.

`frames.bin` is that clip packed for U8g2 (MSB-left Adafruit bytes, bits reversed). The ESP32-C3 Super Mini has 4 MB of flash and no e-paper, so the onboard warning paint is skipped.

## Wiring

ESP32-C3 Super Mini. Power the OLED from **3.3**, not 5V.

| OLED | Board pin | GPIO |
|------|-----------|------|
| VCC | 3.3 | — |
| GND | G | — |
| SDA | 6 | 6 |
| SCL | 7 | 7 |

Address `0x3C`, or `0x3D` if that is what answers. The boot log prints an I2C scan when neither answers.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/juliet-oled
pio run -e esp32-c3-mini -t upload
pio device monitor
```
