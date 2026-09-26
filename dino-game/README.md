# Dino

Chrome's offline T-Rex runner on the **Waveshare ESP32-C6-ePaper-1.54** (200×200 B/W).

The sprites, cactus layouts, jump arc, speeding-up scroll, pterodactyls, night invert, and `HI` score are the Chrome game. Partial refresh is about 0.3 s, so the world scrolls at 1/5 speed and the jump is stretched by the same amount. A hop still clears the same stretch of ground, relative to a cactus, as it does in the browser.

High score is stored in NVS and kept across reboots.

## Controls

One button. The external module on **GP3** is the control. **BOOT** does the same thing.

| Gesture | Action |
|---------|--------|
| Tap | Jump. On the idle screen, start. After a crash, restart. |
| Hold about 1 s | Pause, or resume if already paused |
| Hold about 2.5 s | Back to the idle screen. The saved high score stays. |

Every jump is the full hop. Release-to-shorten exists in Chrome, but on this clock a normal tap would always be a full hop anyway, and a medium hold would make the jump shorter. Duck is not mapped: low and mid pterodactyls are jumped, the high one is run under.

Wire the usual 3-pin module (active-high). Leave the TF slot empty — GP3 is also SD chip-select.

| Module | Header |
|--------|--------|
| VCC | 3V3 |
| SIG | **GP3** |
| GND | GND |

Do not use GP12 or GP13 (USB).

## How it plays

- Idle: blinking trex, horizon, `HI` if you have one.
- Run: small and large cacti, then pterodactyls once the pace picks up. Score flashes every 100. The panel inverts every 700 (night), with a moon and stars.
- Crash: dead trex, **GAME OVER**, restart icon. Tap to run again.
- About every 40 s, when nothing is underfoot, the panel does a full refresh to clear ghosting. The run waits out that refresh.

## Sprites

`tools/gen_sprites.py` packs the Chromium 1x runner assets in `assets/` (BSD-3-Clause) into `src/sprites.cpp`. Pterodactyl frames are drawn to the runner's 46×40 collision box. Regenerate with:

```bash
python3 tools/gen_sprites.py
```

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/dino-game
pio run -t upload
pio device monitor
```

If serial permission fails: `sg dialout -c 'pio run -t upload'`.

See the repo [technical specification](../ESP32-C6-ePaper-1.54-Technical-Specification.md) for pinout and USB caveats.
