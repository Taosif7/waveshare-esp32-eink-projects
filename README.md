# waveshare-esp32-eink-projects

Projects for the [Waveshare ESP32-C6-ePaper-1.54](https://www.waveshare.com/esp32-c6-epaper-1.54.htm) board.

## Projects

| Folder | Description |
|--------|-------------|
| [`rps-game`](rps-game/) | **Ink Duel** — Rock–Paper–Scissors on e-paper with joystick, result images, and SFX |
| [`modak-catcher`](modak-catcher/) | **Modak Catcher** — Ganesh Ji catches falling modaks; analog stick, partial refresh |
| [`claude-desk`](claude-desk/) | **Claude desk** — live Claude Code sessions on e-paper; chime and answer from the panel over BLE |
| [`dino-game`](dino-game/) | **Dino** — Chrome T-Rex runner; one button, partial refresh, saved high score |
| [`dino-oled`](dino-oled/) | **Dino OLED** — same runner on a 128×64 SH1106 I2C display |
| [`space-shooter`](space-shooter/) | **Space Shooter** — portrait 64×128 shmup on the SH1106; joystick, fire button, saved scores. Also builds for the ESP32-C3 Super Mini |
| [`mini-doom`](mini-doom/) | **Mini Doom** — one-level raycasted maze shooter on the SH1106; ESP32-C3 Super Mini, joystick, touch to fire |
| [`stock-ticker`](stock-ticker/) | **Stock Ticker** — live Yahoo prices, selectable-range chart, 1 s poll over Wi-Fi |
| [`oled-hello`](oled-hello/) | **OLED Hello** — animated HELLO WORLD on a 4-pin I2C OLED |
| [`adxl-cube`](adxl-cube/) | **ADXL cube** — wireframe cube on the OLED that tilts with an ADXL345 |
| [`adxl-tunnel`](adxl-tunnel/) | **ADXL tunnel** — concentric wireframe tunnel on the OLED that steers with an ADXL345 |
| [`adxl-horizon`](adxl-horizon/) | **ADXL horizon** — attitude indicator on the OLED, driven by an ADXL345 |
| [`adxl-ridge`](adxl-ridge/) | **ADXL ridge** — topographic mountain flight on the OLED, steered by an ADXL345 |
| [`odyssey-oled`](odyssey-oled/) | **Odyssey** — 121-frame OLED animation looping at ~15 fps |
| [`juliet-oled`](juliet-oled/) | **Juliet** — 2414-frame OLED animation on the ESP32-C3 Super Mini |
| [`touch-pot-test`](touch-pot-test/) | **Touch + pot test** — five touch pads and a potentiometer live on the SH1106 (ESP32-C3 Super Mini) |
| [`cocktail-lab`](cocktail-lab/) | **Cocktail lab** — match layered drinks; flavor pads + flow-rate tap on the SH1106 (ESP32-C3 Super Mini) |
| [`sling-bird`](sling-bird/) | **Sling bird** — pot aims, a right-to-left swipe on five touch pads fires; SH1106 on the ESP32-C3 Super Mini |

## Hardware

Waveshare **ESP32-C6-ePaper-1.54** (200×200 B/W e-paper). See each project's `README.md` for build/flash notes.

## Board reference

Component wiring and lab notes live in [`component-specifications/`](component-specifications/), one file per part (this board, the ESP32-C3-MINI-1, the 1.54″ e-paper, SH1106, button, joystick, ADXL345, touch sensor, potentiometer).

See also [ESP32-C6-ePaper-1.54-Technical-Specification.md](ESP32-C6-ePaper-1.54-Technical-Specification.md) for USB caveats, toolchain, and bring-up.
