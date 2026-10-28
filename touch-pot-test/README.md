# Touch + potentiometer test

Live bring-up for five 3-pin touch modules and one 3-pin potentiometer on the **ESP32-C3 Super Mini**, shown on the **SH1106** OLED.

- Five boxes: outlined circle = idle, filled disc = touched
- Progress bar under them: empty at display-value 0, full at 4095 (inverted vs raw ADC)
- Serial prints `T 01001  pot_raw=….  pot=….` every 200 ms

Specs: [`touch-sensor.md`](../component-specifications/touch-sensor.md), [`potentiometer.md`](../component-specifications/potentiometer.md).

## Wiring — ESP32-C3 Super Mini

Power every module from **3.3**. Do not use **5V**. Leave **GPIO18 / GPIO19** for USB. Leave straps **GPIO2 / GPIO8 / GPIO9** alone.

| OLED | Board pin | GPIO |
|------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SDA | **6** | 6 |
| SCL | **7** | 7 |

| Touch (left → right on OLED) | SIG → board pin | GPIO |
|------------------------------|-----------------|------|
| 1 | **4** | 4 |
| 2 | **10** | 10 |
| 3 | **20** | 20 |
| 4 | **21** | 21 |
| 5 | **1** | 1 |

Each touch: **VCC → 3.3**, **GND → G**, **SIG/IO →** the pin above.

| Potentiometer | Board pin | GPIO |
|---------------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SIG / IO | **3** | 3 (ADC1_CH3) |

## Flash and watch

```bash
pio run -e esp32-c3-mini -t upload
pio device monitor -b 115200
```

Or use the **Flash: touch-pot-test** run config.

## If the UI looks wrong

| Symptom | Fix |
|---------|-----|
| Circles filled at rest, empty when touched | Set `-DTOUCH_ACTIVE_HIGH=0` in `platformio.ini` and reflash |
| One circle never changes | Check that pad's SIG pin in the table |
| Pot bar stuck | Confirm pot SIG on GPIO3 |
| OLED blank | Reseat I2C; serial should show `0x3C` or `0x3D` |
