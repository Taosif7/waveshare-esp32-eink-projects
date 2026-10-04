# ADXL345 accelerometer

A small I2C accelerometer on the same header bus as the OLED. Used by `adxl-cube` (wireframe cube) and `adxl-horizon` (attitude indicator). Both sketches talk to it with raw register reads on `Wire`, not a separate driver library.

## Wiring

Power from **3V3**. VSYS is about 5 V.

| ADXL345 | Header | Notes |
|---------|--------|--------|
| VCC | **3V3** | |
| GND | **GND** | |
| SDA | **SDA** (GPIO18) | Same net as the OLED and the onboard chips |
| SCL | **SCL** (GPIO8) | |
| CS | **3V3** | High selects I2C. A 4-pin module already ties this |
| SDO / ALT | **GND** | Address `0x53`. Tie to 3V3 instead for `0x1D` |
| INT1, INT2 | leave open | The sketches poll; they do not use the interrupt pins |

The OLED address (`0x3C` or `0x3D`) does not overlap `0x53` or `0x1D`, so both devices stay on one bus.

## Identity and setup

The sketches probe `0x53` first, then `0x1D`. A device has to ACK and then return DEVID `0xE5` from register `0x00`.

Init sequence, in order:

| Register | Address | Value | Meaning |
|----------|---------|-------|---------|
| POWER_CTL | `0x2D` | `0x00` | Standby. Data registers stay zero until measure mode |
| DATA_FORMAT | `0x31` | `0x09` | Full-resolution, ±4 g |
| BW_RATE | `0x2C` | `0x0A` | 100 Hz output data rate |
| POWER_CTL | `0x2D` | `0x08` | Measure |

Full-resolution mode is **256 counts per g** at this range and at the others. ±4 g is enough headroom for a shake without clipping a normal tilt. Samples are six bytes from `0x32` (DATAX0): X, Y, Z as little-endian int16, divided by 256 to get g.

Two I2C details that matter on the modules used here:

- Use a **STOP** between the register pointer write and the read. A repeated start makes some boards ACK and then never return the ID.
- Set **bit 7** of the register address on a multi-byte read (`0x32 | 0x80`). Without the multi-byte flag the chip repeats the first data byte and a 6-byte sample never finishes.
- The data read drops the clock to 100 kHz, then restores 400 kHz. The rest of the bus, including the OLED, stays at 400 kHz.

A still module, flat, chip facing up, reads about `X 0  Y 0  Z +1`. The Z sign follows which face is up.

## How each sketch turns g into a picture

Both expose `kRollSign` and `kPitchSign` at the top of `src/main.cpp`. Flip the one whose axis tilts backwards.

### Cube (`adxl-cube`)

Roll is `atan2(ay, az)`, pitch is `atan2(-ax, hypot(ay, az))`. Flat on the table shows a face of the cube. The bottom line of the OLED prints the three axes in g.

### Horizon (`adxl-horizon`)

The "level" pose is not flat on the table. Tip the board about halfway from flat toward upright (about 0.45 × π/2). In that pose Y is about **−0.65 g**, the wings line up with the horizon, and further pitch and roll move the horizon around the aircraft. Nose up drops the limb. Right wing down puts more ground on the right.

The external button on GP3 is a warp hold for this sketch only. It is not part of the accelerometer. See [button.md](button.md).

## If it does not show up

The serial log prints an I2C scan, then either `ADXL345 at 0x53` (or `0x1D`) or the wiring reminder: CS to 3V3, SDO to GND for `0x53`, SDA to SDA, SCL to SCL, VCC to 3V3.

An address that ACKs but fails the DEVID read is usually the repeated-start issue above, or SDO wired so the address is the other one. The probe tries both addresses.
