# Waveshare ESP32-C6-ePaper-1.54 — Technical Specification

Complete hardware and software reference for building, flashing, and extending firmware on this board. Based on Waveshare documentation, Espressif ESP32-C6 docs, and the local project at `~/Desktop/esp32` (Ink Duel RPS game).

---

## 1. Product identity

| Item | Value |
|------|--------|
| Product name | ESP32-C6-ePaper-1.54 |
| Vendor | Waveshare |
| Typical SKUs | 34393 (battery included), 34394 (`-EN`, battery not included) |
| MCU | Espressif **ESP32-C6** (QFN40), RISC-V single-core up to **160 MHz** |
| Flash | **16 MB** NOR (on-module / stacked) |
| SRAM | 512 KB HP + 16 KB LP (chip); 320 KB ROM |
| Wireless | Wi-Fi 6 (2.4 GHz), Bluetooth 5 LE, IEEE 802.15.4 (Zigbee/Thread) |
| Display | 1.54″ e-Paper, **200 × 200**, 1-bit B/W |
| Official docs | https://docs.waveshare.com/ESP32-C6-ePaper-1.54 |
| Demo / BSP repo | https://github.com/waveshareteam/ESP32-C6-ePaper-1.54 |
| Local project | `/home/taosif7/Desktop/esp32` |

---

## 2. Display (e-Paper)

| Parameter | Value |
|-----------|--------|
| Size | 1.54 inch |
| Resolution | 200 × 200 pixels |
| Colors | Black / white (1 bit per pixel) |
| Interface | 4-wire SPI |
| Full refresh | ~2 s (typical) |
| Partial refresh | ~0.3 s (typical) |
| Framebuffer size | `200 × 200 / 8 = 5000` bytes |
| Viewing | Passiveive; readable in ambient light |

**Pixel buffer convention (this BSP):** bit `1` = white, bit `0` = black (`DRIVER_COLOR_WHITE = 0xFF`, `DRIVER_COLOR_BLACK = 0x00`). Index: `y * 25 + (x >> 3)`, MSB-first within the byte.

Color PNGs/JPEGs are not usable directly; assets must be 1-bit or drawn procedurally.

---

## 3. Onboard peripherals

| Block | Part / function | Notes |
|-------|-----------------|--------|
| USB | Native USB Serial/JTAG | Type-C; programming + serial log |
| GPIO expander | **TCA9554** (I2C) | Power rails, LED, amp enable |
| Audio codec | **ES8311** | I2S + I2C; mic + speaker path |
| Amp enable | TCA9554 **EXIO3** | Must be driven high for speaker |
| RTC | **PCF85063** | I2C addr `0x51` |
| Temp/humidity | **SHTC3** | I2C addr `0x70` |
| Storage | MicroSD (TF) slot | SPI; FAT32 |
| Power | Li-ion connector + charge mgmt | PWR button soft-power when on battery |
| Buttons | **BOOT**, **PWR** (side) | See §5 |
| Expansion | 2×6 2.54 mm female header | See pin caveats §6 |

---

## 4. Pin map (project BSP)

Source of truth: `lib/board_bsp/include/epaper_config.h`.

### 4.1 E-Paper SPI

| Signal | GPIO |
|--------|------|
| EPD_SCK | 6 |
| EPD_MOSI | 5 |
| EPD_MISO | 4 |
| EPD_CS | 7 |
| EPD_DC | 15 |
| EPD_RST | 11 |
| EPD_BUSY | 10 |
| SPI host | `SPI2_HOST` |

### 4.2 I2C (shared: TCA9554, ES8311, RTC, SHTC3)

| Signal | GPIO |
|--------|------|
| SDA | 18 |
| SCL | 8 |
| Port | `I2C_NUM_0` |

### 4.3 Audio I2S (ES8311) — board cfg `C6_ePaper_1_54`

| Signal | GPIO |
|--------|------|
| MCLK | 19 |
| BCLK | 21 |
| WS | 22 |
| DIN | 20 |
| DOUT | 23 |
| PA GPIO | none (`pa: -1`); amp via expander EXIO3 |

Sample format used by this project: **16 kHz**, **16-bit**, **stereo**.

### 4.4 Buttons

| Button | GPIO | Active | Role in Ink Duel |
|--------|------|--------|------------------|
| BOOT (onboard) | **9** | Low | Cycle / advance |
| PWR (onboard) | **2** | Low | Confirm / play; long-press = reset |
| External (optional) | **3** | **High** | Short = cycle; long ≈ 0.8 s = play |

