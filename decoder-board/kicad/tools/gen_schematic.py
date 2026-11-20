#!/usr/bin/env python3
"""Generate the first version of the decoder board schematic (KiCad 10).

    python decoder-board/kicad/tools/gen_schematic.py

The netlist below is the design: every pin of every part is tied to a named net with a
label, so the schematic reads as a set of functional blocks rather than a wired drawing.
This script was used to create the first version of open-dvr-decoder.kicad_sch; once the
schematic is edited in KiCad, edit it there and don't regenerate (that would overwrite it).

Symbols are copied from KiCad's own libraries and from kicad/lib/lcsc.kicad_sym (upgraded
to the KiCad 10 format with `kicad-cli sym upgrade` on the fly).
"""
import os
import re
import subprocess
import sys
import tempfile
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.dirname(HERE)                       # decoder-board/kicad
KICAD = os.environ.get('KICAD_DIR', r'C:\Program Files\KiCad\10.0')
KSYM = os.path.join(KICAD, 'share', 'kicad', 'symbols')
KCLI = os.path.join(KICAD, 'bin', 'kicad-cli.exe')
PROJECT = 'open-dvr-decoder'
OUT = os.path.join(KDIR, PROJECT + '.kicad_sch')

ROOT_UUID = '5d1c3a7e-0f0b-4a52-9d1e-6c2f3b8a9e10'   # fixed, so regenerating keeps the same sheet id

# ---------------------------------------------------------------------------------------
# Footprints for KiCad-library parts
FP_R0402 = 'Resistor_SMD:R_0402_1005Metric'
FP_C0402 = 'Capacitor_SMD:C_0402_1005Metric'
FP_C0603 = 'Capacitor_SMD:C_0603_1608Metric'
FP_C0805 = 'Capacitor_SMD:C_0805_2012Metric'
FP_SH3 = 'Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal'
FP_HDR2 = 'Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical'
FP_HDR3 = 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical'
FP_SJ = 'Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm'
FP_HOLE = 'MountingHole:MountingHole_2.2mm_M2'

# Common passives (LCSC numbers, JLC basic parts unless noted)
C100N = ('100nF', FP_C0402, 'C1525')
C1U = ('1uF', FP_C0402, 'C52923')
C10U = ('10uF', FP_C0603, 'C19702')
C22U = ('22uF', FP_C0805, 'C45783')
C33P = ('33pF', FP_C0402, 'C1562')
R10K = ('10k', FP_R0402, 'C25744')
R100K = ('100k', FP_R0402, 'C25741')
R4K7 = ('4.7k', FP_R0402, 'C25900')
R0 = ('0R', FP_R0402, 'C17168')
R75 = ('75R', FP_R0402, 'C25133')      # extended


def cap(ref, spec, a, b, x, y, dnp=False):
    v, fp, lcsc = spec
    return dict(ref=ref, lib='Device:C', value=v, fp=fp, lcsc=lcsc, at=(x, y), rot=90,
                pins={'1': a, '2': b}, dnp=dnp)


def res(ref, spec, a, b, x, y, dnp=False):
    v, fp, lcsc = spec
    return dict(ref=ref, lib='Device:R', value=v, fp=fp, lcsc=lcsc, at=(x, y), rot=90,
                pins={'1': a, '2': b}, dnp=dnp)


def part(ref, lib, value, lcsc, x, y, pins, fp=None, rot=0, dnp=False):
    return dict(ref=ref, lib=lib, value=value, fp=fp, lcsc=lcsc, at=(x, y), rot=rot,
                pins=pins, dnp=dnp)


NC = None
P = []      # parts
T = []      # block titles: (text, x, y)

