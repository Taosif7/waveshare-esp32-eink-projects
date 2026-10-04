# waveshare-esp32-eink-projects

Projects for the [Waveshare ESP32-C6-ePaper-1.54](https://www.waveshare.com/esp32-c6-epaper-1.54.htm) board.

## Projects

| Folder | Description |
|--------|-------------|
| [`rps-game`](rps-game/) | **Ink Duel** — Rock–Paper–Scissors on e-paper with joystick, result images, and SFX |
| [`modak-catcher`](modak-catcher/) | **Modak Catcher** — Ganesh Ji catches falling modaks; analog stick, partial refresh |
| [`dino-game`](dino-game/) | **Dino** — Chrome T-Rex runner; one button, partial refresh, saved high score |
| [`dino-oled`](dino-oled/) | **Dino OLED** — same runner on a 128×64 SH1106 I2C display |
| [`stock-ticker`](stock-ticker/) | **Stock Ticker** — live Yahoo prices, selectable-range chart, 1 s poll over Wi-Fi |
| [`oled-hello`](oled-hello/) | **OLED Hello** — animated HELLO WORLD on a 4-pin I2C OLED |
| [`adxl-cube`](adxl-cube/) | **ADXL cube** — wireframe cube on the OLED that tilts with an ADXL345 |
| [`adxl-horizon`](adxl-horizon/) | **ADXL horizon** — attitude indicator on the OLED, driven by an ADXL345 |
| [`odyssey-oled`](odyssey-oled/) | **Odyssey** — 121-frame OLED animation looping at ~15 fps |

## Hardware

Waveshare **ESP32-C6-ePaper-1.54** (200×200 B/W e-paper). See each project's `README.md` for build/flash notes.

## Board reference

Component wiring and lab notes live in [`component-specifications/`](component-specifications/), one file per part (this board, the 1.54″ e-paper, SH1106, button, joystick, ADXL345).

See also [ESP32-C6-ePaper-1.54-Technical-Specification.md](ESP32-C6-ePaper-1.54-Technical-Specification.md) for USB caveats, toolchain, and bring-up.
