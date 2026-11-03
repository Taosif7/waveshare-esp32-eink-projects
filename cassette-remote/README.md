# Cassette

A Bluetooth LE media remote on the **ESP32-C3 Super Mini** and the **SH1106** 128×64 OLED. Five touch pads skip, seek, and toggle play. The potentiometer is the volume knob. The screen is a cassette: the reels spin with the local transport, and the tape pack moves from the left reel to the right.

There is no e-paper and no speaker on this board. Phase 2 (Apple Media Service now-playing) is not in this firmware.

## Controls

Pads are debounced for 25 ms. Play state on the screen is local, because a consumer-control host does not report it back.

| Pad (left → right) | GPIO | Tap | Hold |
|--------------------|------|-----|------|
| 1 Previous | 4 | One press of Scan Previous Track (`0xB6`). Restarts the local tape timer and whip-spins the reels backward. | Same single press. It does not repeat. |
| 2 Rewind | 10 | One short Rewind (`0xB4`), about 70 ms. | Rewind stays repeating (70 ms down, 80 ms up) until the finger lifts. Reels spin backward with eased acceleration, and the tape runs back toward the left reel. |
| 3 Play/Pause | 20 | Play/Pause (`0xCD`), and toggles the local reels. | Same single press. It does not repeat. |
| 4 Fast forward | 21 | One short Fast Forward (`0xB3`), about 70 ms. | Fast Forward repeats the same way as Rewind. Reels spin forward fast, and the tape runs toward the right reel. |
| 5 Next | 1 | One press of Scan Next Track (`0xB5`). Restarts the local tape timer and whip-spins the reels forward. | Same single press. It does not repeat. |

A tap on pad 2 or 4 is the first pulse of that same usage. Letting go before the next repeat (150 ms) leaves it as a single press, which a lot of players treat as a small skip. Holding keeps sending it.

The knob is the volume. An exponential moving average plus a deadband ignores ADC noise, then the travel is split into 16 key steps. One step sends a single Volume Up (`0xE9`) or Volume Down (`0xEA`). Steps are at least 55 ms apart, so a fast flick cannot flood the host. The on-screen meter is the knob position, not the phone's volume, and it stays up for about 1.5 s after the knob moves.

On this module the wiper voltage falls as the knob turns toward "more", so the firmware inverts the ADC. Clockwise toward that end raises the bar and sends volume up.

The local track timer is 90 seconds. It is an animation clock, not the length of the song.

## Wiring — ESP32-C3 Super Mini

Power every module from **3.3**. Do not use **5V**. Leave **GPIO18 / GPIO19** for USB. Leave strap **GPIO2** alone.

| OLED | Board pin | GPIO |
|------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SDA | **8** | 8 |
| SCL | **9** | 9 |

GPIO9 is the boot strap. I2C leaves SCL high when idle, which is the normal start from flash. Do not hold GPIO9 low while the board resets, or the chip waits for a download instead of running the sketch. GPIO8 is also the onboard blue LED, active low, so the LED lights while a frame is sent.

| Touch (left → right) | SIG → board pin | GPIO |
|----------------------|-----------------|------|
| 1 Previous | **4** | 4 |
| 2 Rewind | **10** | 10 |
| 3 Play/Pause | **20** | 20 |
| 4 Fast forward | **21** | 21 |
| 5 Next | **1** | 1 |

| Potentiometer | Board pin | GPIO |
|---------------|-----------|------|
| VCC | **3.3** | — |
| GND | **G** | — |
| SIG / IO | **3** | 3 (ADC1_CH3) |

The panel is an SH1106 at `0x3C` (also `0x3D`), full buffer, column offset handled by the U8g2 SH1106 driver. If every pad reads touched at rest, set `-DTOUCH_ACTIVE_HIGH=0` in `platformio.ini` and rebuild.

## Libraries

