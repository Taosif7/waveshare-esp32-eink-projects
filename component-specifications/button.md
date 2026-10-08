# Buttons

Three kinds show up in these projects: the two onboard buttons, a single external 3-pin module, and a pair of those modules (red and green) on the stock ticker.

## Onboard BOOT and PWR

| Button | GPIO | Header label | Idle | Pressed | Electrical setup |
|--------|------|--------------|------|---------|------------------|
| BOOT | 9 | GP9 | High (internal pull-up) | Low | `INPUT_PULLUP`, active-low |
| PWR | 2 | not on the 2×6 sticker as a GPIO | High (internal pull-up) | Low | `INPUT_PULLUP`, active-low |

Both are momentary switches to ground. `OneButton` is used with `activeLow = true` where the library is linked (`rps-game`, `modak-catcher`, `stock-ticker`). Debounce in those games is 30 ms; a click window is 400 ms. PWR long-press is 900 ms in Ink Duel and the catcher.

BOOT is also the ROM download strap. Holding it while the chip resets enters the serial bootloader. `adxl-cube`, `adxl-tunnel`, and `adxl-horizon` only restart after BOOT has been held about 800 ms **and then released**, so the restart does not land in download mode.

PWR is the soft power key when this board runs from the battery. The rail stays up only while the button is down, until firmware sets TCA9554 EXIO5 high. See [esp32-c6-epaper-1.54.md](esp32-c6-epaper-1.54.md).

### What each sketch does with them

| Project | BOOT | PWR |
|---------|------|-----|
| `rps-game` | Short: cycle / advance | Short: confirm / play. Long: reset scores |
| `modak-catcher` | Short: start / play again | Short: start / play again. Long: title |
| `dino-game`, `dino-oled` | Same gestures as the external button | not a game control |
| `stock-ticker` | Same as the red button (next) | Same as the green button (confirm) |
| `adxl-cube`, `adxl-tunnel`, `adxl-horizon` | Hold ~1 s, then release: restart the app | Latches power on at boot. A later click, or a hold of about 800 ms, drops the latch and shows OFF |

Poll onboard buttons from a background task. A full e-paper refresh blocks the main loop for about two seconds, and a click in that window is otherwise missed.

## External 3-pin module

Lab-checked module: pins `VCC`, `SIG`, `GND`. Press connects SIG to VCC. It is **active-high**, the opposite of BOOT and PWR.

| Module pin | Header |
|------------|--------|
| VCC | **3V3** |
| SIG | **GP3** (GPIO3) |
| GND | GND |

Firmware: `INPUT` with **internal pull-down**. Pressed is `gpio_get_level == 1`.

`INPUT_PULLUP` on this module is wrong. Idle is already pulled toward the switch network, so the pin sits high whether or not it is pressed and the sketch sees no edges.

Leave the TF card slot empty. GPIO3 is the SD card chip-select.

Do not move this signal to GP12 or GP13.

### Gestures

`dino-game` and `dino-oled` OR the external button with BOOT (either one down counts as down). Debounce is 25 ms.

| Gesture | Action |
|---------|--------|
| Tap | Jump, start, or restart after a crash |
| Hold about 1 s | Pause or resume |
| Hold about 2.5 s | Back to the idle screen. The saved high score stays |

`adxl-horizon` uses the same GP3 module as a warp hold: speed eases up over about half a second while SIG is high, and eases back when it is released. It is not a click.

## Two external buttons (stock ticker)

Same 3-pin modules, both active-high, both with pull-downs. Leave the TF slot empty. GP4 is also e-paper MISO, so the sketch times holds from the raw pin level. SPI traffic on that pin would otherwise look like a release and cancel a long press.

| Button | SIG | Role |
|--------|-----|------|
| Red | **GP4** (GPIO4) | Next item, next chart, next duration. Hold on the chart: sell (fun mode) |
| Green | **GP3** (GPIO3) | Confirm, open, save. Hold on the chart: buy (fun mode) |

BOOT copies red. PWR copies green. Fun-mode hold time is `FUN_LONG_PRESS_MS` (600 ms) in `stock-ticker/src/fun_config.h`.

## Pin conflict

GP3 cannot be a button and the joystick Y axis at the same time. Projects pick one:

- Button on GP3: `dino-game`, `dino-oled`, `stock-ticker` (green), `adxl-horizon` (warp)
- Joystick VRy on GP3: `rps-game`, `modak-catcher`
