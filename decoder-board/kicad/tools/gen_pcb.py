#!/usr/bin/env python3
"""Create the first version of the decoder board PCB: outline, placement, nets, planes, rules.

Run with KiCad's own Python (it has the pcbnew module):

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" decoder-board/kicad/tools/gen_pcb.py

It reads the schematic's netlist (exported with kicad-cli), places every footprint at the
position in PLACE below, assigns the nets, draws the outline, adds GND planes on both inner
layers and writes the design rules and net classes into the project file. No tracks are
routed. Like gen_schematic.py, this creates the starting point only: once the board is edited
in KiCad, don't rerun it (it would overwrite the board).
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.dirname(HERE)
KICAD = os.environ.get('KICAD_DIR', r'C:\Program Files\KiCad\10.0')
KCLI = os.path.join(KICAD, 'bin', 'kicad-cli.exe')
KFP = os.path.join(KICAD, 'share', 'kicad', 'footprints')
PROJECT = 'open-dvr-decoder'
SCH = os.path.join(KDIR, PROJECT + '.kicad_sch')
PCB = os.path.join(KDIR, PROJECT + '.kicad_pcb')
PRO = os.path.join(KDIR, PROJECT + '.kicad_pro')

W, H = 28.0, 38.0          # board size, mm
OX, OY = 100.0, 100.0      # board origin on the page

# ref: (x, y, rotation in degrees, CCW), board coordinates in mm from the top-left corner
PLACE = {
    # Luckfox FPC (top contact) on the top edge, cable entering from the edge; pad n = Luckfox pin n
    'J1': (15.2, 4.3, 180),
    # Decoder: MIPI side (pins 21-26) faces J1, video pins (7, 8) face down, crystal on the left
    'U1': (11.5, 12.5, 0),
    'Y1': (4.6, 12.5, 270), 'C1': (4.6, 9.6, 0), 'C2': (4.6, 15.1, 180),   # XTI pad upper left, XTO lower right
    'R11': (3.0, 7.9, 270),                                       # MCLK -> XTI option (DNP)
    'C6': (7.3, 9.3, 180), 'C9': (7.3, 8.3, 180),                 # DVDD 28, VIO 27 (top-left, clear of the lane 1 vias)
    'C10': (7.7, 11.2, 90), 'C7': (7.6, 14.9, 270),                # VIO 35, DVDD 40 (left)
    'C8': (16.0, 13.0, 0), 'C5': (16.0, 14.4, 0),                 # VIO 14, DVDD 11 (right)
    'R4': (18.6, 10.8, 90), 'C11': (19.8, 10.8, 90),              # RSTB pull-up and cap
    'R12': (21.6, 7.4, 0),                                        # Luckfox 3.3 V link (DNP)
    'C3': (9.7, 17.1, 270), 'C4': (10.9, 17.1, 270),              # AVDD33 / AVDD12 100 nF
    'C12': (12.1, 17.1, 90), 'C13': (13.3, 17.1, 270),            # video coupling caps
    'C24': (9.7, 19.4, 270), 'C19': (10.9, 19.4, 270),            # AVDD33 / AVDD12 1 uF
    'FB1': (8.1, 18.3, 90), 'FB2': (12.4, 20.4, 90), 'FB3': (14.6, 19.4, 90),
    'C27': (12.0, 22.6, 0),                                       # DVDD 10 uF
    # Video and 5 V input (left)
    'J2': (3.5, 22.3, 270),
    'D1': (7.9, 20.6, 90), 'R3': (7.9, 22.6, 90), 'JP1': (8.4, 25.3, 90),
    'Q1': (2.2, 28.8, 0), 'R5': (4.8, 28.8, 90), 'R16': (6.3, 30.45, 0), 'D2': (7.6, 32.2, 0), 'C28': (2.6, 32.0, 0),
    # Camera 5 V out
    'F1': (12.6, 31.8, 0), 'FB4': (16.4, 31.8, 0), 'C29': (12.6, 29.2, 0),
    # 1.2 V buck (right, away from the video input and the mic)
    'U3': (20.6, 16.4, 0), 'L1': (24.0, 16.0, 90),
    'C25': (17.6, 16.6, 90), 'R1': (18.6, 18.8, 0), 'R2': (18.6, 20.0, 0),
    'C26': (26.0, 16.0, 90), 'C30': (24.0, 19.6, 0),
    # 3.3 V and 1.8 V LDOs (middle)
    'U2': (17.2, 22.6, 90), 'C15': (14.6, 22.6, 90), 'C16': (19.8, 22.6, 90),
    'U4': (17.2, 26.6, 180), 'C17': (14.2, 26.6, 90), 'C18': (20.2, 26.6, 90),
    'R9': (10.8, 24.6, 0), 'R10': (10.8, 25.8, 0),
    # Mic (bottom right): MK1 with its sound hole, LDO, coupling caps, attenuator
    'MK1': (25.4, 28.6, 0),
    'U5': (22.6, 22.6, 0), 'C20': (25.6, 21.8, 90), 'C21': (25.6, 24.0, 90), 'C14': (22.4, 25.4, 0),
    'C22': (22.6, 31.2, 180), 'C23': (22.6, 32.6, 180),           # mic side on the right
    'R13': (20.4, 31.2, 180), 'R14': (20.4, 32.6, 180), 'R15': (21.6, 33.65, 0),
    # One row of 10 holes along the bottom edge (wires or a 2.54 mm header)
    'J3': (2.57, 36.0, 90),
    'H1': (2.6, 2.6, 0), 'H2': (25.8, 2.4, 0),
}

J3_LABELS = ['VID', 'VG', '5Vin', 'GND', 'CAM', 'GND', 'VSYS', 'M+', 'M-', 'GND']
SILK = [  # text, x, y, size, angle, layer
    ('VID 5V GND', 4.0, 18.2, 0.6, 0, 'F'), ('75R', 8.4, 27.4, 0.6, 0, 'F'),
    ('open-dvr decoder', 14.0, 12.0, 1.0, 0, 'B'), ('v0.2  2026-11', 14.0, 13.6, 0.8, 0, 'B')]
SHOW_REF = ()                             # silkscreen references kept for these (none: labels instead)
NOT_IN_BOM = {'J3', 'JP1', 'H1', 'H2'}    # PCB-only, matches the schematic

RULES = {
    'min_clearance': 0.1, 'min_track_width': 0.1, 'min_via_diameter': 0.45,
    'min_through_hole_diameter': 0.3, 'min_via_annular_width': 0.075, 'min_hole_to_hole': 0.25,
    'min_copper_edge_clearance': 0.3, 'min_hole_clearance': 0.2, 'min_connection': 0.1,
    'min_text_height': 0.6, 'min_text_thickness': 0.1,
}
NETCLASSES = [
    # name, track, clearance, via dia, via drill, diff width, diff gap, patterns
    ('Default', 0.15, 0.15, 0.5, 0.3, 0.2, 0.15, []),
    ('Power', 0.4, 0.2, 0.6, 0.3, 0.2, 0.15,
     ['/VIN_RAW', '/5V', '/CAM_5V*', '/SW_1V2', '/1V2', '/3V3']),
    ('MIPI', 0.2, 0.15, 0.5, 0.3, 0.2, 0.2, ['/MIPI_*']),
    ('Analog', 0.2, 0.2, 0.5, 0.3, 0.2, 0.15, ['/VIDEO_IN', '/VID_REF', '/VIN1P', '/VIN1N', '/TERM', '/MIC_*']),
]


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


def sexp(text):
    """Parse an S-expression into nested lists of strings."""
    tok = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()"]+', text)
    stack = [[]]
    for t in tok:
        if t == '(':
            stack.append([])
        elif t == ')':
            done = stack.pop()
            stack[-1].append(done)
        else:
            stack[-1].append(t[1:-1].replace('\\"', '"') if t.startswith('"') else t)
    return stack[0][0]


def find(node, key):
    return [c for c in node[1:] if isinstance(c, list) and c and c[0] == key]


def val(node, key, default=''):
    f = find(node, key)
    return f[0][1] if f and len(f[0]) > 1 else default


def read_netlist():
    with tempfile.TemporaryDirectory() as td:
        net = os.path.join(td, 'net.net')
        subprocess.run([KCLI, 'sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', net, SCH],
                       check=True, capture_output=True)
        root = sexp(open(net, encoding='utf-8').read())
    comps = {}
    for c in find(find(root, 'components')[0], 'comp'):
        props = {val(p, 'name'): val(p, 'value') for p in find(c, 'property')}
        comps[val(c, 'ref')] = dict(value=val(c, 'value'), footprint=val(c, 'footprint'),
                                    tstamp=val(c, 'tstamps'), lcsc=props.get('LCSC', ''),
                                    dnp='dnp' in props)
    nodes = {}
    for n in find(find(root, 'nets')[0], 'net'):
        for nd in find(n, 'node'):
            nodes[(val(nd, 'ref'), val(nd, 'pin'))] = val(n, 'name')
    return comps, nodes


def load_fp(fpid):
    lib, name = fpid.split(':')
    path = os.path.join(KDIR, 'lib', 'lcsc.pretty') if lib == 'lcsc' else os.path.join(KFP, lib + '.pretty')
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        sys.exit('footprint not found: ' + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def main():
    comps, nodes = read_netlist()
    missing = sorted(set(comps) - set(PLACE))
    extra = sorted(set(PLACE) - set(comps))
    if missing or extra:
        sys.exit('placement mismatch: missing %s, extra %s' % (missing, extra))

    board = pcbnew.NewBoard(PCB)
    board.SetCopperLayerCount(4)
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(1.6))

    netinfo = {}
    for name in sorted(set(nodes.values())):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        netinfo[name] = ni

    for ref, c in sorted(comps.items()):
        fp = load_fp(c['footprint'])
        if ref == 'J3':                    # wires are soldered straight to the holes: no header model
            while len(fp.Models()):
                fp.Models().pop_back()
        fp.SetReference(ref)
        fp.SetValue(c['value'])
        fp.SetPath(pcbnew.KIID_PATH('/' + c['tstamp']))
        fp.SetSheetfile(PROJECT + '.kicad_sch')
        fp.SetSheetname('/')
        f = pcbnew.PCB_FIELD(fp, pcbnew.FIELD_T_USER, 'LCSC')
        f.SetText(c['lcsc'])
        f.SetVisible(False)
        f.SetLayer(pcbnew.F_Fab)
        fp.Add(f)
        fp.SetExcludedFromBOM(ref in NOT_IN_BOM)
        if c['dnp']:
            fp.SetDNP(True)
        x, y, rot = PLACE[ref]
        board.Add(fp)
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        for pad in fp.Pads():
            n = nodes.get((ref, pad.GetNumber()))
            if n:
                pad.SetNet(netinfo[n])
        # small reference text so it fits between 0402 parts
        rt = fp.Reference()
        rt.SetTextSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6)))
        rt.SetTextThickness(mm(0.1))
        if not ref.startswith(SHOW_REF):
            rt.SetVisible(False)

    # outline
    corners = [(0, 0), (W, 0), (W, H), (0, H)]
    for (x1, y1), (x2, y2) in zip(corners, corners[1:] + corners[:1]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(x1, y1))
        s.SetEnd(pt(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)

    # GND planes on both inner layers
    for layer in (pcbnew.In1_Cu, pcbnew.In2_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(netinfo['/GND'])
        z.SetZoneName('GND_' + board.GetLayerName(layer))
        z.SetLocalClearance(mm(0.25))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        ol = z.Outline()
        ol.NewOutline()
        inset = 0.4
        for x, y in [(inset, inset), (W - inset, inset), (W - inset, H - inset), (inset, H - inset)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)

    x0, y0, _ = PLACE['J3']
    nudge = {'M+': -0.2, 'M-': 0.2}       # clear of R15's pads
    labels = SILK + [(lab, x0 + 2.54 * i + nudge.get(lab, 0), y0 - 2.3, 0.6, 0, 'F') for i, lab in enumerate(J3_LABELS)]
    for text, x, y, size, angle, side in labels:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetPosition(pt(x, y))
        t.SetTextAngleDegrees(angle)
        t.SetLayer(pcbnew.F_SilkS if side == 'F' else pcbnew.B_SilkS)
        t.SetMirrored(side == 'B')
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.12))
        board.Add(t)

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)

    # design rules and net classes live in the project file
    pro = json.load(open(PRO, encoding='utf-8')) if os.path.exists(PRO) else {}
    pro.setdefault('meta', {'filename': PROJECT + '.kicad_pro', 'version': 3})
    rules = pro.setdefault('board', {}).setdefault('design_settings', {}).setdefault('rules', {})
    rules.update(RULES)
    classes, patterns = [], []
    for name, tw, cl, vd, vdr, dw, dg, pats in NETCLASSES:
        classes.append({'name': name, 'track_width': tw, 'clearance': cl, 'via_diameter': vd,
                        'via_drill': vdr, 'diff_pair_width': dw, 'diff_pair_gap': dg,
                        'diff_pair_via_gap': 0.25, 'microvia_diameter': 0.3, 'microvia_drill': 0.1,
                        'bus_width': 12, 'wire_width': 6, 'line_style': 0,
                        'pcb_color': 'rgba(0, 0, 0, 0.000)', 'schematic_color': 'rgba(0, 0, 0, 0.000)',
                        'priority': 2147483647 if name == 'Default' else len(classes)})
        patterns += [{'netclass': name, 'pattern': p} for p in pats]
    pro['net_settings'] = {'classes': classes, 'meta': {'version': 4}, 'net_colors': None,
                           'netclass_assignments': None, 'netclass_patterns': patterns}
    json.dump(pro, open(PRO, 'w', encoding='utf-8'), indent=2)
    print('wrote %s: %d footprints, %d nets' % (PCB, len(comps), len(netinfo)))


if __name__ == '__main__':
    main()
