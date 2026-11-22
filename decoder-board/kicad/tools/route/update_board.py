r"""Bring the routed board in line with the schematic without re-routing it from scratch.

Run with KiCad's Python:
    "C:\Program Files\KiCad\10.0\bin\python.exe" update_board.py board.kicad_pcb NET,NET,...

- removes every track and via of the listed nets (they get re-routed by route.py --reroute),
  plus GND stubs on pads whose part moves or whose net changes;
- sets every pad's net from the schematic netlist and adds footprints that are new in it;
- applies gen_pcb.py PLACE to footprints whose position or rotation differs;
- rewrites the J3 silkscreen labels and the version text from gen_pcb.py.
"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gen_pcb as G  # noqa: E402

VERSION = 'v0.2  2026-11'


def near(a, b, tol=0.01):
    return abs(pcbnew.ToMM(a.x - b.x)) < tol and abs(pcbnew.ToMM(a.y - b.y)) < tol


def main(pcb, reroute):
    comps, nodes = G.read_netlist()
    b = pcbnew.LoadBoard(pcb)
    fps = {f.GetReference(): f for f in b.GetFootprints()}

    # pads that move or change net lose their GND stubs
    changing = []
    for ref, f in fps.items():
        x, y, r = G.PLACE[ref]
        moved = not near(f.GetPosition(), G.pt(x, y)) or min((f.GetOrientationDegrees() - r) % 360, (r - f.GetOrientationDegrees()) % 360) > 0.01
        for p in f.Pads():
            if moved or nodes.get((ref, p.GetNumber()), '') != p.GetNetname():
                changing.append(p)
    # KiCad's Python breaks if the track list is fetched again after a removal: decide everything
    # from one list, then remove in one pass
    alltracks = list(b.GetTracks())
    gpads = [p for p in changing if p.GetNetname() == '/GND']
    drop, stub_ends = [], []
    for t in alltracks:
        if t.GetNetname() in reroute:
            drop.append(t)
        elif t.GetClass() != 'PCB_VIA' and t.GetNetname() == '/GND':
            for e, other in ((t.GetStart(), t.GetEnd()), (t.GetEnd(), t.GetStart())):
                if any(p.HitTest(e) for p in gpads):
                    stub_ends.append(other)
                    drop.append(t)
                    break
    remaining = [t for t in alltracks if t.GetClass() != 'PCB_VIA' and t.GetNetname() == '/GND' and t not in drop]
    nstub = 0
    for v in alltracks:
        if v.GetClass() == 'PCB_VIA' and v.GetNetname() == '/GND' and any(near(v.GetPosition(), e) for e in stub_ends) \
                and not any(near(v.GetPosition(), t.GetStart()) or near(v.GetPosition(), t.GetEnd()) for t in remaining):
            drop.append(v)
            nstub += 1
    nrm = len(drop) - len(stub_ends) - nstub
    for t in drop:
        b.Remove(t)

    # nets and footprints from the schematic
    netinfo = {n.GetNetname(): n for n in b.GetNetsByName().values()}
    for name in sorted(set(nodes.values())):
        if name not in netinfo:
            ni = pcbnew.NETINFO_ITEM(b, name)
            b.Add(ni)
            netinfo[name] = ni
    added = []
    for ref, c in sorted(comps.items()):
        if ref in fps:
            continue
        fp = G.load_fp(c['footprint'])
        fp.SetReference(ref)
        fp.SetValue(c['value'])
        fp.SetPath(pcbnew.KIID_PATH('/' + c['tstamp']))
        fp.SetSheetfile(G.PROJECT + '.kicad_sch')
        fp.SetSheetname('/')
        f = pcbnew.PCB_FIELD(fp, pcbnew.FIELD_T_USER, 'LCSC')
        f.SetText(c['lcsc'])
        f.SetVisible(False)
        f.SetLayer(pcbnew.F_Fab)
        fp.Add(f)
        fp.SetExcludedFromBOM(ref in G.NOT_IN_BOM)
        fp.SetDNP(c['dnp'])
        rt = fp.Reference()
        rt.SetTextSize(pcbnew.VECTOR2I(G.mm(0.6), G.mm(0.6)))
        rt.SetTextThickness(G.mm(0.1))
        rt.SetVisible(False)
        b.Add(fp)
        fps[ref] = fp
        added.append(ref)
    for ref, f in fps.items():
        x, y, r = G.PLACE[ref]
        f.SetPosition(G.pt(x, y))
        f.SetOrientationDegrees(r)
        c = comps[ref]
        f.SetValue(c['value'])
        for p in f.Pads():
            n = nodes.get((ref, p.GetNumber()))
            if n:
                p.SetNet(netinfo[n])
            else:
                p.SetNetCode(0)

    # silkscreen: J3 labels (sorted left to right) and the version text
    x0, y0, _ = G.PLACE['J3']
    texts = [d for d in b.GetDrawings() if isinstance(d, pcbnew.PCB_TEXT)]
    row = sorted([t for t in texts if t.GetLayer() == pcbnew.F_SilkS
                  and abs(pcbnew.ToMM(t.GetPosition().y) - G.OY - (y0 - G.J3_LABEL_DY)) < 0.3],
                 key=lambda t: t.GetPosition().x)
    for t, (i, lab) in zip(row, enumerate(G.J3_LABELS)):
        t.SetText(lab)
        t.SetPosition(G.pt(x0 + 2.54 * i + G.J3_LABEL_NUDGE.get(lab, 0), y0 - G.J3_LABEL_DY))
        t.SetTextSize(pcbnew.VECTOR2I(G.mm(G.J3_LABEL_SIZE), G.mm(G.J3_LABEL_SIZE)))
        t.SetTextThickness(G.mm(0.15))
    for t in texts:
        if t.GetText().startswith('v0.'):
            t.SetText(VERSION)
    pcbnew.SaveBoard(pcb, b)
    print('removed %d tracks/vias of %d nets, %d GND stubs (%d vias); added %s; J3 labels %d' % (
        nrm, len(reroute), len(stub_ends), nstub, added, len(row)))


if __name__ == '__main__':
    main(sys.argv[1], set(sys.argv[2].split(',')))
