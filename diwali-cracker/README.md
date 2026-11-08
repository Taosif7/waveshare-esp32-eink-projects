# Diwali Cracker

ESP32-C3 Super Mini hosts a night-sky page. Press the physical button → 57 WS2812 LEDs chase like a fuse → the browser fires a realistic fireworks show with sound.

## Wiring

| Function | Pin | Notes |
|----------|-----|--------|
| WS2812 DIN | **GPIO4** | ~330 Ω series resistor |
| Button | **GPIO3** | Momentary to **GND** (internal pull-up) |
| Strip 5V | ESP32 **5V** (USB VBUS) | Brightness is limited in firmware for USB power |
| Strip GND | ESP32 **GND** | Same ground as the strip |

## Wi-Fi

Credentials live in the **workspace-root** `.env` (gitignored):

```bash
cp .env.example .env
# edit WIFI_SSID / WIFI_PASS
```

They are injected at build time by `scripts/load_env.py`.

## Flash

```bash
cd diwali-cracker
pio run -e esp32-c3-mini -t upload
pio device monitor -e esp32-c3-mini
```

Or use the workspace **Flash: diwali-cracker** run config.

Serial prints the IP (also try `http://diwali-cracker.local/`). Open that URL on a phone/laptop on the same network, **tap once** to unlock sound, then press the button.

## Show timing

- Chase: 3-dot fuse train, 28 ms/step × 57 ≈ 1.6 s
- WebSocket `fuse` when LEDs start (whistle), `blast` when that train ends (rocket)
- Multiple trains run in parallel on the strip; each press starts another immediately
