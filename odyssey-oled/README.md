# Odyssey on OLED

Loops the 121-frame OLED Animation Maker clip on a **128×64 SH1106** I2C OLED. Frames advance every 67 ms (about 15 fps), then start over.

## Wiring

On the ESP32-C6-ePaper-1.54 header. Power the module from **3V3**, not VSYS.

| OLED | Header |
|------|--------|
| VCC | 3V3 |
| GND | GND |
| SDA | **SDA** (GPIO18) |
| SCL | **SCL** (GPIO8) |

Address `0x3C`, or `0x3D` if that is what answers. The boot log prints an I2C scan when neither answers.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/odyssey-oled
pio run -t upload
pio device monitor
```
