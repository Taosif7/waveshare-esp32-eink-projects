# Space Shooter

A vertical shooter on the **128×64 SH1106** I2C OLED, rotated to portrait so the glass is **64 wide × 128 tall**. Waves of dome saucers slide and step down, a few peel off into a dive, asteroids burst into pixel debris, and every fifth wave is a boss. The onboard e-paper is painted once with a warning mark and the words "Content on other display".

Sprites are original 1-bit art in that chunky saucer style: glass dome, little face, dithered hull, arrow ship, cross stars. Saucers are 12×10, four across. The boss is the same saucer scaled up. High scores (top 3), difficulty, and the sound toggle are kept in NVS under the namespace `shooter`.

The sketch aims at a steady **30 fps** (`33 ms` frames). A full SH1106 buffer on 400 kHz I2C is most of that budget, so a late frame is skipped rather than stacked. The accelerometer sample is one 6-byte read and is skipped entirely when the chip is absent.

## Wiring

Power every module from **3V3**, not VSYS. Leave the TF slot empty. Leave **GP12** and **GP13** empty (USB).

| OLED | Header |
|------|--------|
| VCC | **3V3** |
| GND | **GND** |
| SDA | **SDA** (GPIO18) |
| SCL | **SCL** (GPIO8) |

Address `0x3C`, or `0x3D` if that is what answers. The boot log prints an I2C scan until one of them does. `platformio.ini` defines `OLED_SH1106`, which selects `U8G2_SH1106_128X64_NONAME_F_HW_I2C` with rotation `U8G2_R1`. If the picture is upside down, switch that to `U8G2_R3` in `src/oled.cpp`.

The ADXL345 shares that I2C bus. It is optional: the game still runs if the chip does not answer.

| ADXL345 | Header | Notes |
|---------|--------|--------|
| VCC | **3V3** | |
| GND | **GND** | |
| SDA | **SDA** (GPIO18) | Same net as the OLED |
| SCL | **SCL** (GPIO8) | |
| CS | **3V3** | High selects I2C. A 4-pin module already ties this |
| SDO / ALT | **GND** | Address `0x53`. Tie to 3V3 instead for `0x1D` |
| INT1, INT2 | leave open | The sketch polls |

The probe tries `0x53`, then `0x1D`, and checks DEVID `0xE5`. Samples are read at 100 kHz and the bus is put back to 400 kHz for the OLED. Flip `kRollSign` or `kPitchSign` at the top of `src/main.cpp` if an axis leans backwards.

| Joystick | Header | GPIO | Notes |
|----------|--------|------|--------|
| GND | **GND** | — | |
| +5V | **3V3** | — | Module runs at 3.3 V |
| VRx | **GP4** | 4 | X axis. Also SD MISO, so no TF card |
| VRy | **GP3** | 3 | Y axis. Low counts are up |
| SW | **TXD** | 16 | Stick click, active-low |

| Fire button | Header | GPIO | Notes |
|-------------|--------|------|--------|
| VCC | **3V3** | — | |
| SIG | **RXD** | 17 | Active-high, internal pull-down |
| GND | **GND** | — | |

**BOOT** (GPIO9) fires as well. Do not hold it through a reset; that is the download strap.

**PWR** (GPIO2) is not a game button. The sketch latches TCA9554 EXIO5 at boot so the battery rail stays up, and it turns the e-paper and the speaker amp on the same way.

## Controls

| Control | Action |
|---------|--------|
| Stick | Fly. On a menu, up and down move the cursor. Also banks the ship |
| Board tilt | Leans the starfield. Banks the ship with the stick. Ignored if the ADXL345 is absent |
| Fire (RXD) or **BOOT** | Shoot. Hold for about 6 shots a second, up to 3 bullets in flight. On a menu, select |
| Stick click | Pause, or go back |

The title items are Start, Difficulty (Easy / Normal / Hard), High scores, and Sound. Difficulty cycles when you select it. High scores is the top 3. Sound turns the speaker tones off without erasing scores.

Pause is Resume, Restart, and Quit. Quit drops back to the title and does not save the run. Game over saves the score if it beats one of the three. Fire starts again. Stick click returns to the title.

## Playing

Easy is 4 lives and a slower wave. Normal is 3. Hard is 2, with faster steps, more dives, and denser fire. Saucers step down when the formation hits the side and speed up as the row thins out. The bottom ship in a column sometimes dives in a sway and fires a short burst. Asteroids drift in and pop into debris when shot. A destroyed saucer sometimes drops a spread shot (about 8 seconds, three-way, still capped at 3 bullets) or a one-hit shield. Wave 5, 10, 15… is a boss.

Between waves a `WAVE N` plaque wipes across the glass. A boss wave says `BOSS`. Bullets and explosion debris leave a dithered trail that thins out over three frames. A hit or a large explosion shakes the picture by one pixel for a few frames. Killing the boss inverts the glass for three frames.

Tilt does not fly the ship. The stick does. With an ADXL345, roll shifts the near stars farther than the far ones and shears the field around the middle, and pitch leans it up or down. The ship sprite banks left, level, or right from that roll mixed with horizontal stick. With no accelerometer the stars stay put and the ship banks from the stick alone.

## Sound

Tones go to the onboard ES8311 and speaker, the same amp rails as the other sketches (`EXIO1` and `EXIO3`). They are short synthesized beeps, not the PCM clips from `rps-game`. If the codec does not ACK, the game still runs with the speaker quiet.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/space-shooter
pio run -t upload
pio device monitor
```

`tools/render_previews.py` rewrites `src/sprites.h` from the pixel art and writes 4× PNGs of the menu, a wave, game over, and a juice frame (banked ship, phosphor trails, wave plaque) to `assets/`.
