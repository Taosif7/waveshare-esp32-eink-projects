# Capacitive touch sensor (3-pin module)

External touch pad with pins labeled `VCC`, `GND`, and `SIG` / `IO`. The ESP32-C3 has no touch peripheral, so these modules are ordinary digital inputs. Bring-up: `touch-pot-test`. Game: `cocktail-lab` (same five pads as flavor bottles).

## Wiring — ESP32-C3 Super Mini

Share **3.3** and **G** across all pads. Never use the 5V pin. Leave GPIO2, GPIO8, GPIO9, GPIO18, and GPIO19 alone (straps and USB).

| Pad (OLED left → right) | SIG board pin | GPIO |
|-------------------------|---------------|------|
| 1 | **4** | 4 |
| 2 | **10** | 10 |
| 3 | **20** | 20 |
| 4 | **21** | 21 |
| 5 | **1** | 1 |

| Module | Board pin |
|--------|-----------|
| VCC (each) | **3.3** |
| GND (each) | **G** |
| SIG / IO | pin from the table |

GPIO3 is the potentiometer in `touch-pot-test`, not a touch SIG. See [potentiometer.md](potentiometer.md).

## Electrical behavior

Firmware in `touch-pot-test`:

- `pinMode(INPUT_PULLDOWN)` on each SIG
- Touched is `digitalRead == HIGH` (`TOUCH_ACTIVE_HIGH=1`)

| State | SIG level | OLED in `touch-pot-test` |
|-------|-----------|--------------------------|
| Idle | Low | Outlined circle |
| Touched | High | Filled disc |

Lab-confirmed on pad 1 (GPIO4). If every circle is filled at rest and empties when touched, the modules are active-low: set `-DTOUCH_ACTIVE_HIGH=0` and reflash.

## Proximity, LED, no signal strength

Lab: the onboard LED (and SIG) can trip when a finger is **near** the pad, before contact. That is normal capacitive sensing — the chip watches capacitance change and flips a threshold.

There is **no signal-strength output** on this 3-pin module. SIG is only idle vs active. The ESP32 cannot read “how close” or “how hard”; only whether the module has crossed its internal threshold. The LED is driven by that same binary detect, not by a separate analog channel.

Some boards have solder jumpers that change sensitivity or toggle vs momentary mode. That still only moves the trip point; it does not expose a continuous strength value.

## How the sketch uses it

`touch-pot-test` draws five boxes in a row and prints `T 01001` (one digit per pad) on serial every 200 ms.

## Conflict

On the Waveshare ESP32-C6-ePaper-1.54 header, GPIO4 is also joystick VRx / stock-ticker red button / SD MISO. Do not stack those roles on the same pin. On the C3 Super Mini used here, these five GPIOs are only the touch SIGs for this sketch (and conflict with `space-shooter`'s joystick / fire if both wirings are live).
