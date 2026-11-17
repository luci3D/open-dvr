# Decoder board: preliminary BOM (JLCPCB)

Status: **preliminary, 2026-11-14.** Machine-readable copy: [bom.csv](bom.csv). Design notes: [README.md](README.md).

- **One supplier:** every part is from JLCPCB's assembly parts library (LCSC numbers), so JLC can build the whole board. The one exception is the decoder.
- **The decoder is consigned:** JLC lists the TP9950 (C2852602) but has **0 in stock**. Buy it elsewhere and ship it to JLC as a consigned part; they place it like any other part. Buy spares.
- **Stock and prices** are from JLC's parts search on 2026-11-14, single-unit price. Check again before ordering: stock changes, and JLC prices go down at higher quantities.
- **Basic vs extended:** JLC charges a one-time loading fee (about $3) for each different *extended* part type in an order; *basic* parts have none. Passives are all basic parts on purpose.

## Parts

| Ref | Qty | Part | LCSC | MPN | Type | Stock | Unit $ | Note |
|---|---|---|---|---|---|---|---|---|
| U1 | 1 | Video decoder, CVBS/AHD → MIPI CSI-2, QFN-40 5×5 | C2852602 | TP9950 | consigned | 0 | ~2.00 | Est. price. Ship to JLC |
| Y1 | 1 | Crystal 27 MHz, 20 pF, ±20 ppm, 3225 | C9008 | X322527MSB4SI | extended | 95 673 | 0.109 | Matches the datasheet's 20 pF load |
| C1–C2 | 2 | 33 pF C0G 0402 (crystal load) | C1562 | 0402CG330J500NT | basic | 1.86 M | 0.002 | Tune after first boards |
| U2 | 1 | LDO 3.3 V 500 mA, 50 µVrms, SOT-23-5 | C82942 | ME6211C33M5G-N | extended | 292 342 | 0.062 | AVDD33 and VIO |
| U3 | 1 | Buck 2 A, 1.5 MHz, 2.5–5.5 V in, SOT-23-5 | C141836 | TLV62569DBVR | extended | 639 784 | 0.068 | 1.2 V for AVDD12 and DVDD |
| L1 | 1 | 2.2 µH, 1.1 A, shielded, 2016 | C354533 | CKCS201610-2.2uH/M | extended | 49 936 | 0.041 | For U3 (load about 150 mA) |
| R1–R2 | 2 | 100 kΩ 1 % 0402 (buck feedback) | C25741 | 0402WGF1003TCE | basic | 10.4 M | 0.001 | 0.6 V × (1 + 100k/100k) = 1.2 V |
| U4 | 1 | LDO 1.8 V, SOT-23-5 (VIO) | C236671 | ME6211C18M5G-N | extended | 39 878 | 0.066 | VIO: the Luckfox camera I/O is 1.8 V |
| U5 | 1 | LDO 2.8 V, SOT-23-5 (mic supply) | C53099 | ME6211C28M5G-N | extended | 13 257 | 0.065 | |
| FB1–FB4 | 4 | Ferrite bead 120 Ω @ 100 MHz, 2 A, 0603 | C14709 | BLM18PG121SN1D | basic | 1.61 M | 0.014 | AVDD33, AVDD12, DVDD, camera 5 V |
| D1 | 1 | ESD diode 0.6 pF, DFN1006-2 (video input) | C316041 | ESD9L5.0ST5G-N | extended | 300 102 | 0.026 | Low capacitance so it doesn't load the video line |
| D2 | 1 | TVS 5 V, SOD-123FL (5 V input) | C193402 | SMF5.0A | extended | 456 037 | 0.028 | |
| Q1 | 1 | P-MOSFET, SOT-23 (reverse-polarity protection) | C15127 | AO3401A | basic | 1.54 M | 0.081 | |
| F1 | 1 | Polyfuse 500 mA hold, 6 V, 0805 (camera 5 V out) | C883108 | BSMD0805-050-6V | extended | 39 411 | 0.050 | |
| R3 | 1 | 75 Ω 1 % 0402 (video termination) | C25133 | 0402WGF750JTCE | extended | 1.04 M | 0.001 | Behind JP1, open by default |
| JP1 | 1 | Solder jumper (termination) | — | — | PCB | — | — | |
| MK1 | 1 | MEMS mic, analog **differential**, 135 dB SPL, LLGA-5 4×3 | C3171831 | IM73A135V01XTSA1 | extended | 1 471 | 3.349 | Bottom port: hole in the PCB. Most expensive part after the decoder |
| J1 | 1 | FPC 0.5 mm 20-pin, bottom contact, flip-top | C5362157 | 05A20L20P | extended | 4 083 | 0.071 | 20 pins confirmed from the Luckfox Pro/Max schematic. Pin order measured on the stock cable (see [README](README.md#host-connector-fpc)); check the insertion side on the drawing |
| J2 | 1 | SH 1.0 mm 3-pin, right angle (Video / 5 V / GND) | C3029342 | WAFER-SH1.0-3PWB | extended | 31 515 | 0.093 | Same connector type as the COOAI board, so its cables fit. Solder pads in parallel |
| C3–C14 | 12 | 100 nF X7R 0402 | C1525 | CL05B104KO5NNNC | basic | 38.1 M | 0.002 | 8 decoder pins, RSTB, 2 video coupling, mic |
| C15–C24 | 10 | 1 µF X5R 0402 | C52923 | CL05A105KA5NQNC | basic | 10.2 M | 0.006 | Analog rails, LDOs, 2 mic coupling |
| C25–C27 | 3 | 10 µF X5R 0603 | C19702 | CL10A106KP8NNNC | basic | 11.6 M | 0.023 | DVDD bulk, buck in/out |
| C28–C30 | 3 | 22 µF X5R 25 V 0805 | C45783 | CL21A226MAQNNNE | basic | 4.12 M | 0.108 | 5 V input, camera out, buck out |
| R4–R6 | 3 | 10 kΩ 1 % 0402 | C25744 | 0402WGF1002TCE | basic | 21.1 M | 0.001 | RSTB pull-up, Q1 gate, spare |
| R7–R8 | 2 | 4.7 kΩ 1 % 0402 (I2C pull-ups) | C25900 | 0402WGF4701TCE | basic | 14.1 M | 0.002 | **Not fitted:** the Luckfox has its own |
| R9–R12 | 4 | 0 Ω 0402 jumpers | C17168 | 0402WGF0000TCE | basic | 15.6 M | 0.001 | Only the VIO select is fitted; MCLK→XTI, MICBIAS and FPC 3.3 V are not |

## Cost

| | USD |
|---|---|
| Parts per board, decoder included (about 56 placed parts) | ≈ 6.70 |
| Of which the decoder (estimate) and the mic | ≈ 2.00 + 3.35 |
| Extended part loading fees, once per order (14 types incl. the consigned decoder, about $3 each) | ≈ 42 |

The loading fees dominate a small order. Ways to cut them (the 1.8 V LDO has to stay: the Luckfox camera I/O is 1.8 V):
- J2 and R3 are easy to hand-solder (−2 types).
- A basic-part 3.3 V LDO exists (XC6206P332MR, C5446), but it is noisier (40 dB PSRR) and only 200 mA; not worth it for the analog supply.

Not included: PCB (4 layers), assembly fee, the consignment shipping, and the FPC cable to the Luckfox (comes with the Luckfox camera; spares must be 0.5 mm, 20 conductors, **opposite-side** contacts).

## KiCad footprints

[`kicad/fetch_parts.ps1`](kicad/fetch_parts.ps1) downloads symbols, footprints and 3D models for every non-passive part above with `easyeda2kicad`, into `kicad/lib/` (`lcsc.kicad_sym`, `lcsc.pretty`). Passives use KiCad's own 0402/0603/0805 footprints. J2 uses KiCad's built-in `Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal` (EasyEDA refused that part).

The 3D models (`lcsc.3dshapes`, about 24 MB) are not committed; rerun the script to get them.

Checked so far:
- **TP9950 symbol:** all 40 pin names and numbers match the datasheet pin table.
- **Mic symbol:** pins are Output+, VDD, Output−, GND, GND, so the IM73A135 output is differential.

To check before layout:
- **Mic footprint:** EasyEDA's footprint is named after the IM69D130 (same 4×3 LLGA-5 package). Compare the pad layout and pin 1 with the IM73A135 datasheet.
- **Every footprint against its datasheet,** as usual for imported parts: pad size, pin 1 and the exposed pad of the QFN.
