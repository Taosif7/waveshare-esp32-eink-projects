# Rock–Paper–Scissors (Ink Duel)

Aesthetic single-player RPS for the **Waveshare ESP32-C6-ePaper-1.54** (200×200 B/W).

## Controls

| Button | Action |
|--------|--------|
| **BOOT** (onboard) | Cycle move / advance screens |
| **PWR** (onboard) | Confirm / play; **hold** = reset scores → title |
| **EXT (GP3)** | Short = cycle/advance; long = confirm/play |

Onboard BOOT (GPIO9) and PWR (GPIO2) are polled on a background task so presses are not lost during e-paper refresh.

Note: GP3 is also the SD card CS line — leave the TF slot empty while using the external button. Do **not** use GP12/GP13 (USB D−/D+).

## Sounds

Uses the onboard **ES8311** codec + amp (EXIO3):

| Event | Sound |
|-------|--------|
| Boot / title | Short rising jingle |
| Cycle move | Soft click |
| Confirm / play | Two-note blip |
| Win | Rising arpeggio |
| Lose | Falling tones |
| Draw | Double mid tone |
| Reset | Low drop |

Connect a speaker to the board’s speaker header if one isn’t already attached.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/esp32
pio run -t upload
pio device monitor
```

If serial permission fails: `sg dialout -c 'pio run -t upload'` (or log out/in after joining `dialout`).

## Factory backup

A 4 MB flash dump is in `backup/factory_0x0_4MB.bin`. Restore with:

```bash
esptool --port /dev/ttyACM0 write_flash 0x0 backup/factory_0x0_4MB.bin
```
