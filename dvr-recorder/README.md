# Open DVR recorder (GK7202V300 + TP2850)

Our own recording program for the AliExpress analog FPV DVR board. It runs from the SD card (side-loaded through the board's `encrypt` boot hook), so nothing is flashed and removing the files restores the stock firmware.

It is built on open-source work from the [OpenIPC](https://openipc.org) project; see [Credits and licences](#credits-and-licences).

## Why

The probes (see `../dvr-probe/`) showed that each raw TP2850 frame holds two separate fields (top 288 lines and bottom 288 lines in PAL), but the stock recorder encodes the top field twice. So its 50/60 fps files only have 25/30 distinct images per second. This recorder encodes both fields: real 50 fps in PAL and 60 fps in NTSC.

## The board

Sold as the BAYCKRC / COOAI "MINI DVR" by Sichuan Xilian Zhikong Technology Co., Ltd. The silkscreen reads `cooeye MINIDVR V1.2 23-3-15`. Stock firmware: `V2.4_20251208`.

| | From the manual |
|---|---|
| Size, weight | 26 x 28 x 6 mm, 3.8 g, 4x M3 holes on 20 x 20 mm |
| Power | 5-30 V, 230 mA at 5 V; 5 V or 9 V recommended |
| Connector | SH1.0 mm 3-pin: VIDEO IN, 5-30V IN, GND (the same signals on the solder pads `Video`, `VCC`, `G`); 30 AWG cable included |
| Storage | microSD up to 512 GB, Class 10 |
| On board | microphone, button, blue LED, red LED |
| Button and LED pins (found by `gpioprobe`) | button GPIO6_4 (reads 0 when pressed), blue LED GPIO0_4, red LED GPIO0_3 (both lit at 1) |

### Masked chips

The chip markings are sanded off. This is what they are and how we know:

| Part | Identified as | Evidence |
|---|---|---|
| SoC | Goke GK7202V300: ARM Cortex-A7, H.264/H.265 hardware encoder, built-in audio codec | MPP chip ID `0x72020300`; kernel "Machine model: Goke GK7202V300"; CPU ID `410fc075` (Cortex-A7) |
| RAM | DDR inside the SoC package, 64 MB | Linux gets 32 MB (`mem=32M`) and the media buffers sit above that (at `0x42228000`); 64 MB is the in-package size for this chip family |
| Video decoder | Techpoint TP2850: analog (CVBS / AHD) to MIPI CSI-2, register-compatible with the TP9950 | I2C bus 0, address `0x44`, ID registers `0xFE`/`0xFF` = `0x28 0x50` |
| Flash | Winbond W25Q128, 16 MB SPI NOR (about 9 MB used by the stock firmware) | Kernel boot log: `w25q128(b/f)v (Chipsize 16 Mbytes)`; matches the 16 MB dump |
| Audio codec | Built into the SoC (I2S "inner codec"), mono mic input | `/proc/umap/acodec` |
| Power regulators | Not identified | |

Software on the board: Linux 4.9.37, uClibc 0.9.33, Goke MPP `MPP_V1.0.1.0 B020`.

## Wiring

These are the two options from the board's manual. In both, the DVR's video input is wired in parallel with the existing video line.

### Option 1: record the camera picture (no OSD)

```
Camera VIDEO ──┬────────────────► FC  CAM pad
               └────────────────► DVR VIDEO IN
FC 5V ──────────────────────────► DVR 5-30V IN
FC GND ─────────────────────────► DVR GND
Battery (4.5-30 V) ─────────────► Camera power
```

### Option 2: record with the flight controller's OSD

```
Camera ──► FC CAM pad ──► FC adds OSD ──► FC VTX video out ──┬──► VTX VIDEO
                                                             └──► DVR VIDEO IN
FC 5V ──────────────────────────► DVR 5-30V IN
FC GND ─────────────────────────► DVR GND
FC 5V, GND ─────────────────────► VTX power
```

Interference: the diagonal "chain-link" pattern in our flight clips is also in the stock firmware's footage, so it comes in on the analog video, not from the recorder. Things to try on the hardware side: a clean 5 V supply for the DVR, a short video wire twisted with its ground, and distance from the ESCs and VTX.

## Video modes

Both recorders capture 960 pixels per line (960H). Bitrates are what the encoder aims for (constant bitrate).

| Input | Stock firmware (manual) | dvrrec |
|---|---|---|
| PAL | 960x768, 50 fps, AVI, 5-20 Mbit/s. Only 25 distinct images per second (each field is encoded twice). | **960x576, 50 fps**, every frame a separate field (960x288 doubled to full height). Shown as 4:3 (768x576). |
| NTSC | 960x640, 60 fps, AVI, 5-20 Mbit/s. Only 30 distinct images per second. | **960x480, 59.94 fps**, every frame a separate field (960x240 doubled). Shown as 4:3 (640x480). |
| AHD 720p | 1280x720 at 25/30/60 fps | **Not supported yet.** The TP2850 can decode AHD, but dvrrec only has PAL/NTSC tables. With an AHD camera it will most likely find no picture (untested) and restart every 10 s, so use the stock recorder (`stock` marker, below). |

Bitrate is set with `MaxBitRate` (Mbit/s). Tested up to 20 Mbit/s for whole flights with no lost frames.

| MaxBitRate | Per minute | 5-minute clip | 64 GB card holds |
|---|---|---|---|
| 8 (default when the key is missing) | about 60 MB | about 300 MB | about 17 h |
| 15 (stock default) | about 113 MB | about 560 MB | about 9 h |
| 20 (our card) | about 150 MB | about 750 MB | about 7 h |

## Video encoding

```
camera ─► TP2850 (decode, one field per MIPI frame)
       ─► MIPI CSI-2, 2 lanes, YUV 4:2:2 8-bit
       ─► VI (raw bytes, 1920 per line) ─► NEON: UYVY to YVU 4:2:2 semi-planar (about 3 ms per field)
       ─► VPSS: 960x288 to 960x576 (PAL) / 960x240 to 960x480 (NTSC), 4:2:0
       ─► H.264 encoder ─► MPEG-TS writer
```

- Codec: H.264 Baseline profile, 8-bit 4:2:0, encoded by the SoC's hardware encoder.
- Rate control: CBR at `MaxBitRate`, measured over 1-second windows.
- GOP: one IDR frame every 2 s (100 frames PAL, 120 NTSC), then P frames only (no B frames), QP of I frames 2 lower than P.
- Fields: each field becomes one frame, scaled to full height (no weaving), so motion is as smooth as the camera's field rate. Each frame has the vertical detail of one field (288 / 240 lines).
- Shape: the stream carries a sample aspect ratio (4:5 PAL, 2:3 NTSC), so players show 4:3.
- Timing: frame timestamps come from the capture clock, so files play at the camera's real rate (50.00 or 59.94). The stream also states the nominal 50/60 fps.
- Container: MPEG-TS, PIDs `0x100` video (also carries the PCR) and `0x101` audio, PMT on `0x1000`. Each frame starts with an access unit delimiter.
- Files: `DCIM/DVR_0001.ts`, `DVR_0002.ts`, ... A new clip starts on an IDR frame every `FileIntervalTime` minutes.
- Power cut: data is flushed to the card every second, so pulling the battery loses at most about one second. The cut file still plays.

## Audio channel

```
mic ─► SoC codec (analog gain, ADC) ─► AI: 32 kHz, 16-bit, mono, 10 ms blocks
    ─► 80 Hz low-cut ─► AudioGain ─► MP3 encoder (shine) ─► MPEG-TS
```

- Capture: the board's microphone through the SoC's built-in codec, 32 kHz, 16-bit, mono.
- Analog gain: the codec's default (gain step 3, about 6 dB). `MicBoost = 1` adds about 9 dB; that's good for voice on the bench, but propeller noise clips with it. `MicGain` sets the codec gain step directly.
- Low-cut: first-order 80 Hz high-pass in integer maths. It removes the DC offset and the 13 Hz hum we measured on the mic input.
- Encoder: MP3 (MPEG-1 Layer III), 96 kbit/s CBR, mono, 32 kHz, 1152 samples (36 ms) per frame. It uses about 4% of the CPU. The shine encoder has two fixes from us (see `third_party/shine/SOURCE.txt`).
- Level: the encoder keeps 6 dB of headroom, so the loudest recorded level is -6 dBFS. Without that fix, loud prop noise came out as silence.
- In the TS file: stream type `0x03` (MPEG-1 audio), PID `0x101`, PES stream ID `0xC0`.
- Sync: every MP3 frame is stamped with the capture time of its first sample, from the same clock as the video. On bench clap tests audio and video matched within -11..+9 ms (less than one frame). Audio before the first video IDR frame is dropped, so the audio can start up to one MP3 frame (36 ms) after the picture.
- Flight levels (NTSC, MicBoost on): RMS -10 to -13 dBFS, but 16% of samples clipped (38% in the loudest seconds). That's why MicBoost is now off by default; the next flight checks the clipping with boost off (target: under 1%).

## Settings

dvrrec reads `config.txt` in the root of the SD card, the same file the stock firmware uses. It reads it every time it starts, so changes take effect at the next power-up.

- One setting per line, written `Key = value` (the spaces around `=` are optional).
- Key names are case-sensitive: `MaxBitRate`, not `maxbitrate`. `PAL` and `NTSC` must be in capitals too.
- The file only needs the lines you want to change: a missing key uses the value in the last column.
- Lines starting with `#`, and keys dvrrec doesn't know (like the stock `UserOsd`), are ignored.

| Key | Meaning | Example | When the key is missing |
|---|---|---|---|
| `MaxBitRate` | Video bitrate in Mbit/s, whole numbers (tested up to 20) | `MaxBitRate = 20` | 8 |
| `FileIntervalTime` | Clip length in minutes | `FileIntervalTime = 5` | 5 |
| `VideoFormat` | `PAL` or `NTSC` to force a standard; `AUTO` detects it | `VideoFormat = NTSC` | automatic |
| `Audio` | `1` records the microphone, `0` leaves the track out | `Audio = 0` | 1 (on) |
| `MicBoost` | `1` adds about 9 dB of analog gain: good for voice on the bench, but prop noise clips | `MicBoost = 1` | 0 (off) |
| `MicGain` | Codec mic gain step, 0-31; the codec starts at 3 (about 6 dB) | `MicGain = 2` | codec default |
| `AudioGain` | Extra software gain in dB, 0-30, applied after the low-cut | `AudioGain = 6` | 0 |
| `LoopRecording` | `1`: when the card is nearly full, delete the oldest `DVR_####.ts` clips to make room. `0`: stop and flash "card full" | `LoopRecording = 1` | 0 (off) |

Our card's `config.txt`, which keeps the stock keys so the stock recorder still works with it:

```
MaxBitRate = 20
FileIntervalTime = 5
UserOsd =
```

A file using every key:

```
# 20 Mbit/s, 5-minute clips, detect PAL/NTSC, microphone on at the codec's default gain
MaxBitRate = 20
FileIntervalTime = 5
VideoFormat = AUTO
Audio = 1
MicBoost = 0
# MicGain = 3
AudioGain = 0
LoopRecording = 1
```

For tests, a line of command-line options in `dvr/args` overrides `config.txt` (e.g. `-c 1` for 1-minute clips). The button and LED pins are in a separate file, `dvr/gpio.txt` (see [Button and LEDs](#button-and-leds)).

Loop recording only deletes clips named `DVR_####.ts`, oldest number first, and never the clip being recorded. It keeps room for one whole clip at the current bitrate (about 790 MB at 20 Mbit/s and 5 minutes). Without it, recording stops when 32 MB are left.

The space is checked every 5 s in a separate thread. On a FAT32 card, the first check after power-up makes Linux read the whole allocation table (about 5 s on our 234 GB card). Done in the writer, that froze the first clip for 5 s.

Automatic detection: the TP2850 only locks with the tables of the standard it receives. dvrrec first tries the last standard used (stored in `dvr/std`), then the other one. If there's no picture for 10 s (camera off or unplugged), it restarts and detects again.

## Button and LEDs

dvrrec takes the button and LED pins from `dvr/gpio.txt`. `sdcard/dvr/gpio.txt` has the pins of this board (the cooeye MINIDVR V1.2), so copying `sdcard/` is enough. Without that file, dvrrec records exactly as before, with no button or LEDs.

| | What it does |
|---|---|
| Short press (under 2 s) | Stops recording (the clip is closed cleanly), or starts a new clip |
| Long press (2 s or more) | Nothing. On the stock firmware a 5 s hold formats the card, so dvrrec ignores long presses. |
| Recording LED: one short flash every 5 s | Recording |
| Recording LED: steady on | Stopped with the button |
| Recording LED: two flashes every 5 s | Card full or not writable. A press tries again. |
| Recording LED: fast blink | Starting up, before the first picture (about 10 blinks after power-on) |
| Power LED (red) | On while dvrrec runs |

On this board the recording LED is the blue one. Its 5 s rhythm is deliberately slower than the stock firmware's (one flash about every 4.4 s), so you can tell at a glance which recorder is running.

If the camera is unplugged, the TP2850 keeps sending a black picture, so recording simply continues (black) and the picture comes back on its own when the camera is reconnected. Round 17 test: unplugged for about 65 s, all in the same clip, with no restart and no errors.

Recording always starts at power-on. A stop with the button stays in effect if dvrrec restarts itself (for example when the camera is unplugged), but not across a power cycle.

### Finding the pins: gpioprobe

This is only needed on a different board. The chip markings are gone, so the pins are found by watching what the stock firmware does with them. `dvr/gpioprobe` only reads registers; it never changes anything.

The probe is a developer tool, so it isn't in the release zip. Copy this repository's `sdcard/` folder to the card instead: it adds `dvr/gpioprobe` and `dvr/dev.sh`, which the boot hook runs when it is there.

1. Create an empty file named `probe_gpio` in the card root, next to `encrypt`.
2. Power on with the camera connected. For this one boot the **stock** recorder runs, and the probe watches every pin for 3 minutes.
3. When the blue LED starts flashing (the stock recorder is recording), wait about 10 s. Then tap the button **4 times, about 5 s apart**. **Short taps only: holding the button for 5 s makes the stock firmware format the card.**
4. Leave it on until 3 minutes after power-on, then power off. The `probe_gpio` file deletes itself.
5. Results are in `probe/`:
   - `17_gpio_summary.txt`: what was found.
   - `17_gpio_events.txt`: every change.
   - `17_gpio_boot.txt`: the registers before the stock recorder started.

   If the button and the recording LED were both found without doubt, the probe also writes `dvr/gpio.txt`, so the next power-on already uses them.

   An output that the stock firmware switches on once at start and then holds (like this board's red LED) is listed only as a commented-out guess. An enable line looks the same, so check it before using it.

`dvr/gpio.txt` can also be written by hand. Each pin is `GPIO<bank>_<pin>`, followed by its level when pressed (button) or lit (LEDs). `Mux` lines set a pin function register before the pins are used (only addresses inside the chip's three IO config blocks are accepted). These values are only an example:

```
Button = GPIO5_3 0
RecLed = GPIO6_2 1
PowerLed = GPIO6_1 1
Mux = 0x100c0010 0x1000
```

## Layout

| Path | What |
|---|---|
| `src/` | Our C sources |
| `scripts/setup_toolchain.sh` | Installs the Bootlin `armv5-eabi--uclibc` cross-compiler into WSL (`~/toolchains`) |
| `scripts/build.sh` | Builds into `build/` |
| `scripts/stage_sdcard.sh` | Copies the built programs and libraries into `sdcard/` |
| `scripts/make_release.py` | Packs the release zip for users into `dist/` |
| `sdcard/` | The SD card root for development: the release files plus the developer tools (`dvr/dev.sh`, `dvr/gpioprobe`, `dvr/imutest`) |
| `release/` | The user-facing `README.txt`, the default `config.txt` and the licence notice that go into the zip |
| `tests/` | Host-side tests, run by `scripts/host_test.sh` |
| `sysroot-board/lib/` | The board's own uClibc 0.9.33 libraries (from the flash dump), linked against so the binaries match the board |
| `vendor/lib/` | Goke MPP libraries from OpenIPC (`goke-osdrv-gk7205v200`, see `vendor/SOURCE.txt`) |
| `third_party/divinus/` | Reference HAL headers from OpenIPC Divinus (MIT), commit in `COMMIT.txt` |
| `third_party/openhisilicon/` | OpenIPC's open-source SDK headers (GPL-3.0), commit in `COMMIT.txt`; `dvrrec` is built on these, so the recorder is GPL-3.0 |
| `third_party/shine/` | shine MP3 encoder (LGPL-2.0) from OpenIPC Divinus, with our fixes in `SOURCE.txt` |

## Build (Windows, through WSL Ubuntu)

Run each from PowerShell, in the `dvr-recorder` folder:

```
wsl -d Ubuntu-20.04 -- bash scripts/setup_toolchain.sh
wsl -d Ubuntu-20.04 -- bash scripts/build.sh
wsl -d Ubuntu-20.04 -- bash scripts/stage_sdcard.sh
```

On Linux, run the same scripts with `bash` directly.

Check that everything will load on the board (Windows Python with `pyelftools`):

```
python scripts/check_links.py build/dvrtest
```

Then copy everything in `sdcard/` to the SD card root.

### Making a release

The version is `DVRREC_VERSION` in `src/dvrrec.c`. After `build.sh` and `stage_sdcard.sh`:

```
wsl -d Ubuntu-20.04 -- bash scripts/host_test.sh
python scripts/make_release.py
```

This writes `dist/open-dvr-<version>.zip`: the boot hook, `dvr/dvrrec`, `dvr/start.sh`, `dvr/gpio.txt`, the five libraries `dvrrec` loads, `README.txt`, `config.txt` and the licences. The script refuses a `dvrrec` that doesn't carry the version or that fails `check_links.py`. The developer tools and test results are left out.

## Using it

Users: extract the release zip into the top folder of the SD card (see its `README.txt`). Developers: copy `sdcard/` to the SD card root (`encrypt` and the `dvr/` folder). Either way, at power-on the board then records with `dvrrec` instead of the stock recorder.

- Settings: `config.txt`, see [Settings](#settings).
- Button and LEDs: see [Button and LEDs](#button-and-leds).
- Log: `dvr/log.txt`. Every 30 s it shows the audio peak level, whether it is recording, and the free space on the card. Extra options for testing: one line in `dvr/args` (e.g. `-c 1` for 1-minute clips).
- Back to the stock recorder: create an empty file named `stock` in the card root (or delete `encrypt`). If `dvrrec` fails three times right after starting, the stock recorder starts by itself.
- Stock firmware updates: per the manual, the stock firmware reflashes itself from `app.jffs2`, `rootfs.sqhfs`, `uboot.bin` and `uImage` on the card when the button is held for 5 s. Keep those files off the card unless that's what you want.

## Status

- [x] Toolchain and linking against the board's libraries (watch out: the toolchain has 64-bit `time_t`, the board 32-bit; see `src/compat.h`)
- [x] `dvrtest`: our programs run on the board and talk to the MPP library
- [x] TP2850 setup from our own code; field mode (MIPI page reg 0x23 = 0x00) gives one 288/240-line field per frame
- [x] The VI can't split 8-bit serial YUV, so `dvrrec` captures raw bytes and splits luma/chroma with NEON (3 ms per field)
- [x] Round 13: 50 fps PAL in colour, every frame a distinct field, no frame loss
- [x] Round 14: autostart at power-on, PAL auto-detected, 1-minute clips, 20 Mbit/s for 4.5 min with no failures; TS files decode with 4:3 shape at 50 fps; the clip cut by pulling power plays up to the cut
- [x] NTSC: auto-detection (tries the last standard, then the other), 59.94 fps at 960x480
- [x] Microphone: MP3 track in sync within one frame; shine fixed for 32 kHz framing and for loud input (see `third_party/shine/SOURCE.txt`); boost off by default after flight tests
- [ ] Flight check of audio clipping with MicBoost off
- [x] Round 17: `gpioprobe` found the button and the blue LED on its first run. With them, a tap closed `DVR_0020.ts` cleanly and the next tap started `DVR_0021.ts` (both decode with 0 errors, audio in sync)
- [x] Round 17, 3 more boots: red LED on, blue LED patterns as described, button stop/start in every boot; first clips start without the FAT32 freeze; camera unplug/replug records black, then the picture again, in the same clip
- [x] Release 0.9 prepared: `dist/open-dvr-0.9.zip` from `scripts/make_release.py`; the boot hook's developer extras moved to `dvr/dev.sh`; the launcher creates `DCIM` on a fresh card
- [ ] Loop recording and card-full handling: written, not yet tested on the board
- [ ] AHD 720p
- [ ] Video interference (hardware side first)

## Credits and licences

This recorder would not exist without the [OpenIPC](https://github.com/OpenIPC) project, which publishes open firmware, SDK headers and tools for HiSilicon and Goke camera chips like the one on this board. Thanks to the OpenIPC developers and contributors.

### Borrowed from OpenIPC

| What | Where it came from | Licence | What we use it for |
|---|---|---|---|
| openhisilicon SDK headers (`third_party/openhisilicon/include/`) | [OpenIPC/openhisilicon](https://github.com/OpenIPC/openhisilicon), commit `4fe4009a` | GPL-3.0 | The media API definitions (`GK_API_*` structs and constants) that `dvrrec` is compiled against. This is why `dvrrec` is GPL-3.0. |
| Goke MPP libraries (`vendor/lib/`: `libgk_api.so`, `libhi_mpi.so`, `libsecurec.so`, `libupvqe.so`, `libdnvqe.so`, `libvoice_engine.so`) | [OpenIPC/firmware](https://github.com/OpenIPC/firmware), package `goke-osdrv-gk7205v200` (`files/lib`), commit `09b0334d5aa4` | Goke's binary libraries, as redistributed by OpenIPC (not open source) | The chip's media library: video capture, scaling, H.264 encoding, audio input. Shipped unchanged in `sdcard/dvr/lib/`. |
| Divinus HAL headers (`third_party/divinus/hal/`) | [OpenIPC/divinus](https://github.com/OpenIPC/divinus), commit `10409f41031e` | MIT, Copyright (c) 2024 OpenIPC | Struct definitions for the first capture experiments (`dvrcap`), the order of the MIPI setup calls, and the audio input settings (10 ms blocks). `scripts/hdrtest.sh` checks that they match the openhisilicon headers. |
| shine MP3 encoder (`third_party/shine/`) | The copy bundled in OpenIPC Divinus (`src/lib/shine`); upstream [savonet/shine](https://github.com/savonet/shine) | LGPL-2.0 | The MP3 audio track. We changed two files; see `third_party/shine/SOURCE.txt`. |

The board itself is the same chip family that OpenIPC supports (GK7202V300, listed under "V4/Goke" in openhisilicon). That's how we found matching libraries and headers without touching the vendor's own recorder.

### Other sources and tools

| What | Licence | What we use it for |
|---|---|---|
| Rockchip techpoint TP9950 driver (Linux kernel) | GPL-2.0 | The TP2850 register write sequence in `src/tp2850.c` (the TP9950 is a register-compatible sibling). The register values come from our own dumps of this board. |
| [Bootlin toolchains](https://toolchains.bootlin.com) `armv5-eabi--uclibc--stable-2025.08-1` and `x86-64--glibc--stable-2025.08-1` | GPL and others (each toolchain lists its components) | Cross-compiling for the board, and host-side tests. Build tools only; nothing from them is shipped. |
| The board's own uClibc 0.9.33 (`sysroot-board/lib/`, from our flash dump) | LGPL-2.1 | Linking only, so our programs match the libraries already on the board. Not shipped. |
| [pyelftools](https://github.com/eliben/pyelftools) | Public domain (Unlicense) | `scripts/check_links.py` |

Our code (`src/`, `scripts/`, `tests/`, `sdcard/encrypt`, `sdcard/dvr/start.sh`, and `../dvr-probe/`) is released under GPL-3.0, full text in [`LICENSE`](../LICENSE). `dvrrec` has to be GPL-3.0 anyway, because it is compiled against the openhisilicon headers. Each third-party folder keeps its own licence: `third_party/openhisilicon/LICENSE`, `third_party/divinus/LICENSE` and `third_party/shine/COPYING`.
