# Decoder board (phase 1)

A small board that turns an analog FPV camera signal (CVBS) into MIPI CSI-2 for a Luckfox Pico, and carries the DVR's microphone. It is the "TP2850 decoder board" from [phase 1 of the roadmap](../ROADMAP.md#phase-1-decoder-board--video-on-luckfox), and later part of the DIY kit.

Status: **design start, 2026-11-14.** Nothing is drawn yet. This file holds the pin map, the parts list and what still has to be checked before the schematic.

```
Camera / FC video line ──(parallel tap)──► input network ──► TP9950 ──MIPI 2 lanes + I2C──► FPC ──► Luckfox CSI
5 V pad (from Luckfox VBUS or the FC) ──► camera power out, 3.3 V LDO, 1.2 V buck
MEMS mic ──► MIC pads ──(3 short wires)──► Luckfox codec mic input
```

## Chip: TP9950 instead of TP2850

| | TP2850 (COOAI board) | TP9950 |
|---|---|---|
| Datasheet | Not found | Preliminary datasheet, rev A (2018-12-28) |
| Sourcing | No listing found anywhere | Listed in JLCPCB's assembly library (C2852602 and others), so JLC can place it |
| Package | QFN, about 10 pins per side in the photo, so probably QFN40 | EPQFN40, 5 x 5 mm, 0.4 mm pitch, exposed pad 3.7 mm |
| Linux driver | None | Rockchip's open TP9950 driver, the one the roadmap already plans to use |
| Registers | Our probes found the same CVBS tables as the TP9950 driver | |

