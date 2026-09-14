# Modak Catcher

Catch falling modaks as **Ganesh Ji** on the **Waveshare ESP32-C6-ePaper-1.54** (200×200 B/W).

Three misses and the game is over. Best score is kept in NVS.

## Controls

| Input | Action |
|-------|--------|
| Joystick **X** (tilt left / right) | Move Ganesh Ji |
| Joystick **click** (SW) | Start / play again |
| **BOOT** or **PWR** (short) | Start / play again |
| **PWR** (hold) | Back to title |

Joystick wiring matches the board notes (leave the TF slot empty):

| Stick | Header |
|-------|--------|
| GND | GND |
| +5V | **3V3** (not VSYS) |
| VRx | **GP4** |
| VRy | **GP3** (unused in this game) |
| SW | **TXD** (GPIO16), active-low |

## How it plays

- Title → start → catch modaks; three misses ends the run.
- Overlap Ganesh Ji with a modak to catch it.
- Three hollow life dots in the HUD: each miss empties one. At zero, **GAME OVER** shows score and best.

## Smoothness on e-paper

Full refresh is ~2 s; a partial waveform is ~0.3 s. The game is built around that:

1. **Partial refresh during play** — only the moving sprites change in the framebuffer. The HUD is snapshotted and restored so unchanged pixels do not flash.
2. **Non-blocking BUSY** — analog X is sampled on a background task *while* the panel is updating, so Ganesh Ji tracks the stick instead of waiting on the waveform.
3. **Time-based motion** — catcher and modak positions use elapsed milliseconds, not frames, so speed stays honest at ~3 partial frames/s.
4. **Base-plane refresh** — entering play, after a miss, and every 8 catches, both EPD RAM planes are rewritten (full waveform) to wipe ghost trails, then partial mode resumes.

Placeholder 1-bit silhouettes ship in `src/sprites_data.h`. Swap them for your art (see below).

## Replace Ganesh Ji / modak graphics

Drop **high-contrast** PNGs into `assets/` (dark ink on light/transparent). Then:

```bash
pip install pillow
python3 tools/png_to_sprite.py assets/ganesha.png SPRITE_GANESHA --max-width 56 --max-height 48
python3 tools/png_to_sprite.py assets/modak.png SPRITE_MODAK --max-width 20 --max-height 22
```

Paste the printed arrays over the placeholders in `src/sprites_data.h`. Keep them modest in size — large sprites smear more on partial refresh.

Format: packed MSB-first, bit `1` = black ink, bit `0` = transparent, row stride `(W+7)/8`.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/modak-catcher
pio run -t upload
pio device monitor
```

If serial permission fails: `sg dialout -c 'pio run -t upload'`.

See the repo [technical specification](../ESP32-C6-ePaper-1.54-Technical-Specification.md) for pinout and USB caveats. Do **not** wire the joystick to GP12/GP13.
