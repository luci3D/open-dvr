# Preliminary BOM: open-dvr analog board

Status: **preliminary, 2026-10-26.**
- Prices are single-unit or small-quantity consumer list prices (LCSC, Digi-Key, AliExpress/Alibaba) for a build of 2–5 boards, in USD.
- **Est.** = no public listing found; it's a guess from similar parts. Check every line before ordering.
- The Goke and Techpoint chips are not sold by Western distributors. They come from Chinese brokers, Taobao or Alibaba.

Machine-readable copy: [bom-prelim.csv](bom-prelim.csv).

## Board concept

- **Analog in, parallel tap.** Camera video passes straight through to the VTX; the DVR only listens, so it adds no latency.
- **Power:** 2–6S battery input.
- **Storage:** microSD card.
- **Sensors:** IMU for Gyroflow, plus a microphone.
- **WiFi for offload**, in one of two versions:
  - **A:** RTL8188FTV chip on the board, over USB, 2.4 GHz only.
  - **B:** AIC8800D80 module, over SDIO, 2.4 + 5 GHz WiFi 6. It uses the SoC's second SD controller (mmc1), which is already enabled and empty on the current board.

## Common parts (both versions)

| # | Part | Example part number | Qty | Unit $ | Line $ | Source / note |
|---|---|---|---|---|---|---|
| 1 | SoC: Cortex-A7 900 MHz, H.264/H.265 encoder, 64 MB DDR2 built in, QFN-88 9×9 mm | Goke GK7202V300 | 1 | 3.00 | 3.00 | Alibaba (one seller, $3 or ¥20.28). Rails: 0.9 V core, 3.3 V IO, 1.8 V DRAM |
| 2 | Analog video decoder, MIPI out | Techpoint TP2850 | 1 | 2.00 | 2.00 | **Est.** No public listing found; a related TP2830 is listed at about $1–2 |
| 3 | SPI NOR flash, 16 MB | Winbond W25Q128JVSIQ | 1 | 0.80 | 0.80 | LCSC, typical |
| 4 | IMU | ST LSM6DSOTR | 1 | 3.82 | 3.82 | Digi-Key, qty 1 (LCSC about $4.20) |
| 4b | IMU, budget alternative | ST LSM6DS3TR-C | (1) | ~1.00 | — | Est., LCSC. Pin-compatible (check before layout); imutest supports it in polling mode |
| 5 | Crystal 24 MHz (SoC) | 3225 SMD | 1 | 0.30 | 0.30 | LCSC, typical |
| 6 | Crystal 27 MHz (decoder) | 3225 SMD | 1 | 0.30 | 0.30 | Check the frequency in the TP2850 datasheet |
| 7 | Wide-input buck, 2–6S to 3.3 V, about 1.5 A | TI LMR51420 / TPS54202 class | 1 | 0.90 | 0.90 | Est. Choose a ≥36 V-rated part for 6S spikes |
| 8 | Core buck 3.3 → 0.9 V | SY8089 / TLV62569 class | 1 | 0.30 | 0.30 | Est. |
| 9 | 1.8 V rail (DRAM, decoder IO) | Small buck or LDO | 1 | 0.25 | 0.25 | Est. Decoder may also need its own core rail (check its datasheet) |
| 10 | Inductors for the buck converters | 2.2–10 µH, 2520/3015 | 3 | 0.15 | 0.45 | Est. |
| 11 | microSD socket, push-push | Generic | 1 | 0.30 | 0.30 | LCSC, typical |
| 12 | Microphone | Analog MEMS or 4 mm electret | 1 | 0.40 | 0.40 | Est. |
| 13 | Power/video connector | JST-SH 1.0 mm 4–6 pin, plus solder pads | 1 | 0.40 | 0.40 | Est. |
| 14 | Button + status LED | Tactile 3×4 mm + 0603 LED | 1 | 0.20 | 0.20 | Est. |
| 15 | ESD/TVS protection (power input, video lines) | SMBJ/ESD diodes | 2 | 0.20 | 0.40 | Est. |
| 16 | Passives (about 100 resistors and capacitors) | 0402/0603 | ~100 | — | 1.00 | Est. Buying for 2–5 boards means minimum-quantity strips, so the real spend is a few dollars more |
| | **Common subtotal** | | | | **≈ 14.80** | |

## Version A: RTL8188FTV on the board (USB, 2.4 GHz)

| # | Part | Example part number | Qty | Unit $ | Line $ | Source / note |
|---|---|---|---|---|---|---|
| A1 | WiFi chip 802.11n 1T1R, USB, QFN-24 | Realtek RTL8188FTV-VC-CG | 1 | 1.22 | 1.22 | LCSC C2760855 (Oct 2024 snapshot) |
| A2 | Crystal 40 MHz | 3225 SMD | 1 | 0.30 | 0.30 | Est. |
| A3 | RF matching parts | 0402 L/C | ~4 | 0.03 | 0.10 | Est. |
| A4 | U.FL (IPEX) antenna connector | Generic | 1 | 0.20 | 0.20 | Est. |
| A5 | 2.4 GHz antenna, U.FL lead | FPC or dipole | 1 | 1.00 | 1.00 | Est., AliExpress |
| | **Version A subtotal** | | | | **≈ 2.80** | |
| | **Version A board total (components)** | | | | **≈ 17.60** | |

