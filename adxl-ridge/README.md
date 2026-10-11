# ADXL ridge

A plane over an open range of topographic mountains, on the 128×64 SH1106 OLED. An ADXL345 on the same I2C header supplies pitch and roll. The craft stays fixed. Contour lines ahead grow, pass under the wings, and the same ridges are still there if you turn around. Each power-on grows a new range.

The onboard e-paper uses SPI. This sketch talks only to the external OLED and the accelerometer. The e-paper shows a warning mark and the words "Content on other display".

## Wiring

Power both modules from **3V3**. VSYS is about 5 V.

The OLED and the ADXL345 share SDA and SCL. Their addresses do not overlap (OLED `0x3C`, ADXL345 `0x53` with SDO grounded).

| ADXL345 | Header | Notes |
|---------|--------|--------|
| VCC | **3V3** | |
| GND | **GND** | |
| SDA | **SDA** (GPIO18) | Same wire as the OLED SDA |
| SCL | **SCL** (GPIO8) | Same wire as the OLED SCL |
| CS | **3V3** | High selects I2C. A 4-pin module already does this |
| SDO | **GND** | Address `0x53`. Tie to 3V3 instead for `0x1D` |
| INT1, INT2 | — | Leave open |

| OLED | Header |
|------|--------|
| VCC | **3V3** |
| GND | **GND** |
| SDA | **SDA** (GPIO18) |
| SCL | **SCL** (GPIO8) |

Leave **GP12** and **GP13** empty (USB). Do not put a TF card in the slot if you are also using GP3.

## Flying

The sketch opens on a menu over the range.

| Control | Action |
|---------|--------|
| **GP3** click | Switch the menu between Fly and Exit |
| **BOOT** short | Confirm. Fly starts the range. Exit shuts down |
| **BOOT** from flight | Open the menu again, on Fly |
| **PWR** click, or hold about 800 ms | Shut down, same as Exit |
| **BOOT** hold about 1 s, then release | Restart the app |
| Bank | Turn. Right wing down turns right |
| Nose up | Climb, high enough to clear a tall peak. The craft rises a little on the glass |
| Nose down | Drop toward the valley |
| **GP3** hold, in flight | Throttle. Speed eases up, then back when you let go |

The range is a wireframe grid. Tall peaks stand above level flight. Hold the nose up to climb over them, or turn aside. Flying into one tears the picture until you are clear.

Tip the board about halfway up from flat toward upright, so Y reads about -0.65 g. That pose is level. The onboard speaker plays a low engine drone and rises with the throttle.

**PWR** (GPIO2) latches the rail at boot, so you can let go once the menu is up. Exit or a later PWR press drops the latch and the OLED says **OFF**. On battery the board then shuts off when you let go. On USB the rails stay up, so the sketch sits on **OFF** until the next **PWR** press.

If an axis tilts backwards, flip `kRollSign` or `kPitchSign` at the top of `src/main.cpp`.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/adxl-ridge
pio run -t upload
pio device monitor
```

The serial log prints an I2C scan until the OLED answers, then the ADXL345 address. In flight it prints heading, altitude, and a g reading twice a second.
