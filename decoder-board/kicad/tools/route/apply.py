"""Write routes.json (from route.py) into the board, replacing all tracks and vias.

Run with KiCad's Python:
    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" apply.py board.kicad_pcb routes.json
    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" apply.py board.kicad_pcb --moves-only

MOVES are placement tweaks made for routing (they are also reflected in gen_pcb.py).
"""
import json
import sys

import pcbnew

OX, OY = 100.0, 100.0
MOVES = {  # ref: (x, y, rotation) placement tweaks for routing; keep gen_pcb.py PLACE in step
}


def pt(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))


def main(pcb, routes):
    b = pcbnew.LoadBoard(pcb)
    nets = {str(k): v for k, v in b.GetNetsByName().items()}   # before any removal: KiCad's Python breaks after one
    for fp in b.GetFootprints():
        if fp.GetReference() in MOVES:
            x, y, r = MOVES[fp.GetReference()]
            fp.SetPosition(pt(x, y))
            fp.SetOrientationDegrees(r)
    for t in list(b.GetTracks()):
        b.Remove(t)
    if routes:
        d = json.load(open(routes))
        for t in d['tracks']:
            tr = pcbnew.PCB_TRACK(b)
            tr.SetStart(pt(t['x1'], t['y1']))
            tr.SetEnd(pt(t['x2'], t['y2']))
            tr.SetWidth(pcbnew.FromMM(t['w']))
            tr.SetLayer(pcbnew.F_Cu if t['layer'] == 'F' else pcbnew.B_Cu)
            tr.SetNet(nets[t['net']])
            b.Add(tr)
        for v in d['vias']:
            via = pcbnew.PCB_VIA(b)
            via.SetPosition(pt(v['x'], v['y']))
            via.SetWidth(pcbnew.FromMM(v['d']))
            via.SetDrill(pcbnew.FromMM(v['drill']))
            via.SetNet(nets[v['net']])
            b.Add(via)
        print('applied %d tracks, %d vias' % (len(d['tracks']), len(d['vias'])))
    pcbnew.SaveBoard(pcb, b)


if __name__ == '__main__':
    main(sys.argv[1], None if sys.argv[2] == '--moves-only' else sys.argv[2])
