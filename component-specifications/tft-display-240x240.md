# GMT130-V1.0 TFT display (ST7789, 240×240)

Lab-checked 1.3-inch IPS color TFT module. The back of the board is marked `GMT130-V1.0 IPS 240*240`. It uses an ST7789 controller and a write-only, 4-wire SPI interface. This specific module has a seven-pin header and **does not expose CS or SDO/MISO**.

| Item | Value |
|------|-------|
| Module | GMT130-V1.0 |
| Display type | 1.3-inch IPS TFT, RGB color |
| Active resolution | 240 × 240 pixels |
| Controller | ST7789 |
| Interface | 4-wire SPI (SCK, MOSI/SDA, DC, RES) |
| Header | GND, VCC, SCK, SDA, RES, DC, BLK |
| CS | Not present; controller remains selected |
| SDO / MISO | Not present; display cannot return data |
| Working supply | 3.3 V from ESP32-C3 Super Mini 3V3 |
| Working SPI mode | `SPI_MODE3` (clock idles high) |
| Working SPI clock | 1 MHz, a conservative bring-up speed |
| Working orientation | `tft.setRotation(2)` for the current upside-down mounting |
| Working library | Adafruit ST7735 and ST7789 Library with Adafruit GFX |

## Wiring to ESP32-C3 Super Mini

Use the GPIO numbers printed on the ESP32-C3 board.

| GMT130 pin | Function | ESP32-C3 pin used in the bring-up |
|------------|----------|----------------------------------|
| GND | Ground | GND |
| VCC | Display logic and panel supply | **3V3** |
| SCK | SPI clock | GPIO6 |
| SDA | SPI data input (MOSI), not I²C SDA | GPIO7 |
| RES | Active-low display reset | GPIO3 |
| DC | Data/command select | GPIO4 |
| BLK | Backlight control | GPIO5, driven HIGH |
| CS | No pin on module | No wire; configure CS as `-1` |
| SDO / MISO | No pin on module | No wire |

Connect grounds together. The ESP32-C3's GPIO signals are 3.3 V, matching the display's logic. Do not treat the pins named SCK and SDA as an I²C bus: SCK is SPI SCLK and SDA is SPI MOSI. BLK can be left open on modules that enable the backlight by default; in the tested wiring it is driven HIGH from GPIO5.

## Arduino / PlatformIO initialization

The no-CS panel needs a high-idle SPI clock. The successful lab configuration used hardware SPI, `SPI_MODE3`, and a 1 MHz SPI clock:

```cpp
static constexpr int TFT_SCLK = 6;
static constexpr int TFT_MOSI = 7;
static constexpr int TFT_CS = -1;
static constexpr int TFT_DC = 4;
static constexpr int TFT_RST = 3;
static constexpr int TFT_BL = 5;

SPIClass tftSpi(FSPI);
Adafruit_ST7789 tft(&tftSpi, TFT_CS, TFT_DC, TFT_RST);

void setup() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tftSpi.begin(TFT_SCLK, -1, TFT_MOSI, -1);
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(1000000);
  tft.setRotation(2);  // Rotate 180° to correct the current upside-down mounting.
}
```

`SPI_MODE0` with the same display and wiring produced a lit backlight but no pixels during bring-up. Switching to `SPI_MODE3` made the solid-color test visible. Keep this display on a dedicated SPI bus because it has no CS pin and cannot ignore traffic for other SPI peripherals.

## Bring-up checks

- Backlight on but no pixels: verify RES and DC are not swapped, verify SCK→GPIO6 and SDA→GPIO7, and check that initialization uses `SPI_MODE3` with CS set to `-1`.
- A running serial log only confirms the ESP32 sketch is running; this module has no MISO path to report whether it received commands.
- The current mounting is upside down; use `tft.setRotation(2)` to rotate the image 180°. For another mounting, `tft.setRotation(0..3)` selects the four orientations. If colors are wrong, try `tft.invertDisplay(true)`.
- Keep the SPI wires short. If the image is unstable, reduce the SPI clock further.

## Sources and bench confirmation

- [GMT130-V1.0 module datasheet](https://cdn.ozdisan.com/ETicaret_Dosya/1199674_484261982..pdf): identifies the ST7789, 240×240 panel and the seven connector pins.
- [ST7789V specifications (Sitronix)](https://www.sitronix.com.tw/en/products/aiot-device-ddi/): controller resolution, voltage, and interface details.
- [GMT130 SPI mode 3 troubleshooting report](https://qihao.dev/en/blog/st7789-gmt130-spi-mode3-black-screen-fix/): documents the same no-CS black-screen symptom and mode 3 test.
- Bench check on the ESP32-C3 Super Mini: the 240×240 RGB test remained visible while cycling colors after flashing the `SPI_MODE3`, 1 MHz configuration.