**Pick: TP9950.** It is the chip the driver is written for, it has a datasheet, and it can be bought assembled. The TP2850 on the COOAI board is still useful as a working reference: same package size and same register tables, so it is most likely the same pinout. That needs a continuity check (see [Checks on the COOAI board](#checks-on-the-cooai-board)).

The datasheet is marked confidential, so it is **not** stored in this repo. Only the facts needed for the design are copied here.

## TP9950 pin map

Pin numbers from the TP9950 datasheet (top view, pin 1 = AVDD33, counter-clockwise). "Our net" is what each pin connects to on this board.

| Pin | Name | Type | Our net / connection |
|---|---|---|---|
| 1 | AVDD33 | Power | `3V3A`: 3.3 V through a ferrite bead, 100 nF + 1 µF at the pin |
| 2 | AVDD12 | Power | `1V2A`: 1.2 V through a ferrite bead, 100 nF + 1 µF at the pin |
| 3, 4 | PTZ1, PTZ2 | Out | Not connected (PTZ data over coax, not used) |
| 5, 6 | DIAG1, DIAG2 | In | Not connected (input fault detection, not used). Check the datasheet's "use as directed" note before layout |
| 7 | VIN1 / VIN1P | Analog in | Video input, AC-coupled 0.1 µF (see input network) |
| 8 | VIN2 / VIN1N | Analog in | Pseudo-differential return: 0.1 µF to the video ground pad (to confirm, see below) |
| 9, 10 | VIN3-4 / VIN2P/N | Analog in | Not connected |
| 11, 28, 40 | DVDD | Power | `1V2D`: 1.2 V core, 100 nF at each pin + 10 µF bulk |
| 12, 13 | VS, HS | Out | Not connected (parallel output not used); test pads optional |
| 14, 27, 35 | VDD_IO | Power | `VIO`: 1.8 V from U4 (the Luckfox camera I/O is 1.8 V); 0 Ω option for 3.3 V, not fitted |
| 15 | RSTB | In, low active | Host reset line from the FPC (pin 16), plus 10 kΩ pull-up to `VIO` and 100 nF to GND (power-on reset if the host leaves it floating) |
| 16 | SCL | In | Host I2C SCL from the FPC (pull-ups on the Luckfox; footprints for our own, not fitted) |
| 17 | SDA | Bidir | Host I2C SDA, same as SCL |
| 18 | IRQ | Out | Test pad (not needed by the driver) |
| 19, 20, 29-34 | VD1_0-7 | I/O | Not connected (parallel BT.656 output, not used) |
| 21 | CKN | Out | MIPI clock − |
| 22 | CKP / CLKO | Out | MIPI clock + |
| 23, 24 | D0N, D0P | Out | MIPI lane 0 −/+ |
| 25, 26 | D1N, D1P | Out | MIPI lane 1 −/+ |
| 36 | XTI | In | 27 MHz crystal, or a 27 MHz clock in (see clock) |
| 37 | XTO | Out | 27 MHz crystal |
| 38 | SAD | In | GND → I2C address `0x44` (VIO → `0x45`). `0x44` is what the COOAI board and our driver tables use |
| 39 | TEST | In | GND (to confirm on the COOAI board) |
| EP | GND | | Ground, with a via array under the pad |

## Power

From the datasheet:

| Rail | Range | Current (typ.) |
|---|---|---|
| AVDD33 | 3.15–3.45 V | 24 mA |
| AVDD12 | 1.1–1.3 V | 60 mA |
| DVDD | 1.1–1.3 V | 84 mA at 720p and below (140 mA at 1080p30) |
| VDD_IO | 3.15–3.45 V or 1.7–2.0 V | 9 mA |

So the chip needs about **35 mA at 3.3 V and 145 mA at 1.2 V**, roughly 0.3 W. The power table in the datasheet also has a limit on the difference between VDD_IO and AVDD that didn't extract cleanly; read it on the PDF before choosing 1.8 V IO.

**Plan:** power the board from its own **5 V pad**, the same one that feeds the camera, instead of the Luckfox connector's 3.3 V. This answers the roadmap's open question: the board doesn't depend on how much the Luckfox's 3.3 V can supply. On the bench, the 5 V comes from the Luckfox VBUS pin; on a quad, from the FC's 5 V pad.

| Rail | Part type | Why |
|---|---|---|
| 3.3 V (`3V3A`, and `VIO` if 3.3 V) | Low-noise LDO, 5 V → 3.3 V, 150–300 mA, SOT-23-5 | Only about 35 mA, so about 0.06 W lost; quiet supply for the analog front end |
| 1.2 V (`1V2A`, `1V2D`) | Small buck, 5 V → 1.2 V, 2.5–5.5 V input (SY8089 / TLV62569 class), 2.2 µH | An LDO from 3.3 V would burn about 0.3 W at 145 mA. Ferrite + caps split it into analog and digital 1.2 V |
| 1.8 V (`VIO`) | Tiny LDO, 3.3 V → 1.8 V | The Luckfox camera I2C and reset are 1.8 V (see below) |
| Camera power out | 5 V pass-through, ferrite + 22 µF, polyfuse | See [Camera input in the roadmap](../ROADMAP.md#camera-input-standard-3-wire-analog-fpv-camera-v-gnd-video) |

Input protection on the 5 V pad: 5 V TVS, bulk 10–22 µF, and reverse-polarity protection. The COOAI board has an SMA-package diode marked `BV` next to its connector, most likely for the same job.

## I/O voltage

**The Luckfox camera I/O is 1.8 V** (Luckfox Pico Pro/Max schematic): the SoC's camera pins are on the `VCCIO7` domain, powered from `VCC_1V8`, and the camera I2C pull-ups (R28, R29, 4.7 kΩ) go to `VCC_1V8`.

So `VIO` = **1.8 V**: U4 (1.8 V LDO) is fitted, and the TP9950's I2C and reset talk to the Luckfox directly, with no level shifter. The 0 Ω option to run `VIO` from 3.3 V stays on the board, not fitted.

Still to read on the datasheet PDF: the power table's limit on the difference between VDD_IO and AVDD, which didn't extract cleanly. The datasheet does list 1.8 V as a supported VDD_IO.

## Clock

- **27 MHz fundamental crystal**, ±50 ppm, CL 20 pF, ESR about 80 Ω (datasheet values). 3225 package.
- Load caps: start at 33 pF each (2 × (20 pF − about 4 pF stray)); copy the COOAI board's values if they can be measured.
- The COOAI board uses a 27 MHz crystal next to its decoder (the can marked `R27.0` in the photo).
- **Optional:** XTI also accepts a 27 MHz clock in. Add a 0 Ω (not fitted) from the FPC's MCLK pin to XTI, in case we ever want to drop the crystal and have the Luckfox drive it. The crystal is the default because it doesn't depend on the host's clock setup.

## Video input network

```
Video pad ──┬── ESD diode (<1 pF) ── GND
            ├── 75 Ω ── JP1 (open by default) ── GND
            └── 0.1 µF ── VIN1P (pin 7)

Video GND pad ── 0.1 µF ── VIN1N (pin 8)     (pseudo-differential, to confirm)
```

- The datasheet asks for the input to be AC-coupled with **0.1 µF**.
- **JP1 is open by default:** on the quad the DVR is a parallel tap, and the FC (or VTX) already terminates the line (see [Requirements](../ROADMAP.md#requirements)). Close it on the bench when the camera is wired straight in.
- **Pseudo-differential input:** pins 7 and 8 can be used as a pair (VIN1P/VIN1N). Taking the negative input from the camera's ground pad rejects ground noise between the quad and the DVR. This is how Techpoint chips are usually wired, but the datasheet section on it didn't extract cleanly; check it on the PDF and on the COOAI board before relying on it. Fallback: VIN1N to GND through 0.1 µF at the chip.

## Host connector (FPC)

From the Luckfox Pico Pro/Max schematic, connector `H1` (`CAM`), **0.5 mm, 20 pins**:

| Pin | Luckfox net | Our board |
|---|---|---|
| 1 | GND | GND |
| 2 | `MIPI/LVDS_MCLK0` (SoC `VI_CIF_CLKO_M0` / GPIO3_C4, through 22 Ω) | Optional 0 Ω to XTI, not fitted |
| 3 | GND | GND |
| 4 | `MIPI/LVDS_D0_P` | D0P (TP9950 pin 24) |
| 5 | `MIPI/LVDS_D0_N` | D0N (pin 23) |
| 6 | GND | GND |
| 7 | `MIPI/LVDS_CLK_P` | CKP (pin 22) |
| 8 | `MIPI/LVDS_CLK_N` | CKN (pin 21) |
| 9 | GND | GND |
| 10 | `MIPI/LVDS_D1_P` | D1P (pin 26) |
| 11 | `MIPI/LVDS_D1_N` | D1N (pin 25) |
| 12 | GND | GND |
| 13 | `MIPI_I2C_SCL` (4.7 kΩ to 1.8 V on the Luckfox) | SCL (pin 16) |
| 14 | `MIPI_I2C_SDA` (4.7 kΩ to 1.8 V on the Luckfox) | SDA (pin 17) |
| 15 | GND | GND |
| 16 | `MIPI/LVDS_RST` (SoC GPIO3_C5) | RSTB (pin 15) |
| 17, 18 | GND | GND |
| 19, 20 | `VCC_3V3` | Not used for power; optional 0 Ω to our 3.3 V rail, not fitted |

**Cable** (measured on the bench, 2026-11-17, with the cable plugged into the Luckfox and a meter on the 3.3 V header pin):
- The stock Luckfox camera cable (0.5 mm, 20 conductors) has its contacts on **opposite faces** at its two ends.
- At the free end, looking at the contacts with the cable's tip pointing toward you (the cable running away from you to the Luckfox): **pins 19–20 (3.3 V) are on the right**, so pin 1 is on the left.

**What that means for J1** (bottom-contact connector on the top side of our board): the cable's contacts face down into it. Seen from above, with the cable coming in from the top edge of the board, **Luckfox pin 1 lands on the right** and pin 20 on the left.

**Footprint mapping:** in our J1 footprint, pad 1 is at the left end of the pad row (the triangle marks it). Which J1 pad gets which Luckfox pin depends on which side of the connector the cable goes in:
- cable enters on the side **opposite** the pads: rotate the footprint so the entry faces the cable, pad 1 ends up on the right → **straight** (J1 pad n = Luckfox pin n);
- cable enters on the **pad side**: pad 1 stays on the left → **reversed** (J1 pad n = Luckfox pin 21 − n).

Check the insertion side on the 05A20L20P drawing before naming the J1 nets in the schematic.

**Spare cables** must also be **opposite-side** (sometimes sold as "Type B" or "reverse"), 0.5 mm, 20 conductors. A same-side cable mirrors the pin order and won't work with this layout.

Still to check: the Mini B uses the same 20-pin pinout (the SC3336 camera fits both, which suggests yes).

## Microphone

The mic goes on this board because it is the board that sits on the quad next to the video wiring, and it already has clean local supplies. That saves a separate mic board for the DIY kit.

**Requirement** (from the COOAI board, see [bom-prelim.md](../dvr-recorder/docs/bom-prelim.md#microphone)): prop noise next to the motors is very loud; the COOAI mic input clipped up to 38 % of the time in flight. The mic must not overload before the SoC's input does.

**Where the audio goes:** the RV1106/RV1103 has a built-in audio codec. On the Luckfox Pico Pro/Max its pins come out to **test pads**:

| Pad | Net | SoC pin |
|---|---|---|
| P4 | `MIC0_P` | CODEC_MIC0P (34) |
| P5 | `MIC0_N` | CODEC_MIC0N (33) |
| P7 | `MIC1_P` | CODEC_MIC1P (36) |
| P8 | `MIC1_N` | CODEC_MIC1N (35) |
| P6 | `LINEOUT` | CODEC_LINEOUT (29) |
| P9 | `BIAS` (mic bias, from `VCC1V8_MICBIAS`) | CODEC_MICBIAS (32) |

**Where they are on the board** (confirmed by continuity to the SoC pins, 2026-11-17): the row of **four round gold pads** on the top side, next to the RTC battery connector, just above header pins 21–26. Numbered left to right with the camera connector on the left:

| Pad on the board | SoC pin | Net | Schematic pad |
|---|---|---|---|
| 1 | 29 | `LINEOUT` | P6 |
| 2 | 32 | Mic bias (1.8 V) | P9 |
| 3 | 34 | **`MIC0_P`** | P4 |
| 4 | 33 | **`MIC0_N`** | P5 |

MIC1 (P7, P8) isn't on these pads, and we don't need it.

**Wiring:** the camera FPC carries no audio, so the mic connects with **3 short twisted wires**: MIC+ → pad 3, MIC− → pad 4, GND → header pin 23 (the square pad just below; GND in the Pico pinout, check it beeps to GND). Check the Mini B too.

**Software:** the Luckfox wiki says the Pro/Max "does not expose" the audio pins, and its audio tutorial is for the Ultra W only. The stock image probably has the codec turned off, so expect to enable it in the device tree of our build.

**Plan A, analog mic (default):**
- **Mic:** Infineon IM73A135 analog MEMS, 135 dB SPL overload point, IP57, bottom port, as already chosen in the BOM notes.
- **Supply:** 2.8 V LDO from the board's 3.3 V (the mic's supply range is 1.52–3 V). Optional 0 Ω to take the Luckfox's mic bias (pad 2, 1.8 V) instead, if it can supply the mic's current; that would need a 4th wire.
- **Output:** the IM73A135 has a **differential** output (Output+ and Output−, from its LCSC symbol). Each side goes through a coupling cap to the codec's MICP and MICN, so the pair rejects noise picked up on the wires.
- **Pads:** MIC+, MIC−, AGND at the board edge, next to the camera pads but away from the 1.2 V buck. Twist the three wires to the Luckfox.
- **Port hole:** the PCB needs a hole under the mic, covered with acoustic mesh or foam against wind and prop wash.

**Plan B, digital I2S mic:** only needed if a board doesn't bring out the codec mic pins (the Pro/Max does). The RV1106 has an I2S0 port (`I2S0_SCLK`, `I2S0_LRCK`, `I2S0_SDI0` on GPIO2_A0, A1 and A5 in the Ultra W schematic). An I2S MEMS mic needs 3 wires plus power, and its audio arrives already digital, so no analog noise pickup on the wires. The catch is overload: common I2S mics are around 120 dB SPL, below the IM73A135. Keep a footprint for one only if plan A is ruled out.

## Layout

- **4 layers** (JLC 4-layer is cheap; the integrated board will be 4 layers anyway): signals / GND / power / signals.
- MIPI pairs: 100 Ω differential, matched length within each pair and between pairs, over solid ground, no vias if possible. The data rate is low for CVBS, but keep it clean.
- Video input: short trace from the pad to pin 7, over solid ground, away from the 1.2 V buck's switch node and inductor, and away from the MIPI pairs.
- Mic: on the opposite edge from the buck, with its port hole clear of components; mic traces short and over ground.
- The 1.2 V buck on the far side of the board from the video input.
- Decoupling caps on the same side as the chip, at each power pin.
- Exposed pad: via array to the ground plane (also the chip's heat path).
- Target size: about 20 x 20 mm, with the FPC on one edge and the camera/power pads on the opposite edge.

## Parts list

The preliminary BOM, with JLCPCB part numbers, stock and prices, is in [bom.md](bom.md) ([bom.csv](bom.csv)). Every part comes from JLC except the decoder, which is bought separately and shipped to JLC.

## Checks on the COOAI board

These confirm that the TP2850 is wired like a TP9950 and give us values that worked. A multimeter (continuity and voltage) is enough for all of them.

1. **Find pin 1 of the decoder** (dot or chamfer; a sharper photo of the chip alone would help).
2. **Continuity:** crystal (`R27.0` can) → pins 36 and 37; I2C pull-ups → pins 16 and 17; the MIPI traces to the SoC → pins 21–26. If these match, the TP2850 has the TP9950 pinout.
3. **Voltages with the board running:** pins 1 (expect 3.3 V), 2 and 11/28/40 (expect 1.2 V), 14/27/35 (VDD_IO: 3.3 or 1.8 V?), 38 (SAD, expect 0 V), 39 (TEST).
4. **Input network:** what sits between the `Video` pad and pin 7 (cap value; any resistor to GND), and whether pin 8 goes to ground through a cap.
5. **Where the 1.2 V comes from:** LDO or buck, and from which rail.
6. **Crystal load caps:** the two small caps at the crystal (values can only be read with an LCR meter once removed, so this one is optional).

## What the photo shows

From the close-up of the COOAI board (`cooeye MINI DVR V1.2 23-3-15`):
- **Left, large chip:** the GK7202V300 SoC, with a 24 MHz crystal (`R24.0`) below it.
- **Right, QFN with about 10 pins per side:** the TP2850 decoder, with its 27 MHz crystal (`R27.0`) just below it.
- **Right edge:** the `GND`, `VCC` and `Video` pads, and the SH 1.0 mm connector below them.
- **Next to the connector:** an SMA diode marked `BV` (probably input protection) and a shielded inductor (a buck regulator).
- **Between the decoder and the pads:** a column of small passives, most likely the video input network and the decoder's decoupling. A sharper, straight-on photo of that area would let us read it.