# ---------------------------------------------------------------------------------------
# 1. Power input, protection, power outputs
bx, by = 25, 30
T.append(('POWER INPUT (5 V from the FC) AND OUTPUTS', bx, by))
P += [
    part('J2', 'Connector_Generic:Conn_01x03', 'SH1.0 Video/5V/GND', 'C3029342', bx + 10, by + 15,
         {'1': 'VIDEO_IN', '2': 'VIN_RAW', '3': 'GND'}, fp=FP_SH3),
    part('J3', 'Connector_Generic:Conn_01x03', 'Pads Video/5V/GND', '', bx + 10, by + 35,
         {'1': 'VIDEO_IN', '2': 'VIN_RAW', '3': 'GND'}, fp=FP_HDR3),
    part('Q1', 'lcsc:AO3401A', 'AO3401A', 'C15127', bx + 50, by + 15,
         {'3': 'VIN_RAW', '2': '5V', '1': 'Q1_G'}),
    res('R5', R10K, 'Q1_G', 'GND', bx + 50, by + 32),
    part('D2', 'lcsc:SMF5.0A_C193402', 'SMF5.0A', 'C193402', bx + 90, by + 15,
         {'1': '5V', '2': 'GND'}),
    cap('C28', C22U, '5V', 'GND', bx + 90, by + 32),
    part('J5', 'Connector_Generic:Conn_01x02', '5V to Luckfox VSYS', '', bx + 130, by + 15,
         {'1': '5V', '2': 'GND'}, fp=FP_HDR2),
    part('F1', 'lcsc:BSMD0805-050-6V', 'BSMD0805-050-6V', 'C883108', bx + 50, by + 50,
         {'1': '5V', '2': 'CAM_5V_F'}),
    part('FB4', 'lcsc:BLM18PG121SN1D_C14709', 'BLM18PG121SN1D', 'C14709', bx + 90, by + 50,
         {'1': 'CAM_5V_F', '2': 'CAM_5V'}),
    cap('C29', C22U, 'CAM_5V', 'GND', bx + 90, by + 65),
    part('J4', 'Connector_Generic:Conn_01x02', 'Camera 5V out', '', bx + 130, by + 50,
         {'1': 'CAM_5V', '2': 'GND'}, fp=FP_HDR2),
]

# ---------------------------------------------------------------------------------------
# 2. Regulators
bx, by = 25, 130
T.append(('REGULATORS: 3.3 V, 1.8 V (VIO), 1.2 V (core)', bx, by))
P += [
    part('U2', 'lcsc:ME6211C33M5G-N', 'ME6211C33M5G-N', 'C82942', bx + 25, by + 20,
         {'1': '5V', '2': 'GND', '3': '5V', '4': NC, '5': '3V3'}),
    cap('C15', C1U, '5V', 'GND', bx + 60, by + 12),
    cap('C16', C1U, '3V3', 'GND', bx + 60, by + 24),
    part('U4', 'lcsc:ME6211C18M5G-N', 'ME6211C18M5G-N', 'C236671', bx + 25, by + 50,
         {'1': '3V3', '2': 'GND', '3': '3V3', '4': NC, '5': '1V8'}),
    cap('C17', C1U, '3V3', 'GND', bx + 60, by + 42),
    cap('C18', C1U, '1V8', 'GND', bx + 60, by + 54),
    res('R9', R0, '1V8', 'VIO', bx + 100, by + 42),
    res('R10', R0, '3V3', 'VIO', bx + 100, by + 54, dnp=True),
    part('U3', 'lcsc:TLV62569DBVR', 'TLV62569DBVR', 'C141836', bx + 25, by + 85,
         {'1': '5V', '2': 'GND', '3': 'SW_1V2', '4': '5V', '5': 'FB_1V2'}),
    part('L1', 'lcsc:CKCS201610-2.2UH_M-N', '2.2uH', 'C354533', bx + 60, by + 75,
         {'1': 'SW_1V2', '2': '1V2'}),
    res('R1', R100K, '1V2', 'FB_1V2', bx + 60, by + 87),
    res('R2', R100K, 'FB_1V2', 'GND', bx + 60, by + 99),
    cap('C25', C10U, '5V', 'GND', bx + 100, by + 75),
    cap('C26', C10U, '1V2', 'GND', bx + 100, by + 87),
    cap('C30', C22U, '1V2', 'GND', bx + 100, by + 99),
    part('FB1', 'lcsc:BLM18PG121SN1D_C14709', 'BLM18PG121SN1D', 'C14709', bx + 140, by + 15,
         {'1': '3V3', '2': 'AVDD33'}),
    part('FB2', 'lcsc:BLM18PG121SN1D_C14709', 'BLM18PG121SN1D', 'C14709', bx + 140, by + 35,
         {'1': '1V2', '2': 'AVDD12'}),
    part('FB3', 'lcsc:BLM18PG121SN1D_C14709', 'BLM18PG121SN1D', 'C14709', bx + 140, by + 55,
         {'1': '1V2', '2': 'DVDD'}),
]

