# JLCPCB order files (decoder board v0.2)

| File | Upload it at |
|---|---|
| `open-dvr-decoder-v0.2-gerbers.zip` | The PCB quote page: 4 copper layers, masks, silkscreens, paste, outline, and the PTH/NPTH drill files |
| `open-dvr-decoder-v0.2-bom.csv` | PCB Assembly, BOM step: 24 lines, 57 parts, matched by LCSC number |
| `open-dvr-decoder-v0.2-cpl.csv` | PCB Assembly, CPL step: one placement per BOM part, all on the top side |

They are generated from the board. After any change in KiCad, make them again:

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" decoder-board\kicad\tools\jlc_export.py
```

Left out of the BOM and CPL:
- **Not fitted (DNP):** R10, R11, R12 and R15.
- **Not assembled:** J3 (you solder the wires), JP1 (solder jumper), H1 and H2 (holes) and LOGO1 (silkscreen).

## PCB options

| Option | Choose | Why |
|---|---|---|
| Layers | 4 | |
| Dimensions | 28 × 38 mm (filled in from the outline) | |
| PCB thickness | 1.6 mm | |
| Impedance control / layer stackup | **JLC04161H-7628** | The board's stackup and the MIPI impedance estimate (about 105 Ω differential) assume it. Paid impedance control isn't needed for traces this short |
| Solder mask colour | **Black** | |
| Silkscreen | **White** | |
| Surface finish | **ENIG** recommended | Flat pads for the 0.4 mm-pitch QFN (U1) and the LGA mic (MK1). Lead-free HASL is cheaper but less flat |
| Via covering | Tented | Matches the board setting; the logo prints over tented vias |
| Min via hole size / diameter | 0.3 mm / 0.5 mm | Every via on the board |
| Mark on PCB (order number) | **Specify a location** | JLC prints it where the back says `JLCJLCJLCJLC`, between the version text and the logo |
| Everything else | Default | |

## Assembly options

| Option | Choose |
|---|---|
| Assemble side | Top |
| Quantity | 2 for the first test (or all 5 PCBs) |
| Tooling holes | Added by JLCPCB |
| Confirm parts placement | **Yes**, so you can approve JLC's placement before production |

**PCBA type (Economic or Standard):** check what the site offers with the consigned decoder in the BOM. If it won't take the consigned part under Economic, switch to Standard.

## The decoder chip (U1, TP9950)

U1 is listed at LCSC as C2852602 but had no stock when we checked. Before ordering, look it up again on the BOM matching page:
- **If JLC can source it** (pre-order or global sourcing), take that.
- **Otherwise** buy TP9950s elsewhere and send them to JLC as consigned parts: Parts Manager, then Consign Parts. Order a few spares, since small QFNs get lost in handling. The board can only be assembled once the parts have arrived at JLC.

## Check before paying

JLC shows each part on the board after matching. Check that each one sits on its pads and faces the right way:
- **U1:** the pin 1 dot sits at the top-left corner, next to the crystal.
- **U2–U5, Q1:** pin 1 matches the footprint. U2 is turned 90° and U4 180° on purpose.
- **D1 and D2:** the cathode band is on pad 1.
- **Y1:** the crystal's pin 1 corner.
- **MK1:** the sound hole sits over the board's plated hole.
- **J1 and J2:** the cable openings face the board edges.

No rotation corrections are applied, and none should be needed:
- Every part except the passives and J2 uses LCSC's own footprint.
- The passives are symmetric.
- J2's KiCad footprint was compared with LCSC's footprint for C3029342: same pin 1 corner, centres within 0.06 mm.

If the preview still shows a part turned, fix it there, then add the offset to `ROT_OFFSET` in [`tools/jlc_export.py`](../kicad/tools/jlc_export.py) so the next export gets it right.

The part costs and the extended-part loading fees (about $42 for 14 types) are in the [BOM](../bom.md#cost).
