# IMU probe (LSM6DSOX on a Luckfox Pico)

`imutest` is the bring-up test for the gyro + accelerometer that the Rockchip DVR will log next to each clip, so the footage can be stabilized in [Gyroflow](https://gyroflow.xyz). It is phase 0 of [`../ROADMAP.md`](../ROADMAP.md).

- It finds the chip and shows live values, to check the wiring and the axes.
- It streams gyro + accel through the chip's FIFO into a Gyroflow `.gcsv` log and reports whether any samples were lost.

Hardware: Luckfox Pico Pro (RV1106) and the Adafruit LSM6DSOX breakout. The program is a static ARM binary, so it also runs on the Pico Mini B and on a Radxa Zero 3W.

## Wiring (I2C)

Four wires. Do this with the Luckfox unpowered.

| Adafruit pad | Luckfox Pico Pro pin | Note |
|---|---|---|
| `VIN` | `3V3(OUT)` | **Not `VBUS` / 5 V.** The breakout pulls `SCL` and `SDA` up to `VIN`, and the Luckfox pins are 3.3 V. |
| `GND` | `GND` | |
| `SCL` | I2C3 `SCL` | |
| `SDA` | I2C3 `SDA` | |

Leave `3Vo`, `DO`, `CS`, `INT1` and `INT2` unconnected. The I2C address is then `0x6A` (it becomes `0x6B` when `DO` is tied to 3.3 V).

Where the I2C3 pins are: look for `I2C3_SCL` and `I2C3_SDA` in the pinout diagram on the Luckfox wiki page for the Pico Pro/Max. I2C3 can sit on one of two pin sets (the names end in `_M0` and `_M1`); `luckfox-config` (Advanced Options, then I2C) shows which set is on and lets you change it. Take the physical pin numbers from that diagram.

The breakout has its own pull-up resistors and level shifters, so nothing else is needed. Keep the wires short (under about 15 cm).

## Run it

1. Copy `board/imutest` to the board. With the Ethernet cable in:
   ```
   scp board/imutest root@<board-ip>:/root/
   ```
   (Or put it on the SD card.) Then on the board: `chmod +x /root/imutest`.
2. Find the chip and watch live values for 10 s:
   ```
   ./imutest
   ```
   Expected: `LSM6DSOX found (ID 0x6c)`, then four lines a second.
   - Lying still: gyro within a few deg/s of 0, `|accel|` near 1.00 g.
   - Turn the board around one axis at a time: only that gyro column should move. Tilt it: the 1 g moves between the accel columns. Note which way X, Y and Z point on the breakout.
3. Log for 30 s:
   ```
   ./imutest -o /root/test.gcsv
   ```
   It prints a progress line every second and ends with a summary and `RESULT: PASS` or `FAIL`.

If no chip is found, check the bus by hand: `i2cdetect -y 3` should show `6a`.

### Options

| Option | Meaning | Default |
|---|---|---|
| `-d` | `/dev/i2c-N` or `/dev/spidevX.Y` | search every I2C bus |
| `-a` | I2C address | try `0x6a`, then `0x6b` |
| `-r` | Sample rate in Hz: 104, 208, 416, 833, 1667, 3333, 6667 | 416 |
| `-g` | Gyro full scale in deg/s: 125, 250, 500, 1000, 2000 | 2000 |
| `-x` | Accel full scale in g: 2, 4, 8, 16 | 16 |
| `-t` | Run time in seconds, 0 = until Ctrl-C | 10 live, 30 logging |
| `-s` | SPI clock in Hz | 1000000 |
| `-o` | Log to this `.gcsv` file | live values only |

### Reading the summary

```
Bus speed     about 93 kHz (20 reads in 32.9 ms)
...
Samples       12503 in 30.0 s
Rate          416.9 Hz measured, 417.3 Hz expected (chip trim +0.15%)
FIFO peak     14 of 512 words
Overruns      0
Bus errors    0
Slot jumps    0, unpaired words 0, other words 0
RESULT: PASS
```

- **Bus speed:** measured at the start from 20 direct reads. It comes out a little under the bus setting (about 90 for 100 kHz). Far below it means the bus is misconfigured, and the log will fail.
- **Rate:** samples counted against the board's clock, next to the rate the chip says it runs at (its nominal rate corrected by its factory trim). They should agree within about 1%.
- **FIFO peak:** how full the chip's 512-word buffer got. One sample is two words. A peak near 512 means the bus barely keeps up.
- **Overruns:** the buffer overflowed and samples were lost. This is the only way samples get lost, and it fails the test.
- **Slot jumps, unpaired words:** extra consistency checks on the FIFO stream; both should be 0. Tell us if they are not while overruns are 0.

## How fast can it go

Each sample is 14 bytes on the bus.

| Bus | Highest rate that fits |
|---|---|
| I2C at 100 kHz (the Luckfox default, as far as we know) | 416 Hz |
| I2C at 400 kHz (`clock-frequency` in the board's device tree) | 1667 Hz |
| SPI at 1 MHz or more | 6667 Hz |

416 Hz is enough to try Gyroflow. The first test is whether 416 Hz passes on the stock I2C bus; then try `-r 833`, which should overflow at 100 kHz and pass on SPI.

## The log

`.gcsv` is Gyroflow's own text format: a short header with the scale factors, then one line per sample with the raw chip values.

```
GYROFLOW IMU LOG
version,1.3
id,open-dvr imutest
orientation,XYZ
note,LSM6DSOX 416.7 Hz 2000 dps 16 g
tscale,0.000001
gscale,0.0012217304764
ascale,0.000488
t,gx,gy,gz,ax,ay,az
843792240,0,-3,1,12,-25,2049
```

- `t` is in microseconds on the board's `CLOCK_MONOTONIC`, the clock Linux stamps video frames with: the time the FIFO was started plus the sample number times the chip's sample period.
- `gscale` turns gyro values into rad/s, `ascale` turns accel values into g.
- `orientation` is left at `XYZ`; set the real IMU orientation in Gyroflow once the IMU has a fixed place next to a camera.

To try it in Gyroflow before the DVR records video: tape the breakout to a phone or action camera, record a clip while `imutest -o` runs, then load the clip and the `.gcsv` in Gyroflow and let it synchronize.

## SPI (later)

For rates above 416 Hz. Six wires; enable SPI0 in `luckfox-config` first.

| Adafruit pad | Luckfox Pico Pro pin |
|---|---|
| `VIN` | `3V3(OUT)` |
| `GND` | `GND` |
| `SCL` | SPI0 `CLK` |
| `SDA` | SPI0 `MOSI` |
| `DO` | SPI0 `MISO` |
| `CS` | SPI0 `CS0` |

```
./imutest -d /dev/spidev0.0 -r 1667 -o /root/test.gcsv
```

## Build

The binary in `board/` is ready to use. To rebuild it (WSL Ubuntu or Linux):

```
sudo apt install gcc-arm-linux-gnueabihf
bash scripts/build.sh
```

This first runs the host test (`tests/imusim.c`: the chip code against a simulated LSM6DSOX), then builds `build/imutest` and copies it to `board/`.

| Path | What |
|---|---|
| `src/imutest.c` | The program: I2C and SPI access, live view, logging |
| `src/lsm6dsox.c` | Chip setup, FIFO reading, `.gcsv` writer (no Linux calls, shared with the test) |
| `tests/imusim.c` | Host test with a simulated chip |
| `board/imutest` | Built program for the board (static ARMv7 hard-float) |

## Status

- [x] Host test passes; the program was also run on a PC against a simulated chip on a slow bus (416 Hz passes at 100 kHz, 833 Hz overflows as expected)
- [ ] First run on the Luckfox Pico Pro: chip found, live values, axes
- [ ] 30 s log at 416 Hz with no overruns
- [ ] SPI, 833 and 1667 Hz
- [ ] Log synchronizes in Gyroflow
