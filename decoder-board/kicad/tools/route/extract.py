"""Dump the board's copper geometry to JSON for route.py.

Run with KiCad's Python:  "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" extract.py board.kicad_pcb out.json

Coordinates are in mm relative to the board origin (100, 100) used by gen_pcb.py.
"""
import json
import sys

import pcbnew

OX, OY = 100.0, 100.0
T = pcbnew.ToMM


def poly_points(ps):
    out = []
    for i in range(ps.OutlineCount()):
        ol = ps.Outline(i)
        out.append([(round(T(ol.CPoint(k).x) - OX, 4), round(T(ol.CPoint(k).y) - OY, 4))
                    for k in range(ol.PointCount())])
    return out


def main(pcb, out):
    b = pcbnew.LoadBoard(pcb)
    err = pcbnew.FromMM(0.005)
    pads = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            layers = []
            for name, lid in (('F', pcbnew.F_Cu), ('B', pcbnew.B_Cu)):
                if p.IsOnLayer(lid):
                    ps = pcbnew.SHAPE_POLY_SET()
                    p.TransformShapeToPolygon(ps, lid, 0, err, pcbnew.ERROR_INSIDE)
                    layers.append((name, poly_points(ps)))
            hole = None
            if p.HasHole():
                d = p.GetDrillSize()
                hole = round(T(max(d.x, d.y)) / 2, 4)
            pos = p.GetPosition()
            pads.append({'ref': fp.GetReference(), 'num': p.GetNumber(), 'net': p.GetNetname(),
                         'x': round(T(pos.x) - OX, 4), 'y': round(T(pos.y) - OY, 4),
                         'layers': layers, 'hole': hole,
                         'pth': p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH})
    tracks, vias = [], []
    for t in b.GetTracks():
        if t.GetClass() == 'PCB_VIA':
            pos = t.GetPosition()
            vias.append({'net': t.GetNetname(), 'x': round(T(pos.x) - OX, 4), 'y': round(T(pos.y) - OY, 4),
                         'd': round(T(t.GetWidth(pcbnew.F_Cu)), 4), 'drill': round(T(t.GetDrillValue()), 4)})
        else:
            s, e = t.GetStart(), t.GetEnd()
            tracks.append({'net': t.GetNetname(), 'layer': 'F' if t.GetLayer() == pcbnew.F_Cu else 'B',
                           'x1': round(T(s.x) - OX, 4), 'y1': round(T(s.y) - OY, 4),
                           'x2': round(T(e.x) - OX, 4), 'y2': round(T(e.y) - OY, 4),
                           'w': round(T(t.GetWidth()), 4)})
    bb = b.GetBoardEdgesBoundingBox()
    json.dump({'board': [T(bb.GetLeft()) - OX, T(bb.GetTop()) - OY, T(bb.GetRight()) - OX, T(bb.GetBottom()) - OY],
               'pads': pads, 'tracks': tracks, 'vias': vias}, open(out, 'w'), indent=0)
    print('extracted %d pads, %d tracks, %d vias' % (len(pads), len(tracks), len(vias)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
