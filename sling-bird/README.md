# Sling bird

A landscape slingshot on the **ESP32-C3 Super Mini** and the **SH1106** 128×64 OLED. The potentiometer sets the launch angle. Five touch pads are the sling: swipe from the right pad toward the left to pull, and lift your finger to fire.

The bird is a fixed-point rigid body. Wood, stone, and glass stacks tumble, sleep when they settle, and break into pieces. There is no e-paper and no speaker on this board.

Sprite poses follow the monochrome atlas in `assets/`. The frames in the firmware are the 10–16 px cuts that fit this glass.

## Controls

| Input | Action |
|-------|--------|
| Pot | Launch angle. From 100% to 70% of the knob the aim runs from straight up (75°) to forward (15°). Below 70% the aim stays forward. |
| Rightmost pad, then slide left | Pull. Power is where your finger is, not the furthest pad you reached. |
| Lift | Fire, after the pads have been idle for about 40 ms. |
| Lift while still on the rightmost pad | Cancel. |
| Hold any pad for 1.5 s during the pull | The bird and the sling shake. |
| Any pad on the result card | Next level, or retry. |

## Wiring — ESP32-C3 Super Mini

Power every module from **3.3**. Do not use **5V**. Leave **GPIO18 / GPIO19** for USB. Leave strap **GPIO2** alone.

| OLED | Board pin | GPIO |
|------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SDA | **8** | 8 |
| SCL | **9** | 9 |

GPIO9 is the boot strap. I2C leaves SCL high when idle, which is the normal start from flash. Do not hold GPIO9 low while the board resets, or the chip waits for a download instead of running the game. GPIO8 is also the onboard blue LED, active low, so the LED lights while the screen is being sent.

| Touch (left → right) | SIG → board pin | GPIO |
|----------------------|-----------------|------|
| 1 | **4** | 4 |
| 2 | **10** | 10 |
| 3 | **20** | 20 |
| 4 | **21** | 21 |
| 5 (start the swipe here) | **1** | 1 |

| Potentiometer | Board pin | GPIO |
|---------------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SIG / IO | **3** | 3 (ADC1_CH3) |

If every pad reads touched at rest, set `-DTOUCH_ACTIVE_HIGH=0` in `platformio.ini` and rebuild.

## Flash

```bash
pio run -e esp32-c3-mini -t upload
pio device monitor -b 115200
```

Or use the **Flash: sling-bird** run config.
