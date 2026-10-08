# Component specifications

Lab notes for the hardware this repo actually drives. Pin numbers, addresses, and quirks come from the sketches and from bring-up on the Waveshare ESP32-C6-ePaper-1.54, not from a generic module datasheet alone.

| Document | What it covers |
|----------|----------------|
| [esp32-c6-epaper-1.54.md](esp32-c6-epaper-1.54.md) | Waveshare ESP32-C6-ePaper-1.54: MCU, header, onboard chips, power, USB rules |
| [epaper-1.54.md](epaper-1.54.md) | 1.54″ 200×200 e-paper on that board |
| [sh1106.md](sh1106.md) | External 4-pin SH1106 128×64 I2C OLED |
| [button.md](button.md) | Onboard BOOT and PWR, plus the external 3-pin button modules |
| [joystick.md](joystick.md) | Analog stick on the expansion header |
| [adxl345.md](adxl345.md) | ADXL345 accelerometer on the shared I2C header |

## Which project uses what

| Project | E-paper | OLED | Buttons | Joystick | ADXL345 |
|---------|---------|------|---------|----------|---------|
| `rps-game` | UI + sound | | BOOT, PWR | Direction + click | |
| `modak-catcher` | UI | | BOOT, PWR | X axis + click | |
| `dino-game` | UI | | BOOT, external on GP3 | | |
| `dino-oled` | | UI | BOOT, external on GP3 | | |
| `stock-ticker` | UI | | BOOT, PWR, red on GP4, green on GP3 | | |
| `oled-hello` | warning notice | UI | | | |
| `odyssey-oled` | | UI | | | |
| `adxl-cube` | warning notice | UI | BOOT (restart), PWR (power latch) | | tilt |
| `adxl-tunnel` | warning notice | UI | BOOT (restart), PWR (power latch) | | tilt |
| `adxl-horizon` | warning notice | UI | BOOT, PWR, external on GP3 (warp) | | attitude |

Sketches that draw on the OLED still paint the onboard e-paper once: a warning mark and the words `Content on other display`.

## Header pins that get reused

The 2×6 header only has a few free pins. These assignments are the ones the sketches share. Do not put two of these on the same pin at once.

| Header | GPIO | Used as |
|--------|------|---------|
| SDA | 18 | I2C data: OLED, ADXL345, and the onboard chips |
| SCL | 8 | I2C clock |
| GP3 | 3 | Joystick VRy, or one active-high button. Also SD card chip-select |
| GP4 | 4 | Joystick VRx, or a second active-high button. Also SD card MISO and e-paper MISO |
| TXD | 16 | Joystick click (SW), active-low |
| RXD | 17 | Free in these sketches |
| GP9 | 9 | Onboard BOOT. Also on the header |
| 3V3 / GND | — | Power for every external module |

Leave **GP12** and **GP13** empty. Those are USB D− and D+. Wiring them drops the board off the USB bus.
