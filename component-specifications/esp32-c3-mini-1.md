# ESP32-C3-MINI-1

Espressif's small Wi-Fi and Bluetooth LE module. The unit on the bench enumerates as native USB Serial/JTAG and reports the 4 MB, chip-revision v0.4 variant (ESP32-C3FH4 inside an N4 or H4 module). ESP32-C3-MINI-1U uses the same pads with an antenna connector instead of the PCB antenna. The board this unit is on has a blue LED on GPIO8, active low. An SH1106 OLED was run on the pins below.

| Item | Value |
|------|--------|
| Product | ESP32-C3-MINI-1 (this unit: 4 MB, chip rev v0.4) |
| Vendor | Espressif |
| MCU | ESP32-C3, QFN32, 32-bit RISC-V, up to 160 MHz |
| Flash | 4 MB Quad SPI inside the chip package (XMC, JEDEC `46 4016`) |
| SRAM | 400 KB, of which 16 KB is cache. Rev v0.4 leaves about 10 KB less for the app than rev v1.1 |
| ROM | 384 KB |
| Crystal | 40 MHz, on the module |
| Wireless | Wi-Fi 4 (802.11 b/g/n, 2.4 GHz), Bluetooth 5 LE |
| Supply | **3V3** pin, 3.0–3.6 V. GPIOs are 3.3 V |
| USB | Native USB Serial/JTAG, full speed. Linux sees `303a:1001` and `/dev/ttyACM0` |
| This unit | MAC `e0:72:a1:6f:62:64`, serial `E0:72:A1:6F:62:64` |
| Size | 13.2 × 16.6 × 2.4 mm (MINI-1). MINI-1U is 13.2 × 12.5 × 2.4 mm |
| Datasheet | https://documentation.espressif.com/esp32-c3-mini-1_datasheet_en.pdf |

The module has 53 pads. Most of the bottom is ground. Only 15 GPIOs come out. GPIO11–GPIO17 stay inside the package on the flash bus and are not pads.

## Power and enable

| Pad | Pin | Use |
|-----|-----|-----|
| GND | 1, 2, 11, 14, 36–53 | Ground. 36–53 are the thermal pad |
| 3V3 | 3 | 3.0–3.6 V in. There is no 5 V pin on the module |
| EN | 8 | CHIP_EN. High runs, low shuts the chip off. Do not leave it floating |
| NC | 4, 7, 9, 10, 15, 17, 24, 25, 28, 29, 32–35 | No connect |

## Available GPIOs

Pad numbers are the module's castellations, from the datasheet pin layout. "Safe" means the pin is not a strap and is not the USB pair this unit is using.

| Pad | Name | GPIO | Also | Wiring |
|-----|------|------|------|--------|
| 12 | IO0 | 0 | ADC1_CH0, 32 kHz XTAL P | Safe |
| 13 | IO1 | 1 | ADC1_CH1, 32 kHz XTAL N | Safe |
| 5 | IO2 | 2 | ADC1_CH2, FSPIQ | Strap. See below |
| 6 | IO3 | 3 | ADC1_CH3 | Safe |
| 18 | IO4 | 4 | ADC1_CH4, FSPIHD, MTMS | Safe |
| 19 | IO5 | 5 | ADC2_CH0, FSPIWP, MTDI | Safe as GPIO. ADC only while Wi-Fi is off |
| 20 | IO6 | 6 | FSPICLK, MTCK | Safe |
| 21 | IO7 | 7 | FSPID, MTDO | Safe |
| 22 | IO8 | 8 | Onboard LED, active low | Strap. Drive low to light the LED |
| 23 | IO9 | 9 | — | Strap. Low at reset enters download mode |
| 16 | IO10 | 10 | FSPICS0 | Safe |
| 26 | IO18 | 18 | USB D− | Leave for USB |
| 27 | IO19 | 19 | USB D+ | Leave for USB |
| 30 | RXD0 | 20 | U0RXD | Safe. Free while the console is USB |
| 31 | TXD0 | 21 | U0TXD | Safe. Free while the console is USB |

ADC1 is GPIO0–GPIO4. Those five channels keep working with the radio on. GPIO5 is the only ADC2 channel on this module, and the radio takes ADC2, so an analog read there is only valid with Wi-Fi off.

SPI0 and SPI1 belong to the in-package flash. The free SPI controller is SPI2. Its default pins are the FSPI names in the table (clock on GPIO6, data on GPIO7 and GPIO2, CS on GPIO10). The GPIO matrix can move SPI2, I2C, UART1, I2S, and LEDC onto any free GPIO. UART0 stays on GPIO21 (TX) and GPIO20 (RX) unless firmware remaps it.

There is no DAC and no touch peripheral.

## Pins that must stay alone

| GPIO | Function | Rule |
|------|----------|------|
| 18 | USB D− | This unit enumerates through these two pins. Reusing them drops `/dev/ttyACM0` |
| 19 | USB D+ | Same |
| 9 | Boot strap | Weak pull-up, default high. Hold it low at reset and the ROM waits for a download instead of starting flash |
| 8 | Boot strap | Sampled with GPIO9. Leave it floating at reset unless a download circuit drives it |
| 2 | Boot strap | Does not select the boot mode. Espressif still recommends a pull-up, because a glitch on this pin at reset is enough to upset the boot |

GPIO9 high and GPIO8 anything is SPI boot, which is the normal start from flash. GPIO9 low and GPIO8 high is joint download (USB Serial/JTAG or UART0). After reset the three strap pins are ordinary GPIOs.

GPIO4, GPIO5, GPIO6, and GPIO7 are also the JTAG set (MTMS, MTDI, MTCK, MTDO). USB Serial/JTAG is the debug path on this module, so those four stay available.

## What a carrier still has to provide

The module itself has no USB connector and no 5 V regulator. A board that plugs into a PC wires USB D− and D+ to GPIO18 and GPIO19, pulls EN up, and feeds 3.3 V into pin 3. The Type-C receptacle on the unit that answered here is that carrier, not a pin of the module.

## OLED on this board

The 4-pin SH1106 from the other projects. GPIO8 stays the LED, and GPIO9 stays the onboard BOOT button, so I2C is not on those pins.

| Signal | Board pin | GPIO |
|--------|-----------|------|
| OLED VCC | **3.3** | — |
| OLED GND | **G** | — |
| OLED SDA | **6** | 6 |
| OLED SCL | **7** | 7 |
| Button SIG (optional) | **3** | 3, active-high, internal pull-down |
| Jump, also | **BOOT** | 9, active-low |

The **5V** pin is USB power. Do not feed the OLED from it. GPIO18 and GPIO19 stay on the USB cable.

## Space shooter

`space-shooter` env `esp32-c3-mini` uses this module. There is no e-paper and no ES8311, so the sketch skips the warning paint and leaves sound off. Joystick axes are ADC1. GPIO2 and GPIO8 stay unused straps. Full tables are in `space-shooter/README.md`.

| Module pin | Board pin | GPIO |
|------------|-----------|------|
| OLED SDA | 6 | 6 |
| OLED SCL | 7 | 7 |
| ADXL345 SDA / SCL | 6 / 7 | same bus as the OLED |
| Joystick VRx | 0 | 0, ADC1 |
| Joystick VRy | 1 | 1, ADC1 |
| Joystick SW | 10 | 10, active-low |
| Fire SIG | 3 | 3, active-high |
| Fire, also | BOOT | 9, active-low. Strap: do not hold low at reset |