# ---------------------------------------------------------------------------------------
# 3. Decoder
bx, by = 230, 30
T.append(('VIDEO DECODER TP9950 (CVBS -> MIPI CSI-2), I2C ADDRESS 0x44', bx, by))
tp = {str(n): NC for n in range(1, 42)}
tp.update({
    '1': 'AVDD33', '2': 'AVDD12', '7': 'VIN1P', '8': 'VIN1N',
    '11': 'DVDD', '28': 'DVDD', '40': 'DVDD',
    '14': 'VIO', '27': 'VIO', '35': 'VIO',
    '15': 'RSTB', '16': 'SCL', '17': 'SDA',
    '21': 'MIPI_CKN', '22': 'MIPI_CKP', '23': 'MIPI_D0N', '24': 'MIPI_D0P',
    '25': 'MIPI_D1N', '26': 'MIPI_D1P',
    '36': 'XTI', '37': 'XTO', '38': 'GND', '39': 'GND', '41': 'GND',
})
P.append(part('U1', 'lcsc:TP9950', 'TP9950 (consigned)', 'C2852602', bx + 45, by + 60, tp))
dx, dy = bx + 110, by + 12
P += [
    cap('C3', C100N, 'AVDD33', 'GND', dx, dy),
    cap('C24', C1U, 'AVDD33', 'GND', dx, dy + 12.7),
    cap('C4', C100N, 'AVDD12', 'GND', dx, dy + 25.4),
    cap('C19', C1U, 'AVDD12', 'GND', dx, dy + 38.1),
    cap('C5', C100N, 'DVDD', 'GND', dx, dy + 50.8),
    cap('C6', C100N, 'DVDD', 'GND', dx, dy + 63.5),
    cap('C7', C100N, 'DVDD', 'GND', dx, dy + 76.2),
    cap('C27', C10U, 'DVDD', 'GND', dx, dy + 88.9),
    cap('C8', C100N, 'VIO', 'GND', dx + 40, dy),
    cap('C9', C100N, 'VIO', 'GND', dx + 40, dy + 12.7),
    cap('C10', C100N, 'VIO', 'GND', dx + 40, dy + 25.4),
    res('R4', R10K, 'VIO', 'RSTB', dx + 40, dy + 38.1),
    cap('C11', C100N, 'RSTB', 'GND', dx + 40, dy + 50.8),
    res('R7', R4K7, 'VIO', 'SCL', dx + 40, dy + 63.5, dnp=True),
    res('R8', R4K7, 'VIO', 'SDA', dx + 40, dy + 76.2, dnp=True),
    part('Y1', 'lcsc:X322527MSB4SI', '27MHz 20pF', 'C9008', bx + 20, by + 145,
         {'1': 'XTI', '3': 'XTO', '2': 'GND', '4': 'GND'}),
    cap('C1', C33P, 'XTI', 'GND', bx + 60, by + 140),
    cap('C2', C33P, 'XTO', 'GND', bx + 60, by + 152),
    res('R11', R0, 'MCLK', 'XTI', bx + 100, by + 140, dnp=True),
]