- [olikraus/U8g2](https://github.com/olikraus/u8g2) 2.36.18, `U8G2_SH1106_128X64_NONAME_F_HW_I2C`
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.5.2

HID is a small consumer-control report map in `src/ble_hid.cpp`, on NimBLE's HID device helper. T-vK's ESP32-BLE-Keyboard is not used; it misbehaves on Arduino core 3.x, which is what this platform package builds. 2.5 is the current 2.x line: its old `startServices()` call is a no-op, so the sketch starts the GATT server itself.

The panel is clocked at 800 kHz (`-DOLED_I2C_HZ=800000`) so a full-buffer frame can land near 30 fps. If the picture breaks up, set that flag to `400000` and rebuild. The probe for the panel address stays at 100 kHz either way.

## Build and flash

```bash
pio run -e esp32-c3-mini
pio run -e esp32-c3-mini -t upload
pio device monitor -b 115200
```

Or use the **cassette-remote** Flash run config. The monitor prints `fps=` every two seconds.

Serial `c` deletes stored bonds and starts advertising again. Use that when a phone is stuck remembering a bad pair. The host has to forget "Cassette" as well.

## Pairing

The radio advertises as **Cassette** with the HID service and the Generic Remote Control appearance. Pairing is Just Works (the phone asks you to confirm; there is no passkey). Bonds are stored in flash. After a reset the board advertises on the same public address, and the last host reconnects without another pair dialog while the pairing screen is up.

### iOS

1. Settings → Bluetooth.
2. Under Other Devices, tap **Cassette** and confirm the pair prompt.
3. It should show Connected. Media keys reach the lock-screen player and Control Center.
4. To start over: forget Cassette on the phone, then send `c` on the serial monitor (or erase flash) and pair again.

### Android

1. Settings → Connected devices → Pair new device.
2. Tap **Cassette** and accept the pair.
3. Keys should work in the system media session. Some skins hide that under "Input" or "Connected device preferences" if the first try does nothing.

### macOS

1. System Settings → Bluetooth.
2. Click Connect next to **Cassette**.
3. Keys go to whatever app owns media keys (Music, a browser, and so on).

### Windows

1. Settings → Bluetooth & devices → Add device → Bluetooth.
2. Choose **Cassette**. It may show up as an input device rather than a headphone.
3. Media keys follow the app that has the system media session.

## Screen

Drawn in code, full-buffer SH1106, one frame about every 33 ms. The shell follows the close-up of the norns cassette.

- A 1 px volume rail sits on the left, with a small foot and a wide flat knob.
- The shell that never moves is one bitmap: a 16-sided outline around the left reel, a dashed circle around the right, hollow guide nodes, the tape head and its roof, the slider rail and foot, and the four segment frames.
- The left pack is a round thick ring. The right pack is a slightly square thick ring. Both have a hollow square hub and three long spokes. The top tape run steps down near the middle onto the right pack. The left tape leaves that pack on its left side and runs down to the guide node.
- A `>` between the reels means play or fast-forward. Rewind mirrors it to `<`. Pause is two short bars.
- Four segment frames run along the bottom. Played segments are filled, with a small hollow window in the fill. A bold `v` sits over the active segment. The Bluetooth mark is only shown while pairing.
- While no host is connected the same shell stays up, the spokes keep turning, the caption reads `pairing...`, and the Bluetooth mark blinks. Tape amount still moves the thick rings only, and the tape lines follow those rings. The left ring stays the larger one at the start of a track.

## Known limits

- Many players ignore Fast Forward and Rewind. Previous, next, and play/pause are the ones that usually land.
- The knob cannot read the phone's volume. Each step sends a relative key, so if the host's volume was not already where the knob is, the two drift. The bar is only the knob.
- Play, pause, and the tape timer are local. The screen can say "playing" while the phone is stopped, and the other way around.
- GPIO9 is the boot strap. A reset that lands while SCL is low makes the chip wait for a download instead of starting the sketch. Re-plug USB if that happens.
- The blue LED is on GPIO8 (SDA), so it flickers with every frame.

## Phase 2

Apple Media Service is the iOS GATT service that publishes now-playing title, artist, duration, and playback state. It is not implemented here.

A later `ams` module would subscribe to the Entity Update characteristic after the HID link is encrypted, parse the attribute notifications, and write `playing` and the tape position into the same `Transport` the screen already draws. `transport_toggle_play` and the 90 s timer in `main.cpp` are the two spots that guess today. Android has no AMS, so those hosts would keep this local model.

## Host preview

`host/` compiles the draw code against U8g2's bitmap device and writes the PNGs and GIFs in `assets/`. It does not flash anything.

```bash
pio run -e esp32-c3-mini    # downloads U8g2 into .pio/libdeps
make -C host test preview
```
