# JLCPCB order files (decoder board v0.2)

| File | Upload it at |
|---|---|
| `open-dvr-decoder-v0.2-gerbers.zip` | The PCB quote page: 4 copper layers, masks, silkscreens, paste, outline, and the PTH/NPTH drill files |
| `open-dvr-decoder-v0.2-bom.csv` | PCB Assembly, BOM step: 22 lines, 55 parts, matched by LCSC number |
| `open-dvr-decoder-v0.2-cpl.csv` | PCB Assembly, CPL step: one placement per BOM part, all on the top side |

They are generated from the board. After any change in KiCad, make them again:

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" decoder-board\kicad\tools\jlc_export.py
```

Left out of the BOM and CPL:
- **Not fitted (DNP):** R10, R11, R12 and R15.
- **Not assembled:** J3 (you solder the wires), JP1 (solder jumper), H1 and H2 (holes) and LOGO1 (silkscreen).
- **Soldered at home:** U1 (TP9950 decoder) and MK1 (mic), because JLC has no stock of either. See [Parts you solder at home](#parts-you-solder-at-home). To have JLC place them again, empty `HAND_PLACED` in the export script.

## PCB options

| Option | Choose | Why |
|---|---|---|
| Layers | 4 | |
| Dimensions | 28 × 38 mm (filled in from the outline) | |
| PCB thickness | 1.6 mm | |
| Impedance control / layer stackup | **JLC04161H-7628** | The board's stackup and the MIPI impedance estimate (about 105 Ω differential) assume it. Paid impedance control isn't needed for traces this short |
| Solder mask colour | **Black** | |
| Silkscreen | **White** | |
| Surface finish | **ENIG** | Flat pads for the 0.4 mm-pitch QFN (U1) and the LGA mic (MK1), which you solder by hand. ENIG pads also stay solderable while you wait for the chips. Lead-free HASL is cheaper but less flat |
| Via covering | Tented | Matches the board setting; the logo prints over tented vias |
| Min via hole size / diameter | 0.3 mm / 0.5 mm | Every via on the board |
| Mark on PCB (order number) | **Specify a location** | JLC prints it where the back says `JLCJLCJLCJLC`, between the version text and the logo |
| Stencil | None | JLC uses its own for assembly. A stencil wouldn't lie flat for U1 at home: ten 0402 caps sit within 0.5 mm of it |
| Everything else | Default | |

## Assembly options

| Option | Choose |
|---|---|
| Assemble side | Top |
| Quantity | All 5. The loading fees are per order, so extra boards only add part cost, and a hand-soldered QFN may cost you a board |
| Tooling holes | Added by JLCPCB |
| Confirm parts placement | **Yes**, so you can approve JLC's placement before production |

**PCBA type:** Economic. With no consigned parts it should be accepted; if the site refuses a part, switch to Standard.

## Parts you solder at home

U1 (TP9950, QFN-40 5×5 mm, 0.4 mm pitch, 3.7 mm ground pad with 4 vias) and MK1 (IM73A135, LGA-5, bottom port). Buy them elsewhere, with spares: 3–5 TP9950s and 2 mics.

**Tools:** a hot plate that holds the whole 28 × 38 mm board, a hot-air rework station (small nozzle, adjustable airflow; not a paint-stripping heat gun), Sn63Pb37 solder paste in a syringe with a fine tip, no-clean gel flux, Kapton tape, fine tweezers, solder wick and a magnifier.

Why both heat sources: the ground pad's vias go into two inner ground planes that pull heat away, and ten 0402 caps sit within 0.5 mm of U1. Hot air alone needs long, strong heating that blows the caps off, can lift pads and can cook the mic. The plate does most of the work, so the hot air can stay gentle.

1. **Preheat** the board on the plate to about 150 °C. Every JLC part is on the top, so the board sits flat, and JLC's joints (217 °C) don't melt.
2. **Paste U1's pads:** a thin line across each row of pins, and 4 small dots on the ground pad covering about half of it. Too much there floats the chip and bridges the pins.
3. **Place U1** with the pin 1 dot toward the crystal Y1 (top-left).
4. **Hot air** at about 330 °C with low airflow, circling over the chip until it settles and centres itself (20–40 s). The nearby caps may melt briefly; surface tension holds them in place.
5. **Check for bridges** under magnification; clear them with flux and the iron.
6. **MK1 last.** Cover its sound hole on the back with Kapton so flux and paste stay out (the tape is fine on the plate). Paste the pads thinly, keeping paste off the hole's edge, and place the mic with its sound port over the hole and pin 1 on the footprint's pin 1. Pad 5 is the ring around the hole and needs solder all the way round: it seals the sound path. Check that the hole is open after removing the tape. Use gentle heat, never blow straight down into it, and don't soak it in alcohol or clean it ultrasonically.
7. **Before powering up,** measure 5V, 3V3 and 1V2 to GND with a multimeter. A bridge on U1 usually shows as a short on the 1.2 V rail.

## Check before paying

JLC shows each part on the board after matching. Check that U1's and MK1's pads are empty, and that each other part sits on its pads and faces the right way:
- **U2–U5, Q1:** pin 1 matches the footprint. U2 is turned 90° and U4 180° on purpose.
- **D1 and D2:** the cathode band is on pad 1.
- **Y1:** the crystal's pin 1 corner.
- **J1 and J2:** the cable openings face the board edges.

No rotation corrections are applied, and none should be needed:
- Every part except the passives and J2 uses LCSC's own footprint.
- The passives are symmetric.
- J2's KiCad footprint was compared with LCSC's footprint for C3029342: same pin 1 corner, centres within 0.06 mm.

If the preview still shows a part turned, fix it there, then add the offset to `ROT_OFFSET` in [`tools/jlc_export.py`](../kicad/tools/jlc_export.py) so the next export gets it right.

The part costs are in the [BOM](../bom.md#cost). With U1 and MK1 left out, the extended-part loading fees come to about $36 (12 types).