# ---------------------------------------------------------------------------------------
# 4. Video input
bx, by = 25, 250
T.append(('VIDEO INPUT (parallel tap: JP1 open = no 75 ohm termination)', bx, by))
P += [
    part('D1', 'lcsc:ESD9L5.0ST5G-N', 'ESD9L5.0ST5G-N', 'C316041', bx + 15, by + 15,
         {'1': 'VIDEO_IN', '2': 'GND'}),
    res('R3', R75, 'VIDEO_IN', 'TERM', bx + 55, by + 15),
    part('JP1', 'Jumper:SolderJumper_2_Open', 'Term 75R', '', bx + 95, by + 15,
         {'1': 'TERM', '2': 'GND'}, fp=FP_SJ),
    cap('C12', C100N, 'VIDEO_IN', 'VIN1P', bx + 55, by + 30),
    cap('C13', C100N, 'VIN1N', 'GND', bx + 55, by + 42),
]

# ---------------------------------------------------------------------------------------
# 5. Host connector (Luckfox camera FPC, pin n = Luckfox H1 pin n)
bx, by = 420, 30
T.append(('LUCKFOX CAMERA FPC (J1 pin n = Luckfox H1 pin n, opposite-side cable)', bx, by))
j1 = {
    '1': 'GND', '2': 'MCLK', '3': 'GND', '4': 'MIPI_D0P', '5': 'MIPI_D0N', '6': 'GND',
    '7': 'MIPI_CKP', '8': 'MIPI_CKN', '9': 'GND', '10': 'MIPI_D1P', '11': 'MIPI_D1N',
    '12': 'GND', '13': 'SCL', '14': 'SDA', '15': 'GND', '16': 'RSTB', '17': 'GND',
    '18': 'GND', '19': 'LF_3V3', '20': 'LF_3V3', '21': 'GND', '22': 'GND',
}
P += [
    part('J1', 'lcsc:05A20L20P', 'FPC 0.5mm 20P', 'C5362157', bx + 30, by + 50, j1),
    res('R12', R0, 'LF_3V3', '3V3', bx + 80, by + 20, dnp=True),
]

# ---------------------------------------------------------------------------------------
# 6. Microphone
bx, by = 230, 250
T.append(('MICROPHONE IM73A135 (abs. max 3.0 V: 2.8 V LDO only) -> Luckfox MIC0 pads', bx, by))
P += [
    part('U5', 'lcsc:ME6211C28M5G', 'ME6211C28M5G-N', 'C53099', bx + 25, by + 20,
         {'1': '3V3', '2': 'GND', '3': '3V3', '4': NC, '5': '2V8_MIC'}),
    cap('C20', C1U, '3V3', 'GND', bx + 60, by + 12),
    cap('C21', C1U, '2V8_MIC', 'GND', bx + 60, by + 24),
    part('MK1', 'lcsc:IM73A135V01XTSA1', 'IM73A135V01XTSA1', 'C3171831', bx + 25, by + 55,
         {'1': 'MIC_OUTP', '2': '2V8_MIC', '3': 'MIC_OUTN', '4': 'GND', '5': 'GND'}),
    cap('C14', C100N, '2V8_MIC', 'GND', bx + 60, by + 45),
    cap('C22', C1U, 'MIC_OUTP', 'MIC_P_C', bx + 60, by + 58),
    cap('C23', C1U, 'MIC_OUTN', 'MIC_N_C', bx + 60, by + 70),
    res('R13', R0, 'MIC_P_C', 'MIC_P', bx + 100, by + 58),
    res('R14', R0, 'MIC_N_C', 'MIC_N', bx + 100, by + 70),
    res('R15', R10K, 'MIC_P', 'MIC_N', bx + 100, by + 84, dnp=True),
    part('J6', 'Connector_Generic:Conn_01x03', 'Mic to Luckfox pads 3/4 + GND', '', bx + 140, by + 60,
         {'1': 'MIC_P', '2': 'MIC_N', '3': 'GND'}, fp=FP_HDR3),
]

# ---------------------------------------------------------------------------------------
# 7. Mounting
bx, by = 420, 250
T.append(('MOUNTING', bx, by))
P += [
    part('H1', 'Mechanical:MountingHole', 'M2', '', bx + 10, by + 15, {}, fp=FP_HOLE),
    part('H2', 'Mechanical:MountingHole', 'M2', '', bx + 30, by + 15, {}, fp=FP_HOLE),
]

