# Cocktail lab

Layer a mocktail to match the ticket. **ESP32-C3 Super Mini** + **SH1106**, same wiring as `touch-pot-test`.

## Controls (hold-to-pour)

| Input | Action |
|-------|--------|
| **Hold** a pad | That flavor bottle is active (lifts on screen) |
| Pot open while holding | **Pour** — flow rate from the knob (closed at the low end) |
| Release pad, or close tap | **Seal** the current layer |
| **Long-hold** any pad (~0.8 s) with tap **closed** | **Dump** the glass (charge bar on screen) |
| Glass full + tap closed | Auto **serve** / judge |

Recipes use **2–5** layers. Match flavor order and height (±3 px).

## Patterns (flavors)

Bayer dither fills: solid, 25%, 50%, 75%, hatch.

## Flash

```bash
pio run -e esp32-c3-mini -t upload
```

Or **Flash: cocktail-lab**.
