# ADXL horizon

Attitude indicator on the 128×64 SH1106 OLED. An ADXL345 on the same I2C header supplies pitch and roll. The spacecraft stays fixed in space. Below the horizon the earth is a world map in perspective: coasts stay small at the limb and grow as they pass under the wings. Stars expand out of that same point as you fly toward them. Tip the board and the planet moves behind the wings: nose up drops the limb, right wing down puts more earth on the right.

The onboard e-paper uses SPI. This sketch talks only to the external OLED and the accelerometer.

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

Leave **GP12** and **GP13** empty (USB). Do not put a TF card in the slot if you are also using GP3 or GP4.

**PWR** (GPIO2) turns the board on. The sketch latches that rail, so you can let go once the horizon is up. Click **PWR** again, or hold it until the OLED says **OFF**, to drop the latch. On battery the board then shuts off when you let go. On USB the rails stay up, so the sketch stops on **OFF** until the next **PWR** press. Unplug USB to test a real shutdown. Hold **BOOT** (GPIO9) for about a second, then release, to restart the app. Hold the external button on **GP3** (active-high, leave the TF slot empty) to warp. Speed eases up over half a second and stays there while the button is down. The stars stretch into lines. Release and it eases back over half a second.

The onboard speaker plays a low engine drone. The pitch and hiss rise with the warp and settle again when you let go.

The onboard e-paper shows a warning mark and the words "Content on other display".

Tip the board about halfway up from flat toward upright, so Y reads about -0.65 g. That pose is level, and the wings line up with the horizon. Tip the far edge down and the earth rises. Drop the right edge and ground fills the right side.

If an axis tilts backwards, flip `kRollSign` or `kPitchSign` at the top of `src/main.cpp`.

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Desktop/waveshare-esp32-eink-projects/adxl-horizon
pio run -t upload
pio device monitor
```

The serial log prints an I2C scan until the OLED answers, then the ADXL345 address and a g reading twice a second.