NOTES = [
    'open-dvr decoder board, first version generated by tools/gen_schematic.py from the netlist there.',
    'Nets are connected by labels. DNP parts: R7 R8 R10 R11 R12 R15 (not fitted).',
    'C13: route its GND side to the video connector ground (pseudo-differential input).',
]

# ---------------------------------------------------------------------------------------
# S-expression helpers


def find_block(text, start):
    """Return the end index (exclusive) of the parenthesised block starting at text[start]=='('."""
    depth, i, n = 0, start, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError('unbalanced')


def top_symbols(libtext):
    """Map name -> block text for each top-level symbol in a .kicad_sym file."""
    out = {}
    for m in re.finditer(r'\n\t\(symbol "([^"]+)"', libtext):
        s = m.start() + 2
        out[m.group(1)] = libtext[s:find_block(libtext, s)]
    return out


def load_lib(path):
    return top_symbols(open(path, encoding='utf-8').read())


PIN_RE = re.compile(r'\(pin (\w+) (\w+)\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)\s*\(length ([-\d.]+)\)'
                    r'.*?\(name "([^"]*)".*?\(number "([^"]*)"', re.S)


def pins_of(block):
    pins = {}
    for t, _, x, y, a, ln, name, num in PIN_RE.findall(block):
        pins.setdefault(num, (float(x), float(y), float(a), name, t))
    return pins


def embed(block, libname, name):
    """Rename a library symbol to lib:name for the schematic's lib_symbols section."""
    head = '(symbol "%s"' % name
    assert block.startswith(head), block[:60]
    return '(symbol "%s:%s"' % (libname, name) + block[len(head):]


