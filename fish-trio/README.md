# Fish trio

A top-down calico koi swims right to left across three panels:

1. **Right** — Waveshare ESP32-C6-ePaper-1.54, 200×200 black-and-white e-ink (27.6 × 27.6 mm, 7.25 px/mm). Slow flipbook, about 19 mm/s.
2. **Middle** — 1.3″ SH1106 128×64 OLED on an ESP32-C3 Super Mini (29.4 × 14.7 mm, 4.35 px/mm). Smooth monochrome, about 27 mm/s.
3. **Left** — GMT130-V1.0 240×240 ST7789 IPS on the same C3 (23.4 × 23.4 mm, 10.26 px/mm). Chunkier color, about 34 mm/s.

The koi is 17.5 mm long. Its pectoral span stays inside 9.5–9.8 mm, so it fits the 14.7 mm OLED with a gentle bob. Each display stays dark until the nose actually enters it, and goes dark again after the tail leaves. The fish is long enough to bridge both bezels: the nose is on the next glass while the tail is still leaving the previous one.

The C6 tells the C3 when the nose reaches the left edge of the e-ink (`HANDOFF`, with `y_mm`, `phase`, `speed_mm_s`, and `x_nose_mm`). The C3 waits out the bezel at the e-ink speed, then speeds up on the OLED and again on the IPS. When the tail clears the IPS it sends `CYCLE_DONE`. The C6 pauses briefly and starts another pass. If that packet is missed, a safety timeout on the C6 starts the next pass anyway.

## Wiring

Power every module from **3.3 V**. Do not use 5 V.

The C3 leaves GPIO8 (onboard LED, strap), GPIO9 (BOOT strap), and GPIO18/GPIO19 (USB) alone. The OLED is **not** on GPIO8/GPIO9.

### ESP32-C3 Super Mini → SH1106 OLED and GMT130-V1.0 TFT

| Module pin | Function | ESP32-C3 | Notes |
|------------|----------|----------|-------|
| OLED VCC | Power | **3V3** | Never 5 V |
| OLED GND | Ground | GND | Common with the TFT |
| OLED SDA | I2C data | **GPIO20** | Address `0x3C` (also accepts `0x3D`) |
| OLED SCL | I2C clock | **GPIO21** | 400 kHz. Not GPIO8/GPIO9 |
| TFT GND | Ground | GND | |
| TFT VCC | Power | **3V3** | Panel and logic. Never 5 V |
| TFT SCK | SPI clock | **GPIO4** | Dedicated SPI bus |
| TFT SDA | SPI MOSI | **GPIO6** | Data in. This is not I2C SDA |
| TFT RES | Reset | **GPIO5** | GPIO-controlled, so boot can reset the panel |
| TFT DC | Data/command | **GPIO7** | |
| TFT BLK | Backlight | **GPIO10** | PWM. Held off until the koi enters, then ramped up |
| TFT CS | Chip select | **no wire** | The GMT130 ties CS low on the PCB. Firmware uses `CS = -1` |

GMT130-V1.0 has no CS pin, so it cannot share a bus and it will not clock in `SPI_MODE0`. The sketch uses a dedicated SPI bus, **`SPI_MODE3`** (CPOL = 1, CPHA = 1), `invertDisplay(true)`, and `init(240, 240)`, which places the 240×240 window in the ST7789’s 240×320 RAM (row offset 80).

### ESP32-C6-ePaper-1.54

The e-ink panel, its SPI bus, and the TCA9554 power expander are onboard. This sketch does not use the expansion header for a second display.

| Control | GPIO | Behavior |
|---------|------|----------|
| PWR | **GPIO2** | Active-low. Firmware latches TCA9554 EXIO5 high at boot so the battery rail stays up after the button is released. Hold PWR about 0.8 s to drop the latch. |
| BOOT | **GPIO9** | Active-low, internal pull-up. Short press starts a pass now. Hold about 0.8 s to toggle the centerline. |
| USB D− / D+ | **GPIO12 / GPIO13** | Leave these free. |

## Build, test, flash

From this folder. Plug in **one** board at a time; both default to `/dev/ttyACM0`.

```bash
pio run -e esp32c6
pio run -e esp32c3
make -C host test
```

`pio run` with no `-e` builds both environments.

```bash
pio run -e esp32c6 -t upload
pio run -e esp32c3 -t upload
```

Serial monitor is 115200. The C6 prints `HANDOFF` when the nose leaves the e-ink. The C3 prints `HANDOFF` when it takes over and `CYCLE_DONE` when the tail clears the IPS.

Host tests cover the millimeter layout, bezel timing, reveal-on-entry, the koi’s size, and the ESP-NOW packet (magic, retries, sequence dedup). They do not need a board.

## Calibration

On boot, both boards show a horizontal line at world `y = 0` for about 5 seconds, with tick marks at ±5 mm and ±10 mm where the glass is tall enough. The OLED is only 14.7 mm tall, so it has the centerline and the ±5 mm ticks. Labels read `EINK`, `OLED`, and `IPS`.

Sit the panels edge to edge, right to left: e-ink, OLED, IPS. The three lines should be one straight line across the desk.

The five tuning values are at the top of `include/pond_config.h`. Change them, then rebuild **both** environments. They share one header.

| Define | Default | What it does |
|--------|---------|--------------|
| `GAP_EINK_OLED_MM` | `9.0` | Bezel between the e-ink’s left active edge and the OLED’s right active edge. Larger makes the C3 wait longer before the nose appears. |
| `GAP_OLED_TFT_MM` | `5.0` | Bezel between the OLED’s left active edge and the IPS’s right active edge. |
| `EINK_Y_OFFSET_MM` | `0.0` | Positive moves the e-ink centerline and the koi **up** on that glass. |
| `OLED_Y_OFFSET_MM` | `0.0` | Same for the OLED. |
| `TFT_Y_OFFSET_MM` | `0.0` | Same for the IPS. |

If the OLED line sits about 2 mm below the other two, set `OLED_Y_OFFSET_MM` to `2.0f`. If the koi seems to hesitate at a bezel, the gap constant is a bit large; if it pops early, the constant is a bit small.

`TFT_ROTATION` (0–3, just under those defines) flips the IPS image. Use `2` if that module is mounted upside down.

Hold **BOOT** on the C6 for about 0.8 seconds to bring the centerline back on all three screens. The C6 broadcasts that over ESP-NOW, so the C3 does not need its own button. A short BOOT press leaves calibration and starts a pass immediately.

## What each screen does

- **E-ink:** white water, a few stipple specks, two or three still lily-pad outlines, a black-and-white Kohaku koi, dotted ripples. Partial refresh is the flipbook (about 0.3 s). Every fourth pass, after the koi has left, the panel does a full ghost-clearing refresh and then sleeps until the next pass.
- **OLED:** stays in power-save until the nose crosses the right edge. Black water, white koi, dark calico cutouts, two lily pads that drift a pixel or two, dotted ripples. It goes black and back to sleep after the tail leaves.
- **IPS:** backlight PWM on GPIO10 stays at 0 until the nose enters, then ramps up. 120×120 logical pixels drawn as 2×2 blocks, and only the dirty rectangle is sent, so the panel can keep up while the OLED is also refreshing. Muted teal water, orange-and-white koi, a dark-teal drop shadow, two or three lily pads, and a few corner stones. Backlight and pixels go off after the tail leaves.

ESP-NOW is a broadcast to `FF:FF:FF:FF:FF:FF` on channel 1. There is no MAC address to type in. Each event is sent three times, about 10 ms apart, and the receiver keeps the first copy of a given sequence number.
