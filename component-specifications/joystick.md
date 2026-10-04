# Analog joystick

A 5-pin stick module used by `rps-game` (Ink Duel) and `modak-catcher`. Axes are the ESP32-C6 ADC. The stick click is a digital input.

Typical module pins: `GND`, `+5V`, `VRx`, `VRy`, `SW`.

## Wiring

| Joystick | Header | GPIO | Notes |
|----------|--------|------|--------|
| GND | **GND** | — | Common ground |
| +5V | **3V3** | — | The module runs at 3.3 V. Do not use VSYS |
| VRx | **GP4** | 4 | ADC, X axis. Also SD MISO |
| VRy | **GP3** | 3 | ADC, Y axis. Also SD chip-select |
| SW | **TXD** | 16 | Stick click |

Leave the TF slot empty. A card shares GPIO3 and GPIO4 with the pots.

Leave GP12 and GP13 empty (USB).

`epaper_config.h` names: `JOY_VRX_PIN`, `JOY_VRY_PIN`, `JOY_SW_PIN`.

## Electrical behavior (lab-checked)

Setup in both games:

- `pinMode` on VRx and VRy is `INPUT` (no pull; the pots are the source).
- SW is `INPUT_PULLUP`.
- `analogReadResolution(12)` so a sample is 0…4095.

| Signal | Idle | Active |
|--------|------|--------|
| VRx, VRy | Center sits near **2000** (code treats center as 2048) | Full throw reaches roughly **50** at one end and **3300+** at the other |
| SW | High | **Low** when clicked. Opposite of the active-high button modules |

A dead stick that never leaves ~2000, or that only twitches a few counts, is almost always VRx/VRy on the wrong pins or the module powered from VSYS. SW that never changes is almost always the click wire not on TXD, or firmware looking for an active-high press.

## How the games read it

### Ink Duel (`rps-game`)

Direction is an edge, not a level. Deadzone is center ±**600**. If both axes are outside the deadzone, the larger delta wins. A new direction is posted only when it differs from the last one and at least **250 ms** have passed. Returning to center clears the last direction so the same way can be pressed again.

Low Y is **Up**. That mapping came from a probe on this module (low counts one extreme, high counts the other).

| Stick | Action |
|-------|--------|
| Left | Rock |
| Right | Paper |
| Up | Scissors |
| Down | Select / confirm (start, play, advance) |
| Click (SW) | Back to the title. Scores stay |

BOOT and PWR remain backups. Hold PWR still resets scores. SW is debounced at 25 ms and fires on the press, not the release.

### Modak Catcher (`modak-catcher`)

Only **X** moves Ganesh Ji. Center is 2048. Y is wired the same way and then ignored. Deflection is scaled as `(x - 2048) / 1400`, clamped to about ±1, and the catcher speed follows that.

SW, BOOT, and a short PWR press start a game or play again. A long PWR press returns to the title.

Analog X is sampled on a background task **while the panel is busy**, so the catcher keeps tracking the stick through a partial refresh.

## Conflict with buttons

GP3 is VRy here and the external button in the dino sketches and the stock ticker. GP4 is VRx here and the red button on the stock ticker. Do not combine those wirings.
