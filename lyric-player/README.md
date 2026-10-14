# Lyric Player

A landscape player for the **128×64 SH1106** I2C OLED. Sixteen 1-bit frames live in flash. A timed lyric file on LittleFS picks the frame and types its line out, character by character, for as long as that line lasts. It builds for the Waveshare ESP32-C6-ePaper-1.54 and for an ESP32-C3 Super Mini (ESP32-C3-MINI-1). On the C6, the onboard e-paper is painted once with a warning mark and the words "Content on other display". The C3 has no e-paper, so that paint is skipped.

The clock is `millis()`. **BOOT** (GPIO9, active-low) starts the file again from `00:00.00`. Do not hold BOOT through a reset; on the C3 it is the download strap.

No song lyrics are stored in the repo. The firmware reads whatever you put in `data/lyrics.txt`. `lyrics.example.txt` is only placeholders.

## Lyric file

Copy the example, edit the placeholders, and upload the filesystem:

```bash
cp lyrics.example.txt data/lyrics.txt
pio run -t uploadfs
```

`data/lyrics.txt` is gitignored. `pio run -t uploadfs` writes the `data/` folder to the LittleFS partition (the `spiffs` slot in `partitions.csv`, formatted as LittleFS). On the device the file is `/lyrics.txt`.

Each cue is one line:

```text
mm:ss.xx|frame|text
```

| Field | Meaning |
|-------|---------|
| `mm:ss.xx` | Start time from playback zero. `mm` is minutes, `ss` is seconds (`00`–`59`), `xx` is centiseconds |
| `frame` | Picture number `1`–`16`, matching `assets/frames/f01.png`–`f16.png` |
| `text` | The line to type. Anything after the second `\|` |

Blank lines and lines that start with `#` are ignored. Cues are played in time order. A line's duration is the gap until the next cue, and the letters spread across that whole gap. The last cue types for 4 seconds, then stays up. A gap of zero shows the whole line at once. Text wraps to at most two lines inside that frame's box; anything past the second line is clipped.

`lyrics.example.txt`:

```text
00:00.00|1|Line one goes here
00:04.00|2|Line two goes here
```

Changing the file does not require a firmware rebuild. Upload the filesystem again. The sketch and the filesystem are separate images: `pio run -t upload` writes the firmware, `pio run -t uploadfs` writes LittleFS.

## Picture and type

Frames are 128×64, landscape, rotation `U8G2_R0`. `tools/png_to_frames.py` turns the PNGs into PROGMEM XBM arrays in `include/frames.h` (LSB is the left pixel, white pixels are lit). Regenerate with:

```bash
python3 tools/png_to_frames.py
```

That needs Pillow (`pip install pillow`).

When the cue changes frame, the new picture comes in with a left-to-right dither wipe of about half a second. The line is drawn in `u8g2_font_lubBI12_tr` with a 1px black halo: the glyphs are drawn in black at the eight neighbour offsets, then in white on top. A cursor blinks after the last typed character until the line is finished.

The text box depends on the frame. Frames **5**, **11**, and **15** use the left half, so a line there has to be short (`Line five` fits on two lines; `Line five goes here` does not). The others use the open bottom band, the open centre band, or the open top band when the drawing sits low.

| Frames | Box |
|--------|-----|
| 5, 11, 15 | Left half, `x=0` `w=64` |
| 1, 7, 8, 13, 16 | Bottom |
| 2, 3, 6, 9, 10, 14 | Centre |
| 4, 12 | Top |

If `/lyrics.txt` is missing, frame 1 stays up with `Add lyrics.txt`.

## Wiring — ESP32-C6 ePaper

Power the OLED from **3V3**, not VSYS. Leave the TF slot empty. Leave **GP12** and **GP13** empty (USB).

| OLED | Header |
|------|--------|
| VCC | **3V3** |
| GND | **GND** |
| SDA | **SDA** (GPIO18) |
| SCL | **SCL** (GPIO8) |

Address `0x3C`, or `0x3D` if that is what answers. The boot log prints an I2C scan until one of them does. `platformio.ini` defines `OLED_SH1106`, which selects `U8G2_SH1106_128X64_NONAME_F_HW_I2C` with rotation `U8G2_R0`. If the picture is upside down, switch that to `U8G2_R2` in `src/oled.cpp`.

**BOOT** (GPIO9) restarts playback. Do not hold it through a reset.

**PWR** (GPIO2) is not a player button. The sketch latches TCA9554 EXIO5 at boot so the battery rail stays up, and it turns e-paper power on (EXIO0) before the panel init.

## Wiring — ESP32-C3 Super Mini

This is the ESP32-C3-MINI-1 module documented in `component-specifications/esp32-c3-mini-1.md`. The board has no e-paper, so the warning screen is skipped. Power the OLED from the **3.3** pin. The **5V** pin is USB power; do not feed the OLED from it.

Leave **GPIO18** and **GPIO19** on the USB cable. Leave **GPIO2** and **GPIO8** alone (boot straps; GPIO8 is also the onboard LED). GPIO11–GPIO17 are inside the module on the flash bus and are not pads.

| OLED | Board pin | GPIO | Notes |
|------|-----------|------|--------|
| VCC | **3.3** | — | Not the 5V pin |
| GND | **G** | — | |
| SDA | **6** | 6 | Lab-checked I2C data |
| SCL | **7** | 7 | Lab-checked I2C clock |

Address `0x3C`, or `0x3D` if that is what answers. Same `U8G2_R0` landscape rotation as the C6. If the picture is upside down, switch that to `U8G2_R2` in `src/oled.cpp`.

**BOOT** (GPIO9) restarts playback, active-low. Do not hold it while the chip resets: GPIO9 low at reset waits for a download instead of starting flash.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/lyric-player
pio run
pio run -e esp32-c6-epaper -t upload
pio run -e esp32-c3-mini -t upload
pio run -e esp32-c3-mini -t uploadfs
pio device monitor
```

`pio run` builds both environments. Pass `-e` when flashing so the C6 image is not written to the C3, or the other way around. The C3 image is the 4 MB ESP32-C3-MINI-1. The C6 image stays the 16 MB e-paper board. Both images use the 4 MB partition table in `partitions.csv`, with a LittleFS slot named `spiffs`.

Upload the lyric file after the firmware, or the other way around. `uploadfs` does not erase the application, and `upload` does not erase LittleFS.
