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

dvrrec reads the stock `config.txt` in the card root. Keys it doesn't know (like `UserOsd`) are ignored.

| Key | Meaning | When the key is missing |
|---|---|---|
| `MaxBitRate` | Video bitrate in Mbit/s | 8 |
| `FileIntervalTime` | Clip length in minutes | 5 |
| `VideoFormat` | `PAL` or `NTSC`; anything else means automatic | automatic |
| `Audio` | `0` turns the microphone track off | on |
| `MicBoost` | `1` adds about 9 dB of analog gain | off |
| `MicGain` | Codec mic gain step | codec default (step 3) |
| `AudioGain` | Software gain in dB, after the low-cut | 0 |

Automatic detection: the TP2850 only locks with the tables of the standard it receives. dvrrec first tries the last standard used (stored in `dvr/std`), then the other one. If there's no picture for 10 s (camera off or unplugged), it restarts and detects again.

## Layout

| Path | What |
|---|---|
| `src/` | Our C sources |
| `scripts/setup_toolchain.sh` | Installs the Bootlin `armv5-eabi--uclibc` cross-compiler into WSL (`~/toolchains`) |
| `scripts/build.sh` | Builds into `build/` |
| `scripts/stage_sdcard.sh` | Copies the built programs and libraries into `sdcard/` |
| `sdcard/` | Exactly what goes on the SD card root |
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

## Using it

Copy `sdcard/` to the SD card root (`encrypt` and the `dvr/` folder). At power-on the board then records with `dvrrec` instead of the stock recorder.

- Settings: `config.txt`, see [Settings](#settings).
- Log: `dvr/log.txt`. It also prints the audio peak level every 30 s. Extra options for testing: one line in `dvr/args` (e.g. `-c 1` for 1-minute clips).
- Back to the stock recorder: create an empty file named `stock` in the card root (or delete `encrypt`). If `dvrrec` fails three times right after starting, the stock recorder starts by itself.
- Not done yet in dvrrec: the button (stock: short press starts/stops, a 5 s hold formats the card) and the LEDs (stock: blue flashes once every 3-5 s while recording, twice when the card is full, three times when it isn't formatted).
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
- [ ] Start/stop button and LEDs, loop recording when the card is full, AHD 720p
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
