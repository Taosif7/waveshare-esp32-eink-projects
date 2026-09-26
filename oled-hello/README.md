# OLED Hello

Animated **HELLO WORLD** on a 4-pin I2C OLED wired to the ESP32-C6-ePaper-1.54 header.

The onboard e-paper uses SPI. This sketch talks only to the external display on the shared I2C bus (SDA = GPIO18, SCL = GPIO8).

## Wiring

Power the module from **3V3**. VSYS is about 5 V and will damage a 3.3 V OLED.

| OLED | Header |
|------|--------|
| VCC | **3V3** |
| GND | **GND** |
| SDA | **SDA** (GPIO18) |
| SCL | **SCL** (GPIO8) |

This module is an SH1106 128×64 at `0x3C` (it also accepts `0x3D`). An SH1106’s memory is 132 columns wide, so the SSD1306 setup leaves a 2-pixel line on the right edge. `platformio.ini` selects the SH1106 driver, which starts the picture at column 2. Board-level notes are in the technical spec, §4.9.

If the panel is a 128×32 SSD1306 instead, comment out `-DOLED_SH1106` and uncomment `-DOLED_128X32`.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/oled-hello
pio run -t upload
pio device monitor
```

The serial log prints an I2C scan until the OLED answers.
