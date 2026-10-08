# Onboard e-paper

Built into the Waveshare ESP32-C6-ePaper-1.54. Every sketch that draws a game or a chart uses it as the main display. Sketches whose picture lives on the external OLED still paint this panel once, with a warning.

| Item | Value |
|------|--------|
| Size | 1.54 inch |
| Resolution | 200 × 200 |
| Colors | Black and white, 1 bit per pixel |
| Interface | 4-wire SPI, host `SPI2_HOST` |
| Framebuffer | `200 × 200 / 8 = 5000` bytes |
| Full refresh | about 2 seconds |
| Partial refresh | about 0.3 seconds |
| Power gate | TCA9554 EXIO0, driven high before init |
| BSP | `lib/board_bsp` in each e-paper project (`port_display`, `epaper_config.h`) |

The panel is reflective. It holds the last image with the rails on and does not need a constant refresh.

## SPI pins

| Signal | GPIO |
|--------|------|
| SCK | 6 |
| MOSI | 5 |
| MISO | 4 |
| CS | 7 |
| DC | 15 |
| RST | 11 |
| BUSY | 10 |

MISO (GPIO4) and the SD card chip-select (GPIO3) are the same header pins as a joystick or external buttons. Display init can claim them. Control sketches reconfigure GP3 and GP4 as inputs after the panel is up.

## Pixel buffer

In this BSP, bit `1` is white and bit `0` is black (`DRIVER_COLOR_WHITE = 0xFF`, `DRIVER_COLOR_BLACK = 0x00`). Byte index is `y * 25 + (x >> 3)`, MSB first inside the byte.

Color PNGs are not a framebuffer. Assets are packed 1-bit, or drawn with the BSP primitives (`EPD_FillRect`, `EPD_DrawString`, and the project sprite blitters). `modak-catcher` sprites use the opposite ink convention in their own arrays: bit `1` = black ink, bit `0` = transparent.

## Refresh

`EPD_Init()` loads the full waveform (`WF_Full_1IN54`). `EPD_Init_Partial()` loads the partial LUT (`WF_PARTIAL_1IN54`) and is what the games use while something is moving.

Partial updates ghost. The games clear that on purpose:

- `dino-game` does a full refresh about every 40 seconds when nothing is under the runner, and the run waits for BUSY.
- `modak-catcher` rewrites both RAM planes (commands `0x24` and `0x26`) when play starts, after a miss, and every 8 catches, then returns to partial mode.

BUSY is GPIO10, high while the panel is updating. `modak-catcher` samples the joystick on another task during that wait so the catcher does not freeze for the whole waveform.

There is no touch controller on this panel. `EPD_TP_RST_PIN` and `EPD_TP_INT_PIN` are `GPIO_NUM_NC`.

## Warning when another display is the UI

`adxl-cube`, `adxl-tunnel`, and `adxl-horizon` (and the same pattern is required for any sketch that also drives an OLED or LCD):

1. Turn e-paper power on first: TCA9554 EXIO0 high, then `PortDisplay_Init()` / `EPD_Init()`.
2. Draw a warning triangle with an exclamation mark and the exact words `Content on other display`.
3. Paint that once. Do not copy the other display's live text onto the e-paper.

Reference implementation: `adxl-cube/src/eink_warn.cpp`.

## Projects

| Project | How the panel is used |
|---------|------------------------|
| `rps-game` | Full screens (title, choose, reveal, score). Joystick and buttons. |
| `modak-catcher` | Partial refresh during play, full base-plane refresh to clear ghosts. |
| `dino-game` | Partial refresh at about 1/5 of the browser pace, because a frame is ~0.3 s. |
| `stock-ticker` | Menu, chart, and settings. Refreshes on a 1 s poll, so most updates are partial. |
| `adxl-cube`, `adxl-tunnel`, `adxl-horizon` | One-shot warning. The moving picture is on the OLED. |
