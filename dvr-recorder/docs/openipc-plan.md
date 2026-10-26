# Plan: OpenIPC on the board's NOR flash, for WiFi offload

Why: the stock kernel has no networking, no USB host and no SPI (see [prototype-imu.md](prototype-imu.md)). OpenIPC's kernel for this chip (`gk7202v300.generic.config`, Linux 4.9.37, same version as stock) has `CONFIG_NET=y`, `CFG80211=y`, `MAC80211=m`, a USB controller (DWC3 dual-role, xHCI), MMC/SDIO, I2C chardev and an SPI core.

The SD side-load recorder stays as the "nothing flashed" edition for stock boards. This is a second, flashed edition.

## User workflow this is for

- **Phone:** join the DVR's WiFi → open the DVR's page in the browser → download a clip (MP4) and its `.gcsv` → stabilise in the Gyroflow app → share to social media.
- **PC:** mount the DVR's clip folder over **SSHFS** and copy files in the file manager.
- **No USB connector.** The microSD card is the wired path.

## Phase 0: preparation (no flashing)

1. **Verify the backups.** `Desktop\60fpsDVR\Og Firmware dumps\dump1.bin` and `dump2.bin` (16 MB, identical, sha256 4146…0d8). Keep a third copy off this PC.
2. **Re-read the flash with the Raspberry Pi clip** and compare it to dump1. This proves the clip connection is reliable before anything is written.
3. **Find the UART pads** (TX, RX, GND; 3.3 V, 115200 baud) and connect a 3.3 V USB-serial adapter or the Pi's UART. This is needed for bring-up: until WiFi works there's no other way in (no Ethernet), and OpenIPC's U-Boot gives a console there.
4. **Take photos of both sides of the PCB** so we can find:
   - the USB D+/D- pads, or the SDIO (mmc1) pins, for the WiFi module;
   - the I2C pull-ups;
   - a button GPIO.

## Phase 1: plain OpenIPC, and a rollback rehearsal

1. Get the OpenIPC **gk7202v300** U-Boot and **lite** firmware: kernel and rootfs from the official release.
2. Build a 16 MB image with OpenIPC's default (8 MB) map:

   | Offset | Size | Content |
   |---|---|---|
   | 0x000000 | 256 KB | U-Boot |
   | 0x040000 | 64 KB | env (left erased, so U-Boot uses its defaults) |
   | 0x050000 | 2 MB | kernel |
   | 0x250000 | 5 MB | rootfs |
   | 0x750000 | rest, ~8.7 MB on this chip | rootfs_data, erased; formatted on first boot |

   The 8 MB map works on the 16 MB chip: `-(rootfs_data)` takes the rest of the chip, so the overlay gets about 8.7 MB. `run setnor16m` (bigger rootfs) can wait until the rootfs outgrows 5 MB.
3. Write it with flashrom, then read it back and compare.
4. Boot on the UART and check:
   - U-Boot and kernel start;
   - `/proc/mtd` shows the expected partitions;
   - `i2cdetect` shows the TP2850 at 0x44 on bus 0;
   - the SD card mounts;
   - free RAM.
5. **Rollback rehearsal:** flash `dump1.bin` back, confirm the stock firmware records, then flash OpenIPC again. After this, every later step is known to be reversible.

## Phase 2: our own OpenIPC build

Buildroot via the OpenIPC/firmware repo (`gk7202v300_lite_defconfig` as the base), in WSL. The toolchain is musl (`arm-openipc-linux-musleabi`).

- **Remove** majestic: dvrrec owns the video pipeline, and removing it frees about 1 MB of rootfs.
- **Keep** dropbear for SSH.
- **Add:**
  - hostapd + busybox udhcpd: the board runs its own access point;
  - an SFTP server for SSHFS. Dropbear has none built in, so use Buildroot's `gesftpserver` (small) or OpenSSH's `sftp-server`, installed where dropbear looks for it;
  - the WiFi driver (see Phase 4);
  - dvrrec, imutest, the web UI;
  - an init script that starts dvrrec at boot.
- **Load the MPP kernel modules** with OpenIPC's load script. The stock app used the fake sensor `imx307`; dvrrec does its own MIPI/TP2850 setup.

## Phase 3: port dvrrec

- Rebuild with the OpenIPC musl toolchain against the image's own MPP libraries.
- The board-uClibc link workaround (`sysroot-board`, `-nostdlib`) goes away.
- The `compat.h` time workaround stays harmless: musl's time types match its own kernel interface.
- **Acceptance test:** same results as the SD edition. That means 50.00/59.94 fps fields, 0 lost, audio in sync, power-cut-safe `.ts` files, and PAL/NTSC auto-detect.

## Phase 4: WiFi hardware and services

- **Chip:** OpenIPC packages drivers for, among others:
  - AIC8800: WiFi 6, dual band, USB or SDIO; the pick for transfer speed;
  - RTL8188FU / RTL8188EUS: 2.4 GHz only, cheapest, most proven on OpenIPC cameras;
  - MT7601U.

  Prototype with whichever fits the pads found in Phase 0 (USB, or SDIO on mmc1). The USB lines are internal; no connector.
- **Radio rules:**
  - WiFi only after landing: button long-press, auto-off after inactivity;
  - 5 GHz on channels 36–48, away from the 5.8 GHz video band.
- **Services:**
  - **Web page** (busybox httpd or a small server): clip list with thumbnails, download with HTTP Range (resume, preview), delete, settings, `.gcsv` download.
  - **SSHFS** over dropbear plus the SFTP server. PC setup:
    - Windows: WinFsp + SSHFS-Win, then `\\sshfs\root@<dvr-ip>\mnt\mmcblk0p1\DCIM`;
    - macOS: macFUSE + sshfs;
    - Linux: `sshfs` built in.
  - **Security:** a unique WiFi password and SSH password per board, printed on a label.

## Phase 5: Gyroflow files

- **MP4:** phones and Gyroflow need MP4, not `.ts`. Remux each clip to MP4 when it closes (no re-encode), keeping `.ts` as the power-cut-safe master. MP3-in-MP4 needs checking on iOS and Android; AAC is the fallback.
- **`.gcsv`:** one per clip with the same base name, written by dvrrec from the IMU FIFO, timestamped on the same clock as the video.
- **Check early** that the Gyroflow phone app loads a `.gcsv` next to a video: the docs list `.gcsv` as a supported source, but I found no clear confirmation for the mobile apps. A test clip + `imutest` log can settle it before Phase 4.
- **Lens profiles:** each user's analog camera needs one (chessboard calibration in Gyroflow). We can publish profiles for common FPV cameras at 960×576/480.

## Risks

- **RAM:** Linux gets 32 MB (rest is video memory). WiFi driver + hostapd + dropbear + httpd + dvrrec should fit, but measure in Phase 2.
- **Rootfs size:** 5 MB in the default map. Without majestic it should fit; otherwise `setnor16m` gives 10 MB.
- **USB host mode:** DWC3 is dual-role in OpenIPC's config; it may need forcing to host, and the WiFi module needs a 5 V or 3.3 V supply.
- **Gyroflow mobile `.gcsv` support:** unverified (Phase 5 test).