# ---------------------------------------------------------------------------------------
def main():
    libs = {}
    for lib in ('Device', 'Connector_Generic', 'Jumper', 'Mechanical'):
        libs[lib] = load_lib(os.path.join(KSYM, lib + '.kicad_sym'))
    with tempfile.TemporaryDirectory() as td:
        up = os.path.join(td, 'lcsc.kicad_sym')
        subprocess.run([KCLI, 'sym', 'upgrade', '--force', '-o', up,
                        os.path.join(KDIR, 'lib', 'lcsc.kicad_sym')],
                       check=True, capture_output=True)
        libs['lcsc'] = load_lib(up)

    used = sorted({p['lib'] for p in P})
    lib_blocks, pinmap, fps = [], {}, {}
    for lid in used:
        lib, name = lid.split(':')
        blk = libs[lib][name]
        if '(extends' in blk:
            sys.exit('extends not handled: ' + lid)
        pinmap[lid] = pins_of(blk)
        m = re.search(r'\(property "Footprint" "([^"]*)"', blk)
        fps[lid] = m.group(1) if m else ''
        lib_blocks.append(embed(blk, lib, name))

    U = lambda: str(uuid.uuid4())
    out = []
    w = out.append
    w('(kicad_sch\n\t(version 20250610)\n\t(generator "eeschema")\n\t(generator_version "10.0")\n')
    w('\t(uuid "%s")\n\t(paper "A2")\n' % ROOT_UUID)
    w('\t(title_block\n\t\t(title "open-dvr decoder board (DIY kit)")\n\t\t(rev "0.1")\n'
      '\t\t(comment 1 "TP9950 CVBS->MIPI + IM73A135 mic, for the Luckfox Pico Pro/Max")\n\t)\n')
    w('\t(lib_symbols\n')
    for b in lib_blocks:
        w('\t\t' + b.replace('\n', '\n\t') + '\n')
    w('\t)\n')

    def text(s, x, y, size=2.54, bold=True):
        w('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %.2f %.2f 0)\n\t\t(effects\n\t\t\t(font\n'
          '\t\t\t\t(size %s %s)\n%s\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
          % (s, x, y, size, size, '\t\t\t\t(bold yes)\n' if bold else '', U()))

    for s, x, y in T:
        text(s, x, y)
    for i, s in enumerate(NOTES):
        text(s, 25, 395 + i * 5, size=1.8, bold=False)

    def xform(px, py, rot):
        vx, vy = px, -py
        r = rot % 360
        if r == 0:
            return vx, vy
        if r == 90:
            return vy, -vx
        if r == 180:
            return -vx, -vy
        return -vy, vx

    labels, ncs = [], []
    for p in P:          # snap every symbol to the 2.54 mm grid so its pins land on the 1.27 mm grid
        p['at'] = tuple(round(round(v / 2.54) * 2.54, 2) for v in p['at'])
    for p in P:
        lid = p['lib']
        x0, y0 = p['at']
        rot = p['rot']
        fp = p['fp'] or fps[lid]
        pins = pinmap[lid]
        unknown = set(p['pins']) - set(pins)
        if unknown:
            sys.exit('%s: no pins %s in %s' % (p['ref'], sorted(unknown), lid))
        missing = set(pins) - set(p['pins'])
        if missing:
            sys.exit('%s: pins %s not assigned' % (p['ref'], sorted(missing, key=lambda s: int(s) if s.isdigit() else 0)))
        ref = p['ref']
        props = [('Reference', ref, (x0, y0 - 3.0), False),
                 ('Value', p['value'], (x0, y0 + 3.0), False),
                 ('Footprint', fp, (x0, y0), True),
                 ('Datasheet', '', (x0, y0), True),
                 ('LCSC', p['lcsc'], (x0, y0), True)]
        if lid.startswith('lcsc:') or lid.startswith('Connector_Generic'):
            props[0] = ('Reference', ref, (x0, y0 - 2.54 - max(-xform(*v[:2], rot)[1] for v in pins.values())), False)
            props[1] = ('Value', p['value'], (x0, y0 + 2.54 + max(xform(*v[:2], rot)[1] for v in pins.values())), False)
        w('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %.2f %.2f %d)\n\t\t(unit 1)\n\t\t(exclude_from_sim no)\n'
          '\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp %s)\n\t\t(uuid "%s")\n'
          % (lid, x0, y0, rot, 'yes' if p['dnp'] else 'no', U()))
        for k, v, (px, py), hide in props:
            w('\t\t(property "%s" "%s"\n\t\t\t(at %.2f %.2f %d)\n%s\t\t\t(effects\n\t\t\t\t(font\n'
              '\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
              % (k, v, px, py, rot, '\t\t\t(hide yes)\n' if hide else ''))
        for num in pins:
            w('\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)\n' % (num, U()))
        w('\t\t(instances\n\t\t\t(project "%s"\n\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n'
          '\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n' % (PROJECT, ROOT_UUID, ref))
        for num, (px, py, a, name, t) in pins.items():
            dx, dy = xform(px, py, rot)
            ex, ey = round(x0 + dx, 3), round(y0 + dy, 3)
            net = p['pins'][num]
            if net is None:
                ncs.append((ex, ey))
            else:
                ang = int((a + 180 + rot) % 360)
                labels.append((net, ex, ey, ang))

    for net, x, y, ang in labels:
        just = 'left bottom' if ang in (0, 90) else 'right bottom'
        w('\t(label "%s"\n\t\t(at %.3f %.3f %d)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n'
          '\t\t\t)\n\t\t\t(justify %s)\n\t\t)\n\t\t(uuid "%s")\n\t)\n' % (net, x, y, ang, just, U()))
    for x, y in ncs:
        w('\t(no_connect\n\t\t(at %.3f %.3f)\n\t\t(uuid "%s")\n\t)\n' % (x, y, U()))
    w('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n\t(embedded_fonts no)\n)\n')

    open(OUT, 'w', encoding='utf-8', newline='\n').write(''.join(out))
    print('wrote %s: %d parts, %d labels, %d no-connects' % (OUT, len(P), len(labels), len(ncs)))


if __name__ == '__main__':
    main()
