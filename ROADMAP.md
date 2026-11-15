# open-dvr roadmap

Decided: the next generation is built on **Rockchip RV1103 + Techpoint TP2850**. The current Goke GK7202V300 recorder (`dvr-recorder/`) stays as-is for the stock boards people can already buy.

| Stage | What | Status |
|---|---|---|
| **1. COOAI board upcycle** | Our own recorder on the stock COOAI / BAYCKRC "MINI DVR" (Goke GK7202V300 + TP2850), side-loaded from the SD card | Done, wrapped up with release 0.9. Bug fixes only from here. |
| **2. Rockchip RV1103 + TP2850** | Our own hardware and one recorder codebase: everything from [Product line](#product-line) down, phases 0 to 5 | In progress: phase 0 |

## Stage 1 (done): the COOAI board upcycle

The project started by upcycling a DVR board anyone can buy: the COOAI / BAYCKRC "MINI DVR" (printed on the board: `cooeye MINIDVR V1.2`). The goal was to fix what its stock firmware gets wrong, without flashing anything.

What was wrong with the stock firmware:
- Its 50/60 fps files hold only 25/30 different pictures a second: it encodes one field of each frame twice and drops the other.
- The microphone track doesn't stay in sync with the video.

What stage 1 delivered (all in [`dvr-recorder/`](dvr-recorder/), packaged for users as the release 0.9 zip):
- **`dvrrec`**, our own recorder. It runs from the SD card through the stock firmware's boot hook, so nothing on the board is changed, and deleting the files brings the stock recorder back.
- Real 50 fps PAL and 59.94 fps NTSC (every field recorded), 960x576 / 960x480 shown as 4:3, H.264 (tested up to 20 Mbit/s), PAL/NTSC detected automatically.
- Power-cut-safe MPEG-TS clips, 5 minutes by default.
- The board's microphone as an MP3 track, in sync within one frame.
- Button (tap to stop or start) and both LEDs. Card-full stop and optional loop recording.
- Settings through the stock `config.txt`.

What it taught us, and what stage 2 reuses:
- The board's sanded-off chips, identified from software: GK7202V300 SoC, TP2850 decoder, W25Q128 flash ([details](dvr-recorder/README.md#masked-chips)).
- How the TP2850 behaves: it is register-compatible with the TP9950 (open Rockchip driver), and its field mode gives one field per MIPI frame. This is what makes real 50/60 fps possible, on any SoC.
- Code that carries over: the MPEG-TS writer, the MP3 path (shine with our two fixes), the PAL/NTSC detection, `config.txt` handling, and the rule that audio and video are stamped from one clock.
- Tools: the SD card probes ([`dvr-probe/`](dvr-probe/)), `gpioprobe` for finding buttons and LEDs, and host-side tests that run without a board.

Left open on this board (not planned, unless someone picks them up):
- AHD 720p cameras (use the stock recorder for those).
- Card-full stop and loop recording are written but were never tested on a full card.
- A flight check of the audio level with the microphone boost off.
- WiFi offload and an IMU. The stock kernel has no networking, USB host or SPI, so that would need a different kernel on the board. Those features move to stage 2; the notes are in [`dvr-recorder/docs/`](dvr-recorder/docs/).

Everything below is stage 2.

## Product line

| Product | Hardware | Who it's for |
|---|---|---|
| **DIY kit** | The user's own Rockchip dev board (Luckfox Pico RV1103/RV1106 first, Radxa Zero 3W later) + our TP2850 decoder board + our IMU/mic breakout | People who already own a board or want to build their own |
| **On-quad DVR, full size** | Our integrated RV1103 + TP2850 + IMU + mic board, 20 x 20 mm mounting | People who want the finished product |
| **On-quad DVR, whoop** | Same circuit, smallest possible board | Whoops and micros |
| **Goggle module (later)** | Radxa Zero 3W (RK3566) + TP2850: records the receiver's video, HDMI out, field-tools launcher | Replacement for stock goggle DVRs |

All of them run **one recorder codebase**.

## Requirements

- **The DVR never sits in the live video path.** On the quad, it connects in parallel to the video line as a listen-only, high-impedance tap. The FPV feed goes on exactly as it would without the DVR, so the DVR adds **zero latency**, and the pilot keeps their video even if the DVR crashes, reboots or loses power.
- **Default tap: camera → flight controller.** Most users want a clean recording without OSD elements, so the DVR taps the line between the camera's video out and the FC's video in. Users who want the OSD in the recording can tap the FC → VTX line instead, with the same parallel wiring.
- Because of that, the DVR's video input is **unterminated by default** (the FC, or the VTX for an OSD tap, already terminates the line). A solder jumper adds a 75 Ω termination only for bench use with a camera wired straight in.

## Software design

```
TP2850 ──MIPI CSI-2 (2 lanes)──► Rockchip capture (V4L2) ──► MPP H.264 encoder ──► MPEG-TS writer ──► SD card
IMU (LSM6DSOX) ──SPI/I2C──► FIFO reader ──► gyro log (.gcsv) ─┘   all timestamped on CLOCK_MONOTONIC
Mic ──► audio capture ──► MP3 (shine, reused) ─────────────────┘
```

- **Capture and encoding:** V4L2 capture + Rockchip MPP directly, not the RV1103-only `rkmpi`/rockit layer. MPP and V4L2 should exist on both the RV110x and the RK3566, so the same code covers the DIY, on-quad and goggle products. *To verify in phase 1.*
- **Decoder driver:** Rockchip's open TP9950 kernel driver. `dvr-probe` already found that the TP2850 CVBS tables match it. Field mode (MIPI page reg `0x23 = 0x00`, one field per frame) must be supported to keep real 50/60 fps.
- **Code to carry over from the Goke recorder:** `tsmux.c` (MPEG-TS writer), `mp3frame.c` + shine (MP3 with our fixes), TP2850 tables and standard auto-detection, `config.txt` handling, power-cut-safe flushing.
- **One clock for everything:** V4L2 buffer timestamps, audio blocks and IMU samples all use `CLOCK_MONOTONIC`, the same way audio is synced to video today. That's what makes the gyro data usable in Gyroflow.

## Phases

### Dev hardware
| Board | Chip | Use |
|---|---|---|
| Luckfox Pico Pro | RV1106G2, 128 MB RAM, SPI NAND, Ethernet, MIPI CSI 2-lane | Main development board (IMU, then decoder) |
| Luckfox Pico Mini B | RV1103, 64 MB RAM, 128 MB SPI NAND, MIPI CSI | Final-target check before the integrated PCB |
| Luckfox MIPI camera (SC3336) | | Proves the CSI connector, capture and encoder path before the TP2850 board exists |
| Adafruit LSM6DSOX | IMU | Phase 0 |

### Phase 0: IMU bring-up (now)
Hardware: Luckfox Pico Pro + Adafruit LSM6DSOX breakout.

The test program and the wiring are in [`imu-probe/`](imu-probe/): `imutest` covers steps 2 to 6 below.

1. **Wiring (I2C first, simplest):** breakout `VIN`→3.3 V, `GND`→GND, `SCL`/`SDA`→a Luckfox I2C bus. Take the bus and pin numbers from the Luckfox pinout for the exact board model, and enable it with `luckfox-config`. The Adafruit board has pull-ups and runs on 3.3 V.
2. **Check it's there:** `i2cdetect -y <bus>` should show `0x6A` (Adafruit default; `0x6B` if the address pad is changed). Read register `0x0F` (WHO_AM_I) with `i2cget -y <bus> 0x6a 0x0f`; it should return `0x6C`.
3. **Read data from user space** (through `/dev/i2c-N`, no kernel driver needed): set gyro and accel ODR (start at 416 Hz, aim for 833-1660 Hz), read the gyro + accel registers, print in deg/s and g. Check the axes by rotating the board by hand.
4. **Use the FIFO:** stream samples through the LSM6DSOX FIFO (with its timestamps) so no samples are lost when Linux is busy. Count samples per second against the set ODR.
5. **Switch to SPI:** move to SPI (`/dev/spidevX.Y`) for headroom at high rates; the breakout supports both. Compare the dropped-sample count with I2C.
6. **Log for Gyroflow:** write a `.gcsv` file (Gyroflow's generic gyro log format) with `CLOCK_MONOTONIC` timestamps. Film something by hand while logging, load the video and the log into Gyroflow, and check that stabilization lines up.
7. **Decide** on the production interface (SPI vs I2C), ODR, and where the IMU sits on the board (vibration, orientation).

Done when: a clean, gap-free gyro log at the target rate that Gyroflow accepts.

Optional later: the kernel's `st_lsm6dsx` IIO driver (supports the LSM6DSOX) with a device-tree entry, if user-space reading isn't good enough.

### Phase 1: decoder board + video on Luckfox
0. With the Luckfox camera: run the stock image's camera demo, then capture with `v4l2-ctl` and encode with MPP ourselves. Check that it works on both the Pro and the Mini B. Note the camera FPC pinout and power pins for the decoder board.
1. Design the small decoder board: decoder (TP9950, see [`decoder-board/`](decoder-board/)), crystal, regulators, video input pads (Video/GND), I2C, MIPI 2 lanes, FPC connector to match the Luckfox camera connector.
2. Device tree for the TP2850 on the Luckfox; get the TP9950 driver to probe the chip (ID `0x28 0x50` at `0x44`).
3. Capture PAL and NTSC with `v4l2-ctl`, confirm field mode (288/240-line fields at 50/59.94 fps).
4. Encode with MPP to H.264; check speed, CPU load, power and heat.

Done when: a clean 50 fps PAL / 59.94 fps NTSC H.264 file from the Luckfox.

#### Camera input (standard 3-wire analog FPV camera: V+, GND, Video)
- **Camera power:** not from the FPC's 3.3 V (that is for the TP2850; cameras draw about 100–300 mA and many need 5 V). Bench: the Luckfox VBUS 5 V pin, if the camera accepts 5 V. On a quad: 5 V pass-through, optionally VBAT, picked with a solder jumper. Filter it (ferrite bead or inductor + 10–47 µF) so regulator ripple doesn't show as rolling lines. Add a polyfuse, and reverse-polarity protection if VBAT can reach the pad.
- **Video input (CVBS, 1 Vpp into 75 Ω):** low-capacitance ESD TVS (<1 pF) and 75 Ω termination to GND, then an AC-coupling cap (about 0.1–1 µF) into the TP2850 VIN. Take the exact values from the COOAI board, which uses the same decoder.
- **Termination jumper:** open by default, because on the quad the DVR taps the camera → FC line in parallel (see [Requirements](#requirements)) and the FC already terminates it; double termination would halve the signal to about 0.5 Vpp. Close it only on the bench, with the camera wired straight in.
- **Layout:** short video trace over solid ground, away from the regulator's switch node and the MIPI pairs.
- **Ground:** camera GND and Luckfox GND must be connected (the most common reason for "no video" on the bench). Return the camera ground at the video input.
- **Connector:** solder pads for phase 1; footprints for a 3-pin 1.25 mm JST-GH-style header later (check the camera's connector). The OSD wire on 4-wire cameras isn't needed by the recorder.
- **Before connecting:** check the camera on goggles or a monitor first, and note whether it is set to PAL or NTSC.

### Phase 2: recorder port
- Port `dvrrec` to V4L2 + MPP, reusing the TS writer, MP3, config and auto-detection.
- Audio: mic capture (on-chip codec or I2S/PDM mic on the breakout, see open questions), synced like today.
- IMU logging next to each clip (`DVR_0001.ts` + `DVR_0001.gcsv`).
- Power-cut tests, long recordings at 20 Mbit/s, card-full behaviour, button and LEDs.

### Phase 3: IMU + mic breakout
- Small board: LSM6DSOX (or its successor) on SPI + MEMS mic, with a connector or pads that suit the Luckfox and the Radxa.
- Release Gerbers, BOM and the JLC assembly files for the DIY kit.

### Phase 4: integrated on-quad boards
- RV1103 + TP2850 + SPI NOR/NAND + microSD + IMU + mic + regulators on one board, starting from the open Luckfox Pico schematic.
- 4 layers; keep the analog video input away from the switching regulator; plan for vibration damping of the IMU.
- Full size first (easier), then whoop.

### Phase 5: Radxa Zero 3W
- Same recorder on the RK3566 (proves the single codebase).
- Goggle module: receiver video → TP2850 → record + HDMI out.
- Field-tools launcher: full-screen tiles (DVR, Betaflight, Blackbox, ELRS, EdgeTX, Files, Settings). Betaflight Configurator built from source and served on `localhost` so Web Serial works offline in Chromium.
- Input: wireless keyboard with trackpad first, then the radio as a USB joystick; head tracking with the IMU is a stretch goal.

## Open questions

- [ ] Do MPP and V4L2 give the same capture/encode path on the RV1103 and the RK3566? (phase 1)
- [ ] Does the Rockchip capture block take the TP2850 field-mode output as-is? (phase 1)
- [ ] Camera FPC pinouts: Luckfox vs Radxa. One decoder board with two connector options, or one per board?
- [x] Is the camera connector's 3.3 V enough for the TP2850, or does the decoder board need its own regulator? The decoder board takes its own 5 V (shared with the camera) and makes 3.3 V and 1.2 V locally ([decoder-board](decoder-board/README.md#power)).
- [ ] Camera I2C/reset voltage on the Luckfox connector: 1.8 V or 3.3 V? Sets the decoder's VDD_IO.
- [ ] Is the TP2850 pin-compatible with the TP9950? Continuity check on the COOAI board ([list](decoder-board/README.md#checks-on-the-cooai-board)).
- [ ] Which camera(s) for phase 1, and their voltage range? Sets the camera power section.- [ ] Mic: the RV1103's built-in codec (is the mic input broken out on the Luckfox boards?) or a digital I2S/PDM mic on the breakout?
- [ ] Storage on the integrated board: SPI NAND vs NOR, and how big?
- [ ] Goggles: which models have HDMI in? Power and heat budget of the Zero 3W in a goggle bay.
- [ ] Radxa Zero 3W USB: which port powers the board, and does a hub fit for keyboard + flight controller?

## Safety
Bench-test every new board on a current-limited supply with props off before any flight test.