**External button module (lab-verified):** many 3-pin boards (`VCC` / `SIG` / `GND`) are **active-HIGH** — press drives `SIG` to 3V3. Idle is low when the ESP32 uses **internal pull-down**.

| Module pin | Board |
|------------|--------|
| VCC | 3V3 |
| SIG | **GP3** (header) |
| GND | GND |

Firmware (`configure_ext_button_gpio`): `INPUT` + **pull-down**; treat `gpio_get_level == 1` as pressed. Do **not** use `INPUT_PULLUP` / active-low logic on this module — idle and pressed both read HIGH, so the pin looks stuck and you get **zero edges**.

**Never** wire the external button to **GP12 / GP13** (USB). Prefer GP3; leave the TF slot empty (see §4.5).

### 4.5 Expansion header (back-sticker labels)

Physical **2×6** female header. Sticker names (not always `GPxx`):

| Left column | Right column |
|-------------|--------------|
| **RXD** (= GPIO17) | **TXD** (= GPIO16) |
| **SDA** (= GPIO18) | **GP13** (USB D+ — do not use) |
| **SCL** (= GPIO8) | **GP12** (USB D− — do not use) |
| **GND** | **GP9** (BOOT) |
| **3V3** | **GP4** (ADC; also SD MISO) |
| **VSYS** (~USB 5 V) | **GP3** (ADC; also SD CS) |

**Power rule:** power 3.3 V peripherals from **3V3**, never from **VSYS** (≈5 V) into ESP GPIO/ADC paths.

Usable free GPIOs for extras (TF empty): **GP3, GP4, TXD, RXD**. I2C is **SDA/SCL**. Only **GP3** and **GP4** among these are ADC-capable on ESP32-C6.

### 4.6 Analog joystick (lab-verified)

Typical 5-pin module: `GND`, `+5V`, `VRx`, `VRy`, `SW`.

| Joystick | Header | Notes |
|----------|--------|--------|
| GND | **GND** | Common ground |
| +5V | **3V3** | Module works at 3.3 V; do **not** use VSYS |
| VRx | **GP4** | ADC X axis |
| VRy | **GP3** | ADC Y axis; leave TF empty |
| SW | **TXD** (= GPIO16) | Stick click; use `INPUT_PULLUP` |

**Firmware notes:**
- `analogReadResolution(12)` → rest/center ≈ **2000** (of 0…4095); full throw reaches roughly **~50…~3300+**
- **SW** is **active-low** (pressed = 0) — opposite of the VCC button modules in §4.4
- Probe confirmed: axes OK (large X/Y deltas) and SW edges on TXD
- Deadzone ≈ center ±600; direction edge + ~250 ms cooldown in Ink Duel

**Ink Duel mapping:**

| Stick | Action |
|-------|--------|
| Left | Rock (Choose screen) |
| Right | Paper |
| Top (Up) | Scissors |
| Down | Select / confirm (start, play, advance) |
| Center click (SW) | Home → Title (scores kept) |

BOOT/PWR remain backups; hold PWR still resets scores.

**Do not** wire joystick signals to **GP12 / GP13**.

### 4.7 SD card (SPI, shared clocks with EPD data lines where noted)

| Signal | GPIO |
|--------|------|
| CLK | 6 |
| MOSI/CMD | 5 |
| MISO/D0 | 4 |
| CS | **3** |

**Conflict:** GPIO3 / GPIO4 are SD CS / MISO and also joystick Y / X (or EXT button on GP3). Leave the TF slot empty when using those header pins for controls.

### 4.8 TCA9554 expander (logical EXIOn)

| EXIO | Function |
|------|----------|
| 0 | EPD power |
| 1 | Audio power |
| 3 | Speaker amplifier enable |
| 4 | LED |
| 5 | VBAT / system power hold (battery soft-power) |

Firmware must turn **EXIO0, EXIO1, EXIO3, EXIO5** on (high as outputs) before display/audio work (see Waveshare audio / factory examples).

---

## 5. Critical USB / GPIO warnings (ESP32-C6)

| GPIO | Function | Rule |
|------|----------|------|
| **12** | USB D− (`USB_N`) | **Do not** use as GPIO while using Type-C USB |
| **13** | USB D+ (`USB_P`) | **Do not** wire buttons/sensors here — breaks USB enumeration |

