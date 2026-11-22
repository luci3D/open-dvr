r"""Make the JLCPCB order files: Gerbers + drill (zip), assembly BOM and placement (CPL).

Run with KiCad's Python from anywhere:
    "C:\Program Files\KiCad\10.0\bin\python.exe" decoder-board\kicad\tools\jlc_export.py

Writes decoder-board/jlc/open-dvr-decoder-<version>-{gerbers.zip,bom.csv,cpl.csv}.
- Gerbers follow JLC's KiCad guide: Protel extensions, no X2, no netlist attributes,
  soldermask subtracted from silkscreen, zones refilled; Excellon drill in mm, PTH and NPTH
  in separate files.
- BOM and CPL list only assembled parts: DNP parts and parts excluded from the BOM (J3, JP1,
  H1, H2, LOGO1) are left out. Parts are grouped by LCSC number.
- ROT_OFFSET corrects any footprint whose zero rotation differs from JLC's. Every part on this
  board needs none: the lcsc footprints come from LCSC's own library, the 0402/0603/0805
  passives are symmetric, and J2's KiCad JST SH footprint was checked against LCSC's
  footprint for C3029342 (same pin 1 corner and centre). Still check the placement preview
  on JLC's site before paying.
"""
import csv
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.dirname(HERE)
PCB = os.path.join(KDIR, 'open-dvr-decoder.kicad_pcb')
OUT = os.path.join(os.path.dirname(KDIR), 'jlc')
KCLI = os.path.join(os.path.dirname(sys.executable), 'kicad-cli.exe')
LAYERS = 'F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts'
ROT_OFFSET = {}            # footprint name -> degrees to add for JLC (none needed, see above)


def run(*args):
    r = subprocess.run([KCLI, *args], capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode:
        sys.exit('kicad-cli failed: %s\n%s' % (' '.join(args), r.stderr or r.stdout))
    return r.stdout


def version(b):
    for d in b.GetDrawings():
        if isinstance(d, pcbnew.PCB_TEXT) and re.match(r'v\d+\.\d+', d.GetText()):
            return d.GetText().split()[0]
    return 'v0'


def main():
    b = pcbnew.LoadBoard(PCB)
    ver = version(b)
    base = 'open-dvr-decoder-%s' % ver
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.startswith('open-dvr-decoder-'):
            os.remove(os.path.join(OUT, f))

    # assembled parts, straight from the board
    parts = {}
    for fp in b.GetFootprints():
        if fp.IsDNP() or fp.IsExcludedFromBOM():
            continue
        lcsc = next((x.GetText() for x in fp.GetFields() if x.GetName() == 'LCSC'), '')
        if not lcsc:
            sys.exit('%s has no LCSC number' % fp.GetReference())
        parts[fp.GetReference()] = (fp.GetValue(), str(fp.GetFPID().GetLibItemName()), lcsc)

    with tempfile.TemporaryDirectory() as td:
        g = os.path.join(td, 'gerbers')
        os.makedirs(g)
        run('pcb', 'export', 'gerbers', '--layers', LAYERS, '--subtract-soldermask', '--no-x2', '--no-netlist',
            '--check-zones', '-o', g + os.sep, PCB)
        run('pcb', 'export', 'drill', '--format', 'excellon', '--excellon-units', 'mm',
            '--excellon-zeros-format', 'decimal', '--drill-origin', 'absolute', '--excellon-separate-th',
            '-o', g + os.sep, PCB)
        files = sorted(os.listdir(g))
        with zipfile.ZipFile(os.path.join(OUT, base + '-gerbers.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
            for f in files:
                z.write(os.path.join(g, f), f)
        pos = os.path.join(td, 'pos.csv')
        run('pcb', 'export', 'pos', '--format', 'csv', '--units', 'mm', '--side', 'both', '--exclude-dnp',
            '-o', pos, PCB)
        rows = list(csv.DictReader(open(pos, encoding='utf-8')))

    # placement (CPL)
    cpl = []
    for r in rows:
        ref = r['Ref']
        if ref not in parts:
            continue
        fpname = parts[ref][1]
        rot = (float(r['Rot']) + ROT_OFFSET.get(fpname, 0)) % 360
        cpl.append((ref, '%.4fmm' % float(r['PosX']), '%.4fmm' % float(r['PosY']),
                    'Top' if r['Side'].lower() == 'top' else 'Bottom', '%g' % rot))
    missing = set(parts) - {c[0] for c in cpl}
    if missing:
        sys.exit('no placement for %s' % sorted(missing))
    key = lambda ref: (re.sub(r'\d+', '', ref), int(re.sub(r'\D', '', ref) or 0))
    with open(os.path.join(OUT, base + '-cpl.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        w.writerows(sorted(cpl, key=lambda c: key(c[0])))

    # BOM, one line per LCSC part
    groups = {}
    for ref, (val, fpname, lcsc) in parts.items():
        groups.setdefault((lcsc, val, fpname), []).append(ref)
    with open(os.path.join(OUT, base + '-bom.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (lcsc, val, fpname), refs in sorted(groups.items(), key=lambda kv: key(sorted(kv[1], key=key)[0])):
            w.writerow([val, ','.join(sorted(refs, key=key)), fpname, lcsc])

    print('%s: %d Gerber/drill files zipped, BOM %d lines (%d parts), CPL %d placements -> %s' % (
        base, len(files), len(groups), len(parts), len(cpl), OUT))
    for f in files:
        print('  ' + f)


if __name__ == '__main__':
    main()
