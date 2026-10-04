# Waveshare ESP32-C6-ePaper-1.54

The board every project in this repo is built for.

| Item | Value |
|------|--------|
| Product | ESP32-C6-ePaper-1.54 |
| Vendor | Waveshare |
| Typical SKUs | 34393 (battery included), 34394 (`-EN`, battery not included) |
| MCU | Espressif ESP32-C6, QFN40, single-core RISC-V up to 160 MHz |
| Flash | 16 MB |
| SRAM | 512 KB HP + 16 KB LP; 320 KB ROM |
| Wireless | Wi-Fi 6 (2.4 GHz), Bluetooth 5 LE, IEEE 802.15.4 |
| Display | 1.54″ e-paper, 200×200, black and white. See [epaper-1.54.md](epaper-1.54.md) |
| USB | Native USB Serial/JTAG on the Type-C connector. Linux sees `303a:1001` and `/dev/ttyACM0` |
| Expansion | 2×6 2.54 mm female header on the back |
| Official docs | https://docs.waveshare.com/ESP32-C6-ePaper-1.54 |
| Demo / BSP | https://github.com/waveshareteam/ESP32-C6-ePaper-1.54 |

PlatformIO board id used here: `esp32-c6-devkitc-1`, framework `arduino`, flash size **16MB**. Upload and monitor port is `/dev/ttyACM0` at 115200. Build flags include `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`.

## Expansion header

Physical 2×6 female header. The sticker names are not always `GPxx`.

| Left column | Right column |
|-------------|--------------|
| **RXD** = GPIO17 | **TXD** = GPIO16 |
| **SDA** = GPIO18 | **GP13** (USB D+) |
| **SCL** = GPIO8 | **GP12** (USB D−) |
| **GND** | **GP9** (BOOT) |
| **3V3** | **GP4** (ADC; also SD MISO) |
| **VSYS** (about 5 V from USB) | **GP3** (ADC; also SD CS) |

Power every external module from **3V3**. VSYS is about 5 V and will damage a 3.3 V OLED or accelerometer, and it is the wrong rail for the joystick pots.

Usable free GPIOs when the TF slot is empty: **GP3, GP4, TXD, RXD**. Only GP3 and GP4 among those are ADC inputs on the ESP32-C6. I2C stays on SDA and SCL.

## Pins that must stay alone

| GPIO | Function | Rule |
|------|----------|------|
| 12 | USB D− | Do not use as GPIO while the Type-C cable is the link to the PC |
| 13 | USB D+ | Same. A button or sensor here makes the board enumerate and then drop (`error -71`, no `/dev/ttyACM0`) |
| 9 | BOOT strap | Held low at reset enters the ROM download mode. Fine as a button once the app is running |

GPIO 4, 5, 8, and 15 are also strapping pins. The board already uses 5 and 15 for the e-paper. Do not add pull-downs that fight those straps at reset.

## Onboard chips

These sit on the board. External modules share the I2C bus with them.

| Block | Part | How the sketches reach it |
|-------|------|---------------------------|
| GPIO expander | TCA9554 at `0x20` (`ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000`) | I2C. See the EXIO table below |
| Audio codec | ES8311 | I2C plus I2S. Used by `rps-game` (SFX) and `adxl-horizon` (engine drone) |
| Amp enable | TCA9554 EXIO3 | Must be high before the speaker makes sound |
| RTC | PCF85063 at `0x51` | On the I2C bus. Not used by the current sketches |
| Temp / humidity | SHTC3 at `0x70` | On the I2C bus. Not used by the current sketches |
| Storage | MicroSD (TF), SPI | Shares clocks with the e-paper. Leave the slot empty when GP3 or GP4 is a control |
| Buttons | BOOT, PWR | See [button.md](button.md) |
| Power | Li-ion connector and charge path | PWR is a soft power button on battery |

### TCA9554 EXIO map

| EXIO | Function |
|------|----------|
| 0 | E-paper power. High before `EPD_Init()` |
| 1 | Audio power |
| 3 | Speaker amplifier enable |
| 4 | LED |
| 5 | VBAT / system power hold |

A normal sketch turns EXIO0, EXIO1, EXIO3, and EXIO5 on as outputs and drives them high before display or audio work. EXIO5 is the battery latch: the board is only powered while PWR is held down, until firmware sets EXIO5 high. `adxl-cube` and `adxl-horizon` do that latch themselves, then drop EXIO5 on a later PWR press so the board can shut off on battery. On USB the rails stay up, so those sketches sit on an OFF screen until the next PWR press.

### I2C

| Signal | GPIO |
|--------|------|
| SDA | 18 |
| SCL | 8 |
| Port | `I2C_NUM_0` |

An I2C scan on a bare board still lists the onboard parts (`0x20` TCA9554, and the codec, RTC, and SHTC3 when they answer). External parts add their own addresses: OLED `0x3C` or `0x3D`, ADXL345 `0x53` or `0x1D`.

### Audio I2S (ES8311)

Board config name in the codec stack: `C6_ePaper_1_54`.

| Signal | GPIO |
|--------|------|
| MCLK | 19 |
| BCLK | 21 |
| WS | 22 |
| DIN | 20 |
| DOUT | 23 |
| PA GPIO | none (`pa: -1`); the amp is EXIO3 |

`rps-game` plays 16 kHz, 16-bit, stereo tones. The speaker connects to the onboard MX1.25 header.

### MicroSD (leave empty when using the header for controls)

| Signal | GPIO |
|--------|------|
| CLK | 6 |
| MOSI / CMD | 5 |
| MISO / D0 | 4 |
| CS | 3 |

GPIO3 and GPIO4 are also the joystick Y/X pins and the external-button pins. A card in the slot fights those signals.

## Bring-up order

Sketches that use the e-paper follow this order:

1. Start the I2C master (SDA 18, SCL 8).
2. Init the TCA9554 and drive EXIO0, EXIO1, EXIO3, and EXIO5 high.
3. Init the e-paper SPI and panel.
4. Optionally start the ES8311 on the same I2C bus.
5. Configure BOOT and PWR as inputs with pull-ups. Configure any external button after that, and reclaim GP3 if the panel init touched the SD chip-select.
6. Run the UI. Poll buttons on a background task so a press during a ~2 s refresh is not lost.

OLED-only sketches (`oled-hello`, `odyssey-oled`, the ADXL pair) still power EXIO0 and paint the warning on the e-paper once, then talk only to I2C.

## Host connection

A healthy board shows:

```text
lsusb
# ID 303a:1001 Espressif USB JTAG/serial debug unit
```

The serial user needs to be in group `dialout`, or run uploads with `sg dialout -c 'pio run -t upload'`. Use a data-capable USB-C cable.

If upload cannot connect: unplug, hold **BOOT**, plug in, hold about two seconds, release BOOT.