Symptoms of misuse: device appears briefly then disconnects; kernel `error -71`; no `/dev/ttyACM0`.

Strapping / special pins to treat carefully: GPIO4, 5, 8, 9, 15 (see Espressif datasheet). BOOT (GPIO9) is also the download strap when held at reset.

---

## 6. Host PC detection (Linux)

When healthy:

```text
lsusb
# Bus ... ID 303a:1001 Espressif USB JTAG/serial debug unit

ls -l /dev/ttyACM0
# or /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_...
```

**Permissions:** user must be in group `dialout`:

```bash
sudo usermod -aG dialout $USER
# then log out/in
```

Until then: `sg dialout -c 'pio run -t upload'`.

**Download / recovery mode** (if flash fails):

1. Unplug USB.
2. Hold **BOOT**.
3. Plug USB (hold ~2 s), release BOOT.  
   Or: hold BOOT → pulse reset → release BOOT.

Use a **data-capable** USB-C cable.

---

## 7. Toolchain requirements

| Tool | Notes |
|------|--------|
| PlatformIO Core | Installed e.g. `~/.platformio/penv/bin` |
| Platform package | `pioarduino/platform-espressif32` **54.03.20** (Arduino-ESP32 **3.2.0** / IDF 5.4 libs) |
| Board id | `esp32-c6-devkitc-1` |
| Framework | `arduino` |
| System `esptool` | Optional; useful for full-chip backup/restore |
| Python | Used by PlatformIO / esptool |

Waveshare docs recommend Arduino-ESP32 **≥ 3.3.0** for their IDE examples; this project builds successfully on **3.2.0** via the PlatformIO package above.

**PATH helper:**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

---

## 8. Local project layout

```text
~/Desktop/esp32/
├── platformio.ini          # Build / upload config
├── README.md               # Short user guide
├── backup/
│   └── factory_0x0_4MB.bin # 4 MB factory flash dump
├── lib/
│   ├── board_bsp/          # Waveshare port: EPD, I2C, TCA9554 helpers
│   └── OneButton/          # Debounced button library
└── src/
    ├── main.cpp            # Power, buttons task, game loop
    ├── game.*              # RPS state machine + NVS high score
    ├── ui.*                # 200×200 ink UI
    ├── icons.*             # 1-bit rock/paper/scissors glyphs
    ├── sound.*             # Procedural SFX via ES8311
    ├── port_codec.*        # Codec init wrapper
    ├── codec_board/        # Waveshare codec board cfg + init
    └── esp_codec_dev/      # Espressif codec device stack (ES8311, …)
```

`platformio.ini` highlights:

- `upload_port` / `monitor_port`: `/dev/ttyACM0`
- `board_build.flash_size` / `board_upload.flash_size`: **16MB**
- `ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=1`
- Include paths for `esp_codec_dev` / `codec_board`
- `build_src_filter` excludes unused `lcd_init.c` / `codec_board/drv`

---

## 9. Build and flash (step-by-step)

### 9.1 One-time setup