## Version B: AIC8800D80 module (SDIO, 2.4 + 5 GHz WiFi 6, Bluetooth 5.4)

| # | Part | Example part number | Qty | Unit $ | Line $ | Source / note |
|---|---|---|---|---|---|---|
| B1 | WiFi 6 module, 1T1R, SDIO | AIC8800D80 module | 1 | 3.43 | 3.43 | AliExpress listing; Alibaba $1.75–3 at low MOQ |
| B2 | U.FL connector (if the module only has an antenna pad) | Generic | 1 | 0.20 | 0.20 | Est. |
| B3 | Dual-band 2.4/5 GHz antenna, U.FL lead | FPC | 1 | 1.50 | 1.50 | Est., AliExpress |
| | **Version B subtotal** | | | | **≈ 5.10** | |
| | **Version B board total (components)** | | | | **≈ 19.95** | |

## PCB and assembly for a 5-board run (estimates)

| Item | Est. $ for the run | Note |
|---|---|---|
| 4-layer PCB, about 30×30 mm, 5 pcs, impedance-controlled stackup | 8–15 | JLCPCB-style prototype pricing |
| Assembly setup + stencil | 10–35 | Economic vs standard assembly |
| "Extended" part fees (parts not on the assembler's shelf), about 10–14 types | 30–45 | About $3 per part type |
| Consigned parts (SoC, decoder, maybe the WiFi module): shipped to the assembler or bought through their sourcing service | 0–20 + shipping | Not in JLC/LCSC stock |
| Shipping and import | 15–35 | |
| **Run total excluding components** | **≈ 65–150** | So about **$13–30 per board** for 5 boards |

Rough cost per assembled board for 5 units: **Version A ≈ $35–50, Version B ≈ $38–52.** That's the components above with real minimum-order quantities, plus the fees. Fees dominate at this quantity; at 100+ boards the component totals above become the main cost.

## PCB layers

Both versions: **4 layers.**
- Stackup: signal/components, solid ground, power, signal.
- Typical board: 30×30 mm or 25.5×25.5 mm, 1.0–1.6 mm thick.

Two layers isn't realistic for either version:
- The SoC is a 0.4 mm-pitch QFN-88 that needs dense fan-out.
- The TP2850 → SoC MIPI link needs 100 Ω differential pairs over solid ground.
- The WiFi antenna feed needs a 50 Ω line.

Six layers is only worth it if the board must shrink to about 20×20 mm with parts on one side.

| | A: RTL8188FTV on the board | B: AIC8800D80 module |
|---|---|---|
| Layers | 4 | 4 |
| Controlled-impedance lines | 100 Ω MIPI pairs, 90 Ω USB pair, 50 Ω RF line | 100 Ω MIPI pairs, 50 Ω line from module pad to U.FL; SDIO is 6 short single-ended lines |
| RF design work | Matching network and antenna keep-out on our board; tuning ideally needs a VNA | Done inside the module |
| Board area for WiFi | About 8×8 mm (chip, crystal, matching) | Module footprint (about 12×12 mm, check the module drawing) |
| Speed (realistic file transfer) | About 2–5 MB/s (2.4 GHz 802.11n) | Est. about 8–15 MB/s on 5 GHz; limited by the SDIO bus and CPU |
| 5-minute 20 Mbit/s clip (about 750 MB) | 2.5–6 min | About 1–1.5 min |
| SoC interface | USB 2.0 (DWC3 controller; OpenIPC enables it). Check that the QFN-88 pins it out | SDIO on mmc1 (enabled on the current board). Check the pin assignment |
| OpenIPC driver | rtl8188fu-openipc | aic8800-openipc (confirm it covers the SDIO D80 variant) |
| Certification | The whole board is a radio transmitter that needs testing | Module approval covers the radio **only if** the module is certified (FCC/CE); many cheap AIC modules aren't |
| Version cost delta | Baseline | About +$2.30 |

## Recommendation

**Version B (AIC8800D80 module):**
- It transfers 3–4× faster, which is the part users feel most.
- It avoids RF layout and tuning.
- It uses the free SDIO port instead of relying on the SoC's USB pins.
- It costs about $2.30 more.

Before committing:
1. Confirm OpenIPC's aic8800 driver supports the SDIO D80 module.
2. Find a module seller with a datasheet and, if you plan to sell boards, FCC/CE modular approval.

## Biggest risks (not price)

- **Documentation for the Goke SoC and TP2850:** pinout, power sequencing and reference schematic are normally under NDA. Sources to work from: the current board (reverse-engineer it), Hi3518EV300 reference designs (same chip family), and the OpenIPC community.
- **Small-quantity sourcing of GK7202V300 and TP2850:** a broker or Taobao seller is needed. Buy spares.
- **Video input termination:** the DVR must tap the line at high impedance. A second 75 Ω termination next to the VTX's halves the signal. Check how the current board's input is built.
- **TP2850 needs a different price check:** none of the searches found a listing.
