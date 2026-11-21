# IMU prototype on the current board

Goal: log gyro/accel data next to each clip so Gyroflow can stabilise it. This step only proves that the wiring and the I2C link work; logging during recording comes after.

## What the stock kernel allows

From the round-1 probe (`/proc/iomem`, `/sys/bus`, dmesg):

| Interface | Status on the stock kernel |
|---|---|
| I2C 0, 1, 2 | Drivers loaded, `/dev/i2c-0..2` work. Bus 0 has the TP2850 at 0x44. Buses 1 and 2 had no devices and timed out on a scan, so their pins are probably not wired or not muxed to I2C. |
| SPI | Controllers exist, but the kernel has no SPI core (`isp_sensor_spi: Unknown symbol spi_sync`), so there is no spidev. |
| USB | No USB host controller is enabled (nothing in `/proc/iomem`, no `/sys/bus/usb`). |
| Networking | Not built into the kernel (no `/sys/class/net`, no `NET:` lines in dmesg). |
| SDIO (mmc1) | A second SD controller at 0x10020000 is enabled, with nothing on it. |

So the IMU goes on **I2C bus 0**, shared with the TP2850. WiFi is not possible with the stock kernel; see the last section.

## Parts

- An **LSM6DSO or LSM6DSOX breakout** (Adafruit LSM6DSOX, SparkFun Qwiic LSM6DSO, or a generic module). `imutest` reads its FIFO with hardware timestamps.
- Or a cheap **LSM6DS3TR-C module**. `imutest` supports it in polling mode (about 200 samples/s on this 100 kHz bus).
- If the bus turns out to run at 1.8 V (step 2): a 2-channel I2C level shifter (BSS138 or PCA9306 module).
- Thin wire (30 AWG wire-wrap or enamelled wire) and a fine soldering tip.

## Steps

1. **Find SDA and SCL of bus 0.** Look for the two resistors pulling the TP2850's I2C lines up to the IO supply, usually between the SoC and the TP2850. Their bus-side pads are SDA and SCL. With the board powered and idle, both lines sit at the IO voltage. Without a scope you can't tell which one is SDA; just wire it, and if `imutest` finds nothing, swap the two wires (that does no harm).
2. **Measure the bus voltage** on those pads with the board on: 3.3 V or 1.8 V.
   - 3.3 V: wire the breakout directly.
   - 1.8 V: use the level shifter (low side 1.8 V to the board, high side 3.3 V to the breakout). Never connect a breakout whose pull-ups go to 3.3 V straight onto a 1.8 V bus.
3. **Power the breakout** from the board's 3.3 V. The microSD socket is an easy place to tap it: pin 4 = VDD (3.3 V), pin 6 = GND. The IMU draws about 1 mA.
4. **Wire** VCC, GND, SDA and SCL, keeping the wires under ~10 cm. Leave the breakout's address jumper at its default; `imutest` tries 0x6A and 0x6B. Fix the breakout flat and rigid to the board (foam tape is fine for this test).
5. **Prepare the card:** copy `sdcard/dvr/imutest` and the new `sdcard/encrypt` onto the card, then create an empty file named `imutest` in the card root (or write `1` or `2` into it to test another bus).
6. **Boot with the camera connected** and keep the quad still for about a minute. Twenty seconds after boot, `imutest` runs for 30 s while dvrrec keeps recording.
7. **Send back** `probe/16_imu.txt`, `probe/imu.gcsv` and `dvr/log.txt`. The log shows whether recording lost any fields while the IMU test shared the bus.

### What a good result looks like

```
bus 0 addr 0x6a: WHO_AM_I 0x6c = LSM6DSO/LSM6DSOX
FIFO: ... 0 unknown tags
rates: gyro ~416 Hz, accel ~104 Hz
gyro bias within a few dps, noise well under 0.1 dps rms
|a| ~1.000 g
i2c errors: 0
```

Delete the `imutest` marker afterwards. Without it the hook does nothing.

## WiFi needs a different kernel

The stock kernel has no networking and no USB host, so no WiFi driver can load, whatever chip is attached. Options:

1. **Flash OpenIPC** (U-Boot, kernel and rootfs) to the 16 MB NOR chip. OpenIPC supports the gk7202v300 and ships WiFi drivers and hostapd. The full flash backups (`dump1.bin`) and the Raspberry Pi clip setup make it recoverable. dvrrec would need rebuilding for OpenIPC's C library.
2. **Boot OpenIPC from the SD card** by changing only the bootloader's `bootcmd`. This depends on whether the stock U-Boot has `mmc`/`fatload` support, which can't be read from the dump: the U-Boot code looks compressed. It would still need one write to the flash's environment area.
3. **Develop the offload software off-board first**: web UI, MP4 remux and the `.gcsv` download, on a PC or Raspberry Pi reading an SD card full of clips. Move to the board once the kernel question is settled.

Separately, the board's own WiFi options (USB data pads or the unused SDIO port) need a look at photos of both sides of the PCB.
