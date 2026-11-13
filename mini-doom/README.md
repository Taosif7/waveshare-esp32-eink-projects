# Mini Doom

A one-level first-person maze shooter for the **ESP32-C3 Super Mini** and the **128×64 SH1106**. Its winding corridors stay open from the start to the exit. Troopers throw a slow shot, a brute closes in to bite, and the shotgun fires a spread after you find it.

This is an original game inspired by classic DOOM. It does not use a WAD or original game assets. The screen is monochrome and the board has no PSRAM or speaker.

## Controls

Push the joystick away to move forward (low Y counts as forward); left and right turn. Hold the fire button to shoot. Click the joystick to open or close the map; the map pauses the fight.

Press the fire button or click the joystick on the title, YOU DIED, or CLEARED screen to start again.

The HUD uses icons for health, armor, and ammo, with P/S indicating the selected weapon.

## Wiring — ESP32-C3 Super Mini

Power every module from **3.3 V**, not the 5V pin. Leave GPIO18 and GPIO19 for USB. GPIO2, GPIO8, and GPIO9 are reserved (GPIO8 is the LED; GPIO9 is BOOT).

| OLED | Board pin | GPIO |
|------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SDA | **6** | 6 |
| SCL | **7** | 7 |

The OLED address is usually `0x3C` or `0x3D`. `platformio.ini` selects the SH1106 driver. If the picture is upside down, switch `U8G2_R0` to `U8G2_R2` in `src/oled.cpp`.

| Joystick | Board pin | GPIO | Notes |
|----------|-----------|------|-------|
| GND | **G** | — | |
| VCC | **3.3** | — | Module runs at 3.3 V |
| VRx | **0** | 0 | Turn (ADC1) |
| VRy | **1** | 1 | Move forward/back; low counts move forward |
| SW | **10** | 10 | Active-low click; opens/closes map |

| Fire button | Board pin | GPIO | Notes |
|-------------|-----------|------|-------|
| One terminal | **4** | 4 | Fire; active-high with internal pull-down |
| Other terminal | **3.3** | — | Connect to 3.3 V when pressed |

## Build and flash

```bash
pio run -e esp32-c3-mini -t upload
```