```bash
# PlatformIO (if missing)
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py
python3 /tmp/get-platformio.py

sudo usermod -aG dialout $USER   # then re-login
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

### 9.2 Verify device

```bash
lsusb | grep 303a
ls -l /dev/ttyACM0
```

### 9.3 Build

```bash
cd ~/Desktop/esp32
pio run
```

### 9.4 Flash

```bash
pio run -t upload
# if permission denied:
sg dialout -c 'pio run -t upload'
```

### 9.5 Serial monitor

```bash
pio device monitor
# baud 115200
```

Expected early log lines include `RPS Ink Duel boot` and button GPIO levels; codec init prints `C6_ePaper_1_54` I2S/I2C config.

### 9.6 Factory image backup / restore

Backup (already present as `backup/factory_0x0_4MB.bin`):

```bash
esptool --port /dev/ttyACM0 read_flash 0x0 0x400000 backup/factory_0x0_4MB.bin
```

Restore:

```bash
esptool --port /dev/ttyACM0 write_flash 0x0 backup/factory_0x0_4MB.bin
```

---

## 10. Firmware bring-up sequence (required order)

Any new firmware should roughly follow Waveshare’s order:

1. Init I2C master (SDA=18, SCL=8).
2. Init TCA9554; set EXIO0/1/3/5 as outputs high (EPD + audio + amp + VBAT).
3. Init EPD SPI + panel (`PortDisplay_Init` → `EPD_Init` → clear / base image → optional partial mode).
4. Optionally init ES8311 via `Codec_StartInit()` / `sound_init()` (reuses existing I2C bus on IDF ≥ 5.4).
5. Configure onboard BOOT/PWR with internal **pull-ups** (active-low). Configure EXT on GP3 with internal **pull-down** (active-high module). Reclaim GP3 after SPI/display init (shared with SD_CS). Poll on a **background task** so presses are not lost during ~2 s full refresh.
6. Application UI / game loop.

---

## 11. Ink Duel application behavior

| Screen | Content | Inputs |
|--------|---------|--------|
| Title | Wordmark + three icons | **Down** / BOOT / PWR → start |
| Choose | Large gesture + label | **L/R/U** = Rock/Paper/Scissors; **Down** = play |
| Reveal | You vs CPU + result | **Down** → score |
| Score | W/L/D, streak, best (NVS) | **Down** → next choose; hold PWR = reset |

Joystick **center click** from any non-title screen → Title (scores kept). See §4.6.

Audio: procedural tones through ES8311 (title, click, win/lose/draw, reset). External speaker on MX1.25 speaker header if needed.

---

## 12. Alternate toolchains (vendor)

Waveshare also ships:

| Path | Notes |
|------|--------|
| Arduino IDE | ESP32 board package ≥ 3.3.0; copy `01_Arduino_Libraries` offline; examples under `02_Example/arduino_v3.3.0` |
| ESP-IDF | Examples under `02_Example/espidf_v5.5.3` (factory used **IDF v5.5.3** / project name `11_Fac` on stock devices) |

This Desktop project standardizes on **PlatformIO + Arduino**.

---

## 13. Troubleshooting

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| No `303a:1001` in `lsusb` | Cable / port / GP12–13 short | Data cable; free USB pins; try BOOT-held plug-in |
| Enumerates then disconnects (`error -71`) | USB D+/D− disturbed | Remove anything from GPIO12/13 |
| `Permission denied` on `/dev/ttyACM0` | Not in `dialout` | `usermod -aG dialout` + re-login, or `sg dialout` |
| Blank e-paper | Power rails off | Ensure TCA9554 EXIO0 (and init sequence) |
| No sound | Amp off / no speaker | Drive EXIO3 high; connect speaker header |
| EXT button flaky | SD card present on CS=GPIO3 | Remove TF card |
| EXT always HIGH / **0 edges** with pull-up | Active-HIGH module + `INPUT_PULLUP` | Use pull-down; pressed = HIGH (see §4.4) |
| Joystick X/Y stuck ~2000, tiny delta | VRx/VRy not on GP4/GP3, or powered from VSYS wrong | Wire per §4.6; power from **3V3**; TF empty |
| Joystick SW never toggles | SW not on **TXD**, or looking for active-high | SW → **TXD**; pull-up; pressed = LOW |
| USB dies when button/joystick wired | Signal on GP12/GP13 | Move off USB pins (see §4.5 / §5) |
| Flash connect fail | Not in download / busy port | Hold BOOT while plugging; close serial monitors |

---

## 14. Reference links

- Waveshare product wiki: https://docs.waveshare.com/ESP32-C6-ePaper-1.54  
- Waveshare GitHub demos: https://github.com/waveshareteam/ESP32-C6-ePaper-1.54  
- Espressif ESP32-C6 datasheet / GPIO (USB on GPIO12/13): Espressif documentation site  
- PlatformIO Espressif32 (pioarduino builds): https://github.com/pioarduino/platform-espressif32  

---

## 15. Checklist — “can build and flash from a clean machine”

- [ ] Linux (or similar) host with USB
- [ ] Data USB-C cable
- [ ] User in `dialout` (or use `sg dialout`)
- [ ] PlatformIO Core on `PATH`
- [ ] Project directory `~/Desktop/esp32` present
- [ ] Board powered; `lsusb` shows `303a:1001`; `/dev/ttyACM0` exists
- [ ] Nothing on GPIO12/13; TF empty if using GP3/GP4 (button or joystick)
- [ ] Joystick (if used): GND/3V3/GP4/GP3/TXD per §4.6
- [ ] `pio run -t upload` succeeds
- [ ] E-paper shows UI after ~2 s full refresh

---

*Document generated for local development of the Waveshare ESP32-C6-ePaper-1.54 and the Ink Duel firmware tree under `~/Desktop/esp32`.*
