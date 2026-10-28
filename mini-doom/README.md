# Mini Doom

A one-level first-person maze shooter for the **ESP32-C3 Super Mini** and the **128×64 SH1106**. Walls are raycast, doors rise, a red key opens the exit, troopers throw a slow shot, and a brute closes in to bite. The pistol is hitscan. The shotgun, once you find it, fires a spread.

This is an original game that uses those ideas. It is not id's Doom: no WAD, no shareware graphics, no PrBoom. The Super Mini has no PSRAM, so that port cannot run here. The glass is 1-bit and the bus is I2C, so the view is distance-dithered rather than textured in color. There is no speaker on this board, so there is no sound.

## Controls

Push the stick away from you to walk forward (low Y counts, same as the other sketches). Left and right turn. Hold the touch pad to fire. Click the stick on a door to open or close it. Click anywhere else to open the map; click again to close it. The map pauses the fight.

The HUD is `H` health, `A` armor, then `P` or `S` and the ammo count. `K` means you are carrying the red key.

Touch or click on the title, on YOU DIED, or on CLEARED starts the level over.

## Wiring — ESP32-C3 Super Mini

Power every module from **3.3**, not the 5V pin. Leave GPIO18 and GPIO19 on USB. Leave GPIO2, GPIO8, and GPIO9 alone (straps; GPIO8 is the LED, GPIO9 is BOOT).

| OLED | Board pin | GPIO |
|------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SDA | **6** | 6 |
| SCL | **7** | 7 |

Address `0x3C`, or `0x3D` if that is what answers. `platformio.ini` defines `OLED_SH1106`. If the picture is upside down, switch `U8G2_R0` to `U8G2_R2` in `src/oled.cpp`.

| Joystick | Board pin | GPIO | Notes |
|----------|-----------|------|--------|
| GND | **G** | — | |
| +5V | **3.3** | — | Module runs at 3.3 V |
| VRx | **0** | 0 | Turn. ADC1 |
| VRy | **1** | 1 | Forward / back. Low counts are forward |
| SW | **10** | 10 | Click, active-low. Doors, or the map |

| Touch | Board pin | GPIO | Notes |
|-------|-----------|------|--------|
| VCC | **3.3** | — | |
| GND | **G** | — | |
| SIG | **4** | 4 | Fire. Active-high, internal pull-down |

This is touch pad 1 from `component-specifications/touch-sensor.md`. It does not share a pin with the stick. A finger near the pad can count as a press. If the gun fires with nothing touching the pad, the module is active-low: set `-DTOUCH_ACTIVE_HIGH=0` and rebuild.

An active-high button on GPIO3 also fires, with an internal pull-down, so an unwired pin stays quiet. That is the same fire button `space-shooter` uses. You do not need it.

## Build

```bash
pio run -e esp32-c3-mini -t upload
```
