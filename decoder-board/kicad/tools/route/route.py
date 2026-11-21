"""Small grid router for the decoder board (needs numpy).

    python route.py geometry.json routes.json

Reads the pads extracted by extract.py, adds the hand routes in HAND_TRACKS/HAND_VIAS (MIPI pairs,
thermal vias), then routes the other nets with A* on a 0.05 mm grid over F.Cu and B.Cu, with vias.
Clearances follow the project's net classes (0.15 mm, 0.2 mm for Power/Analog, 0.3 mm to the
board edge). Shapes that sit exactly on the grid (QFN pads, straight tracks) are treated exactly;
everything else gets a margin for the worst-case grid error. KiCad's DRC is the final check.
GND pads get a short trace to a via (or to the TP9950 exposed pad); vias join the In1/In2 planes.
Writes all tracks and vias (hand + routed) to routes.json for apply.py.

    python route.py geometry.json routes.json --reroute NET,NET,...

keeps every existing track and via (including edits made by hand in KiCad) and routes only the
listed nets, with their HAND_TRACKS. Nets in PAIRS are pulled alongside their partner.
"""
import heapq
import json
import math
import sys

import numpy as np

RES = 0.05
W, H = 28.0, 38.0
NX, NY = int(round(W / RES)) + 1, int(round(H / RES)) + 1
N2 = NX * NY
EDGE_CLR = 0.3
VIA_D, VIA_DRILL = 0.5, 0.3
HOLE_TO_HOLE = 0.25
VIA_COST = 30.0           # in grid steps (1.5 mm)
B_COST = 1.15             # prefer the top layer slightly
MARGIN = 0.036 / RES      # extra cells kept from off-grid shapes (worst-case grid error)
WINDOW = 4.0              # mm around each connection searched first

CLASS_02 = {'/VIN_RAW', '/5V', '/CAM_5V_F', '/CAM_5V', '/SW_1V2', '/1V2', '/3V3',      # Power
            '/VIDEO_IN', '/VID_REF', '/VIN1P', '/VIN1N', '/TERM', '/MIC_OUTP', '/MIC_OUTN',           # Analog
            '/MIC_P_C', '/MIC_N_C', '/MIC_P', '/MIC_N'}
WIDTH = {'/VIN_RAW': 0.4, '/5V': 0.4, '/CAM_5V_F': 0.4, '/CAM_5V': 0.4, '/SW_1V2': 0.4,
         '/1V2': 0.4, '/3V3': 0.3, '/1V8': 0.3}
DEFAULT_W = 0.2


def clr_of(net):
    return 0.2 if net in CLASS_02 else 0.15


# Hand routes: (net, layer, [points], width) and vias (net, x, y)
HAND_TRACKS = [
    ('/MIPI_D0P', 'F', [(12.10, 10.0), (12.10, 7.10), (11.95, 6.95), (11.95, 5.46)], 0.2),
    ('/MIPI_D0N', 'F', [(12.50, 10.0), (12.50, 7.00), (12.45, 6.95), (12.45, 5.46)], 0.2),
    ('/MIPI_CKP', 'F', [(12.90, 10.0), (12.90, 7.65), (13.45, 7.10), (13.45, 5.46)], 0.2),
    ('/MIPI_CKN', 'F', [(13.30, 10.0), (13.30, 7.80), (13.95, 7.15), (13.95, 5.46)], 0.2),
    # lane 1 crosses under lane 0 and the clock on B.Cu
    ('/MIPI_D1P', 'F', [(11.30, 10.0), (11.30, 8.45), (10.75, 7.90)], 0.2),
    ('/MIPI_D1P', 'B', [(10.75, 7.90), (11.40, 7.25), (14.60, 7.25), (14.95, 6.90)], 0.2),
    ('/MIPI_D1P', 'F', [(14.95, 6.90), (14.95, 5.46)], 0.2),
    ('/MIPI_D1N', 'F', [(11.70, 10.0), (11.70, 7.90), (11.60, 7.80)], 0.2),
    ('/MIPI_D1N', 'B', [(11.60, 7.80), (11.75, 7.65), (15.00, 7.65), (15.75, 6.90)], 0.2),
    ('/MIPI_D1N', 'F', [(15.75, 6.90), (15.45, 6.60), (15.45, 5.46)], 0.2),
    # crystal: XTI goes over Y1 to its upper-left pad (with C1 and the R11 option), XTO straight
    ('/XTI', 'F', [(9.0, 12.7), (6.6, 12.7), (6.6, 10.45), (3.72, 10.45), (3.72, 11.4)], 0.2),
    ('/XTI', 'F', [(4.12, 10.45), (4.12, 9.6), (3.6, 9.6), (3.0, 9.0), (3.0, 8.41)], 0.2),
    ('/XTO', 'F', [(9.0, 13.1), (5.48, 13.1), (5.48, 14.7), (5.08, 15.1)], 0.2),
    ('/MCLK', 'F', [(10.95, 5.46), (10.95, 6.45), (4.0, 6.45), (3.06, 7.39)], 0.2),
    # video input pair from pins 7/8 to the coupling caps (0.4 mm pitch, 0.2 mm gap)
    ('/VIN1P', 'F', [(12.10, 15.0), (12.10, 16.62)], 0.2),
    ('/VIN1N', 'F', [(12.50, 15.0), (12.50, 15.8), (13.30, 16.6), (13.30, 16.62)], 0.2),
    # TP9950 supply pins to their decoupling caps
    ('/VIO', 'F', [(10.9, 10.0), (10.9, 9.05), (10.35, 8.5), (7.78, 8.5)], 0.2),       # pin 27 -> C9
    ('/DVDD', 'F', [(10.5, 10.0), (10.5, 9.35), (10.4, 9.25), (7.78, 9.25)], 0.2),     # pin 28 -> C6
    ('/VIO', 'F', [(9.0, 12.3), (8.1, 12.3), (7.7, 11.9), (7.7, 11.7)], 0.2),          # pin 35 -> C10
    ('/VIO', 'F', [(14.0, 13.1), (15.45, 13.1)], 0.2),                                 # pin 14 -> C8
    ('/DVDD', 'F', [(14.0, 14.3), (15.45, 14.3)], 0.2),                                # pin 11 -> C5
    # I2C and reset to J1: SDA hops to B.Cu to cross SCL (TP9950 order SDA/SCL/RSTB,
    # Luckfox order SCL/SDA/RSTB)
    ('/SDA', 'F', [(14.0, 11.9), (14.5, 11.9), (14.8, 11.6), (14.8, 11.3)], 0.2),
    ('/SDA', 'B', [(14.8, 11.3), (16.95, 9.15), (16.95, 6.9)], 0.2),
    ('/SDA', 'F', [(16.95, 6.9), (16.95, 5.46)], 0.2),
    ('/SCL', 'F', [(14.0, 12.3), (14.6, 12.3), (16.45, 10.45), (16.45, 5.46)], 0.2),
    # RSTB turns as late as C8 allows, for more room from SCL
    ('/RSTB', 'F', [(14.0, 12.7), (14.85, 12.7), (17.95, 9.6), (17.95, 5.46)], 0.2),
    ('/RSTB', 'F', [(17.95, 9.6), (18.6, 10.25)], 0.2),                                # -> R4
    # mic outputs to the coupling caps as a pair (0.45 mm pitch): OUTP over the top of MK1's
    # pins and down the outer lane to C22, OUTN down the inner lane to C23
    ('/MIC_OUTP', 'F', [(26.25, 27.23), (26.25, 26.6), (23.8, 26.6), (23.35, 27.05), (23.35, 31.2),
                        (23.08, 31.2)], 0.2),
    ('/MIC_OUTN', 'F', [(24.55, 27.23), (23.8, 27.23), (23.8, 32.6), (23.08, 32.6)], 0.2),
]
HAND_VIAS = [
    ('/MIPI_D1P', 10.75, 7.90), ('/MIPI_D1P', 14.95, 6.90),
    ('/MIPI_D1N', 11.60, 7.80), ('/MIPI_D1N', 15.75, 6.90),
    ('/SDA', 14.8, 11.3), ('/SDA', 16.95, 6.9),
    # thermal vias in the TP9950 exposed pad
    ('/GND', 10.7, 11.7), ('/GND', 12.3, 11.7), ('/GND', 10.7, 13.3), ('/GND', 12.3, 13.3),
]

# the buck's switch node: other nets keep this far from it on F.Cu (B.Cu is shielded by the planes)
NOISY_NET, NOISY_KEEP = '/SW_1V2', 4.0
BUCK_NETS = {'/SW_1V2', '/1V2', '/5V', '/FB_1V2', '/GND'}

# second net of a pair: routed after its partner, with a cheap lane alongside the partner's tracks
PAIRS = {'/MIC_OUTN': '/MIC_OUTP', '/MIC_N': '/MIC_P', '/VID_REF': '/VIDEO_IN'}
PAIR_LANE = 0.5           # mm beyond the partner's copper edge that counts as "alongside"
PAIR_DISCOUNT = 0.5       # step cost inside that lane

ORDER = [  # routing order; nets not listed are routed afterwards, GND last (fanout)
    '/XTI', '/XTO', '/VIN1P', '/VIN1N', '/VIDEO_IN', '/VID_REF', '/TERM',
    '/VIO', '/DVDD', '/AVDD33', '/AVDD12', '/RSTB', '/SCL', '/SDA', '/MCLK', '/LF_3V3',
    '/SW_1V2', '/FB_1V2', '/1V2', '/1V8', '/3V3', '/Q1_G', '/VIN_RAW', '/5V', '/CAM_5V_F', '/CAM_5V',
    '/2V8_MIC', '/MIC_OUTP', '/MIC_OUTN', '/MIC_P_C', '/MIC_N_C', '/MIC_P', '/MIC_N',
]


def cell(v):
    return int(round(v / RES))


_DISKS = {}


def disk(r):
    """Offsets (dy, dx) with distance < r cells."""
    key = round(r, 4)
    if key not in _DISKS:
        n = int(math.ceil(r))
        _DISKS[key] = [(dy, dx) for dy in range(-n, n + 1) for dx in range(-n, n + 1)
                       if dy * dy + dx * dx < r * r - 1e-9]
    return _DISKS[key]


def dilate(mask, r):
    ny, nx = mask.shape
    out = np.zeros_like(mask)
    if not mask.any():
        return out
    for dy, dx in disk(r):
        ys, ye = max(0, dy), ny + min(0, dy)
        xs, xe = max(0, dx), nx + min(0, dx)
        if ys < ye and xs < xe:
            out[ys:ye, xs:xe] |= mask[ys - dy:ye - dy, xs - dx:xe - dx]
    return out


def _inside(pts, gx, gy):
    inside = np.zeros(gx.shape, bool)
    on = np.zeros(gx.shape, bool)
    n = len(pts)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        cond = ((ay > gy) != (by > gy))
        with np.errstate(divide='ignore', invalid='ignore'):
            xint = (bx - ax) * (gy - ay) / (by - ay) + ax
        inside ^= cond & (gx < xint)
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy or 1e-12
        t = np.clip(((gx - ax) * dx + (gy - ay) * dy) / L2, 0, 1)
        d2 = (gx - (ax + t * dx)) ** 2 + (gy - (ay + t * dy)) ** 2
        on |= d2 < 1e-8
    return inside | on


def raster_poly(pts):
    """Cells whose centre or any corner is inside or on the polygon (errs towards more copper)."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, x1 = max(0, int(math.floor(min(xs) / RES)) - 1), min(NX - 1, int(math.ceil(max(xs) / RES)) + 1)
    y0, y1 = max(0, int(math.floor(min(ys) / RES)) - 1), min(NY - 1, int(math.ceil(max(ys) / RES)) + 1)
    gx0, gy0 = np.meshgrid(np.arange(x0, x1 + 1) * RES, np.arange(y0, y1 + 1) * RES)
    acc = np.zeros(gx0.shape, bool)
    for ox, oy in ((0, 0), (-RES / 2, -RES / 2), (RES / 2, -RES / 2), (-RES / 2, RES / 2), (RES / 2, RES / 2)):
        acc |= _inside(pts, gx0 + ox, gy0 + oy)
    return (y0, x0), acc


def raster_exact(pts):
    """Cells whose centre is inside or on an axis-aligned rectangle with edges on the grid."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, x1 = int(round(min(xs) / RES)), int(round(max(xs) / RES))
    y0, y1 = int(round(min(ys) / RES)), int(round(max(ys) / RES))
    return (y0, x0), np.ones((y1 - y0 + 1, x1 - x0 + 1), bool)


def raster_capsule(x1, y1, x2, y2, r):
    x0c, x1c = max(0, cell(min(x1, x2) - r) - 1), min(NX - 1, cell(max(x1, x2) + r) + 1)
    y0c, y1c = max(0, cell(min(y1, y2) - r) - 1), min(NY - 1, cell(max(y1, y2) + r) + 1)
    gx, gy = np.meshgrid(np.arange(x0c, x1c + 1) * RES, np.arange(y0c, y1c + 1) * RES)
    dx, dy = x2 - x1, y2 - y1
    L2 = dx * dx + dy * dy or 1e-12
    t = np.clip(((gx - x1) * dx + (gy - y1) * dy) / L2, 0, 1)
    d2 = (gx - (x1 + t * dx)) ** 2 + (gy - (y1 + t * dy)) ** 2
    return (y0c, x0c), d2 <= r * r + 1e-9


def on_grid(v):
    return abs(v / RES - round(v / RES)) < 1e-4


class Board:
    def __init__(self, geo):
        nets = sorted({p['net'] for p in geo['pads'] if p['net']} | {'/GND'})
        self.ids = {n: i + 1 for i, n in enumerate(nets)}
        self.cn = {L: np.zeros((NY, NX), np.int32) for L in 'FB'}      # net id of copper, -1 edge/hole
        self.exact = {L: np.zeros((NY, NX), bool) for L in 'FB'}       # copper raster is exact here
        self.cls = {L: np.zeros((NY, NX), np.float32) for L in 'FB'}   # clearance the copper asks for
        self.via_forbid = np.zeros((NY, NX), bool)
        self.padcells = []
        self.tracks, self.vias = [], []
        self.routed = {}
        self.items = {}                  # net -> list of cell sets (one per polyline or via)
        # board edge: one cell wide, asks for the edge clearance
        for L in 'FB':
            for sl in (np.s_[0, :], np.s_[NY - 1, :], np.s_[:, 0], np.s_[:, NX - 1]):
                self.cn[L][sl] = -1
                self.exact[L][sl] = True
                self.cls[L][sl] = EDGE_CLR
        e = int(math.ceil((VIA_D / 2 + EDGE_CLR) / RES))
        self.via_forbid[:e, :] = True
        self.via_forbid[-e:, :] = True
        self.via_forbid[:, :e] = True
        self.via_forbid[:, -e:] = True
        uid = len(self.ids) + 1
        anypad = np.zeros((NY, NX), bool)
        for p in geo['pads']:
            net = p['net'] or ''
            if net:
                nid = self.ids[net]
            else:
                nid, uid = uid, uid + 1
            cells = set()
            for L, polys in p['layers']:
                for poly in polys:
                    grid_rect = (len(poly) == 4 and all(on_grid(c) for q in poly for c in q)
                                 and len({q[0] for q in poly}) == 2 and len({q[1] for q in poly}) == 2)
                    (y0, x0), m = raster_exact(poly) if grid_rect else raster_poly(poly)
                    win = np.s_[y0:y0 + m.shape[0], x0:x0 + m.shape[1]]
                    self.cn[L][win][m] = nid
                    self.exact[L][win][m] = grid_rect
                    self.cls[L][win][m] = clr_of(net)
                    anypad[win] |= m
                    li = 0 if L == 'F' else 1
                    ys, xs = np.nonzero(m)
                    cells |= {li * N2 + (int(y) + y0) * NX + (int(x) + x0) for y, x in zip(ys, xs)}
            if p['hole']:
                (y0, x0), m = raster_capsule(p['x'], p['y'], p['x'], p['y'],
                                             p['hole'] + VIA_DRILL / 2 + HOLE_TO_HOLE)
                self.via_forbid[y0:y0 + m.shape[0], x0:x0 + m.shape[1]] |= m
                if not p['layers']:          # NPTH: copper keeps the hole clearance
                    for L in 'FB':
                        (y0, x0), m = raster_capsule(p['x'], p['y'], p['x'], p['y'], p['hole'])
                        win = np.s_[y0:y0 + m.shape[0], x0:x0 + m.shape[1]]
                        self.cn[L][win][m] = -1
                        self.cls[L][win][m] = 0.2
            if net:
                self.padcells.append((net, cells, p))
        # no via touching any pad (avoids vias in small pads)
        self.via_forbid |= dilate(anypad, VIA_D / 2 / RES + 1)
        sw = self.ids.get(NOISY_NET)
        self.sw_keep = dilate(self.cn['F'] == sw, NOISY_KEEP / RES) if sw else None

    # --- copper bookkeeping -----------------------------------------------------------
    def _mark(self, net, L, m, y0, x0, exact):
        win = np.s_[y0:y0 + m.shape[0], x0:x0 + m.shape[1]]
        self.cn[L][win][m] = self.ids[net]
        self.exact[L][win][m] = exact
        self.cls[L][win][m] = clr_of(net)
        li = 0 if L == 'F' else 1
        ys, xs = np.nonzero(m)
        cells = {li * N2 + (int(y) + y0) * NX + (int(x) + x0) for y, x in zip(ys, xs)}
        self.routed.setdefault(net, set()).update(cells)
        return cells

    def add_track(self, net, L, pts, w):
        item = set()
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            straight = (abs(x1 - x2) < 1e-9 or abs(y1 - y2) < 1e-9)
            exact = straight and all(on_grid(v) for v in (x1, y1, x2, y2)) and on_grid(w / 2)
            (y0, x0), m = raster_capsule(x1, y1, x2, y2, w / 2)
            item |= self._mark(net, L, m, y0, x0, exact)
            for ex, ey in ((x1, y1), (x2, y2)):      # round ends rasterize inexactly
                (y0, x0), m = raster_capsule(ex, ey, ex, ey, w / 2)
                item |= self._mark(net, L, m, y0, x0, False)
            self.tracks.append({'net': net, 'layer': L, 'x1': round(x1, 4), 'y1': round(y1, 4),
                                'x2': round(x2, 4), 'y2': round(y2, 4), 'w': w})
        self.items.setdefault(net, []).append(item)

    def add_via(self, net, x, y):
        item = set()
        for L in 'FB':
            (y0, x0), m = raster_capsule(x, y, x, y, VIA_D / 2)
            item |= self._mark(net, L, m, y0, x0, False)
        self.items.setdefault(net, []).append(item)
        (y0, x0), m = raster_capsule(x, y, x, y, VIA_DRILL + HOLE_TO_HOLE)
        self.via_forbid[y0:y0 + m.shape[0], x0:x0 + m.shape[1]] |= m
        self.vias.append({'net': net, 'x': round(x, 4), 'y': round(y, 4), 'd': VIA_D, 'drill': VIA_DRILL})

    def masks(self, net, w, win=None):
        """Blocked cells per layer and legal via cells for a track of width w, inside win."""
        nid = self.ids[net]
        own = clr_of(net)
        if win is None:
            win = (0, NY, 0, NX)
        ya, yb, xa, xb = win
        pad = int(math.ceil((max(w, VIA_D) / 2 + EDGE_CLR) / RES + MARGIN)) + 2
        y0, y1, x0, x1 = max(0, ya - pad), min(NY, yb + pad), max(0, xa - pad), min(NX, xb + pad)
        crop = np.s_[ya - y0:yb - y0, xa - x0:xb - x0]
        block, vok = {}, np.zeros((NY, NX), bool)
        vsub = ~self.via_forbid[y0:y1, x0:x1]
        for L in 'FB':
            cn = self.cn[L][y0:y1, x0:x1]
            ex = self.exact[L][y0:y1, x0:x1]
            cl = self.cls[L][y0:y1, x0:x1]
            other = (cn != 0) & (cn != nid)
            b = np.zeros(other.shape, bool)
            for c in (0.15, 0.2, EDGE_CLR):
                sel = other & (np.abs(cl - c) < 1e-3)
                if not sel.any():
                    continue
                need = max(own, c)
                for exflag, extra in ((True, 0.0), (False, MARGIN)):
                    s2 = sel & (ex == exflag)
                    if s2.any():
                        b |= dilate(s2, (w / 2 + need) / RES + extra)
                        vsub &= ~dilate(s2, (VIA_D / 2 + need) / RES + extra)
            full = np.ones((NY, NX), bool)
            full[ya:yb, xa:xb] = b[crop]
            if L == 'F' and self.sw_keep is not None and net not in BUCK_NETS:
                full |= self.sw_keep
            block[L] = full
        vok[ya:yb, xa:xb] = vsub[crop]
        return block, vok


def window(cells_a, cells_b, extra):
    ys = [(c % N2) // NX for c in cells_a] + [(c % N2) // NX for c in cells_b]
    xs = [(c % N2) % NX for c in cells_a] + [(c % N2) % NX for c in cells_b]
    e = int(extra / RES)
    return (max(0, min(ys) - e), min(NY, max(ys) + e + 1), max(0, min(xs) - e), min(NX, max(xs) + e + 1))


def astar(starts, goals, goal_xy, block, vok, allow_via=True, max_exp=800000, cheap=None):
    blk = [bytes(block['F'].ravel()), bytes(block['B'].ravel())]
    hk = PAIR_DISCOUNT if cheap is not None else 1.0
    vk = bytes(vok.ravel())
    gx, gy = cell(goal_xy[0]), cell(goal_xy[1])

    def h(i):
        r = i % N2
        y, x = divmod(r, NX)
        dx, dy = abs(x - gx), abs(y - gy)
        return (max(dx, dy) + 0.4142 * min(dx, dy)) * hk

    g, par, heap = {}, {}, []
    for s in starts:
        g[s] = 0.0
        par[s] = -1
        heapq.heappush(heap, (h(s), 0.0, s))
    moves = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
             (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]
    exp = 0
    while heap:
        f, gc, i = heapq.heappop(heap)
        if gc > g.get(i, 1e18):
            continue
        if i in goals:
            path = []
            while i != -1:
                path.append(i)
                i = par[i]
            return path[::-1]
        exp += 1
        if exp > max_exp:
            return None
        li, r = divmod(i, N2)
        y, x = divmod(r, NX)
        lc = B_COST if li else 1.0
        bl = blk[li]
        for dx, dy, c in moves:
            nx, ny = x + dx, y + dy
            if not (0 <= nx < NX and 0 <= ny < NY):
                continue
            if bl[ny * NX + nx]:
                continue
            if dx and dy and (bl[y * NX + nx] or bl[ny * NX + x]):
                continue
            j = li * N2 + ny * NX + nx
            ng = gc + c * lc * (PAIR_DISCOUNT if cheap is not None and cheap[j] else 1.0)
            if ng < g.get(j, 1e18):
                g[j] = ng
                par[j] = i
                heapq.heappush(heap, (ng + h(j), ng, j))
        if allow_via and vk[r] and not blk[1 - li][r]:
            j = (1 - li) * N2 + r
            ng = gc + VIA_COST
            if ng < g.get(j, 1e18):
                g[j] = ng
                par[j] = i
                heapq.heappush(heap, (ng + h(j), ng, j))
    return None


def los(block_l, a, b):
    """Straight 0/45/90 degree line between cells a=(y,x), b=(y,x) free of blocked cells."""
    (y1, x1), (y2, x2) = a, b
    dy, dx = y2 - y1, x2 - x1
    if not (dy == 0 or dx == 0 or abs(dy) == abs(dx)):
        return False
    n = max(abs(dy), abs(dx))
    sy = (dy > 0) - (dy < 0)
    sx = (dx > 0) - (dx < 0)
    for k in range(1, n):
        if block_l[y1 + sy * k, x1 + sx * k]:
            return False
    return True


def path_to_geometry(path, block):
    """Split a cell path into per-layer polylines (simplified to 0/45/90) and via points."""
    runs, cur = [], [path[0]]
    for i in path[1:]:
        if i // N2 != cur[-1] // N2:
            runs.append(cur)
            cur = [i]
        else:
            cur.append(i)
    runs.append(cur)
    polys, vias = [], []
    for k, run in enumerate(runs):
        L = 'FB'[run[0] // N2]
        pts = [divmod(int(i) % N2, NX) for i in run]
        keep = [pts[0]]
        a = 0
        while a < len(pts) - 1:
            b = len(pts) - 1
            while b > a + 1 and not los(block[L], pts[a], pts[b]):
                b -= 1
            keep.append(pts[b])
            a = b
        xy = [(x * RES, y * RES) for y, x in keep]
        if len(xy) > 1:
            polys.append((L, xy))
        if k < len(runs) - 1:
            vias.append(xy[-1])
    return polys, vias


def free(cells, block):
    fb = (block['F'].ravel(), block['B'].ravel())
    return {c for c in cells if not fb[c // N2][c % N2]}


def connect(bd, net, starts_all, cells, target_xy, w, allow_via=True, extra_goals=None, cheap=None):
    """Try a window around the connection first, then the whole board."""
    for win in (window(starts_all, cells or starts_all, WINDOW), None):
        block, vok = bd.masks(net, w, win)
        st = free(starts_all, block)
        gl = free(cells, block)
        if extra_goals is not None:
            fb = (block['F'].ravel(), block['B'].ravel())
            gl |= {c for c in extra_goals(vok) if not fb[c // N2][c % N2]}
        if not st or not gl:
            continue
        path = astar(st, gl, target_xy, block, vok, allow_via=allow_via, cheap=cheap)
        if path:
            return path, block
    return None, None


def components(bd, net):
    """Groups of cells (pads plus routed items) of a net that are already connected."""
    parts = [set(c) for n, c, _ in bd.padcells if n == net] + [set(i) for i in bd.items.get(net, [])]
    merged = True
    while merged:
        merged = False
        out = []
        while parts:
            cur = parts.pop()
            changed = True
            while changed:
                changed = False
                keep = []
                for q in parts:
                    if cur & q:
                        cur |= q
                        changed = True
                    else:
                        keep.append(q)
                parts = keep
            out.append(cur)
        parts = out
    return parts


def pair_lane(bd, partner):
    """Cells within PAIR_LANE of the partner's routed copper, per layer, as bytes over both layers."""
    cells = bd.routed.get(partner, set())
    if not cells:
        return None
    out = []
    for li in (0, 1):
        m = np.zeros(N2, bool)
        idx = [c - li * N2 for c in cells if c // N2 == li]
        m[idx] = True
        m = m.reshape(NY, NX)
        out.append((dilate(m, PAIR_LANE / RES) & ~m).ravel())
    return bytes(np.concatenate(out))


def route_net(bd, net, w):
    cheap = pair_lane(bd, PAIRS[net]) if net in PAIRS else None
    pads = [(cells, p) for n, cells, p in bd.padcells if n == net]
    if len(pads) < 2:
        return True, 'single pad'
    comps = components(bd, net)
    first = pads[0][0]
    tree = next(c for c in comps if c & first)
    rest = [c for c in comps if not (c & tree)]
    pinfo = {id(c): next((p for cc, p in pads if cc & c), None) for c in rest}
    while rest:
        tl = list(tree)[::5] or list(tree)

        def centre(c):
            ys = [(t % N2) // NX for t in c]
            xs = [(t % N2) % NX for t in c]
            return sum(xs) / len(xs), sum(ys) / len(ys)

        def dist(c):
            cx, cy = centre(c)
            return min(abs((t % N2) % NX - cx) + abs((t % N2) // NX - cy) for t in tl)
        rest.sort(key=dist)
        cells = rest.pop(0)
        cx, cy = centre(cells)
        done = False
        for width in sorted({w, DEFAULT_W}, reverse=True):
            path, block = connect(bd, net, tree, cells, (cx * RES, cy * RES), width, cheap=cheap)
            if path:
                n0 = len(bd.items.get(net, []))
                polys, vias = path_to_geometry(path, block)
                for L, xy in polys:
                    bd.add_track(net, L, xy, width)
                for x, y in vias:
                    bd.add_via(net, x, y)
                for it in bd.items[net][n0:]:
                    tree |= it
                tree |= cells
                done = True
                break
        if not done:
            p = pinfo.get(id(cells))
            return False, ('%s %s unreachable' % (p['ref'], p['num'])) if p else 'track piece unreachable'
        rest = [c for c in rest if not (c & tree)]
    return True, ''


EARLY_GND = {('U2', '2'), ('C4', '2'), ('C3', '2'), ('C19', '2'), ('C24', '2'),
             ('C11', '2'), ('C10', '2'), ('C6', '2'), ('C9', '2')}


def fanout_gnd(bd, only=None):
    fails = []
    anchor = set()
    for net, cells, p in bd.padcells:
        if net == '/GND' and p['ref'] == 'U1' and p['num'] == '41':
            anchor |= cells
    for net, cells, p in bd.padcells:
        if net != '/GND' or p['hole'] or (p['ref'] == 'U1' and p['num'] == '41'):
            continue                       # through-hole GND pads reach the planes directly
        if only is not None and (p['ref'], p['num']) not in only:
            continue
        if cells & bd.routed.get('/GND', set()):
            continue
        done = False
        for width in (0.3, 0.2):
            ag = (anchor | bd.routed.get('/GND', set())) - cells

            def extra(vok, ag=ag):
                ys, xs = np.nonzero(vok)
                return {int(y) * NX + int(x) for y, x in zip(ys, xs)} | ag
            path, block = connect(bd, '/GND', cells, set(), (p['x'], p['y']), width,
                                  allow_via=False, extra_goals=extra)
            if not path:
                continue
            polys, _ = path_to_geometry(path, block)
            for L, xy in polys:
                bd.add_track('/GND', L, xy, width)
            if path[-1] not in ag:
                y, x = divmod(path[-1] % N2, NX)
                bd.add_via('/GND', x * RES, y * RES)
            done = True
            break
        if not done:
            fails.append('%s %s' % (p['ref'], p['num']))
    return fails


def run(geo, order):
    bd = Board(geo)
    for net, L, pts, w in HAND_TRACKS:
        bd.add_track(net, L, pts, w)
    for net, x, y in HAND_VIAS:
        bd.add_via(net, x, y)
    early = fanout_gnd(bd, EARLY_GND)
    failed = []
    for net in order:
        if net.startswith('/MIPI'):
            continue
        ok, why = route_net(bd, net, WIDTH.get(net, DEFAULT_W))
        if not ok:
            failed.append((net, why))
    return bd, failed, early + fanout_gnd(bd)


def run_reroute(geo, order):
    """Keep the board's tracks and vias except those of the nets in order; route those."""
    bd = Board(geo)
    nets = set(order)
    for t in geo['tracks']:
        if t['net'] not in nets:
            bd.add_track(t['net'], t['layer'], [(t['x1'], t['y1']), (t['x2'], t['y2'])], t['w'])
    for v in geo['vias']:
        if v['net'] not in nets:
            bd.add_via(v['net'], v['x'], v['y'])
            bd.vias[-1].update(d=v['d'], drill=v['drill'])
    for net, L, pts, w in HAND_TRACKS:
        if net in nets:
            bd.add_track(net, L, pts, w)
    for net, x, y in HAND_VIAS:
        if net in nets:
            bd.add_via(net, x, y)
    failed = []
    for net in order:
        ok, why = route_net(bd, net, WIDTH.get(net, DEFAULT_W))
        if not ok:
            failed.append((net, why))
    return bd, failed, fanout_gnd(bd)


def main(geo_path, out_path, tries=6, reroute=None):
    geo = json.load(open(geo_path))
    probe = Board(geo)
    hand = {n for n, _, _, _ in HAND_TRACKS if n.startswith('/MIPI')}   # fully hand-routed
    nets = [n for n in ORDER if n in probe.ids]
    others = sorted({n for n, _, _ in probe.padcells} - set(nets) - {'/GND'} - hand)
    order = [n for n in nets + others if not n.startswith('unconnected')]
    if reroute:
        unknown = set(reroute) - set(probe.ids)
        if unknown:
            sys.exit('unknown nets: %s' % sorted(unknown))
        order = [n for n in order if n in reroute]
    best = None
    for k in range(tries):
        bd, failed, gnd_failed = (run_reroute if reroute else run)(geo, order)
        score = len(failed) * 10 + len(gnd_failed)
        print('try %d: failed %s, GND fanout failed %s' % (k + 1, [f for f, _ in failed], gnd_failed), flush=True)
        if best is None or score < best[0]:
            best = (score, bd, failed, gnd_failed)
        if score == 0:
            break
        first = [f for f, _ in failed]
        order = first + [n for n in order if n not in first]
    _, bd, failed, gnd_failed = best
    print('kept: failed %s, GND fanout failed %s; %d tracks, %d vias' % (
        failed, gnd_failed, len(bd.tracks), len(bd.vias)))
    json.dump({'tracks': bd.tracks, 'vias': bd.vias, 'failed': failed, 'gnd_failed': gnd_failed},
              open(out_path, 'w'), indent=0)


if __name__ == '__main__':
    rr = None
    if len(sys.argv) > 4 and sys.argv[3] == '--reroute':
        rr = set(sys.argv[4].split(','))
    main(sys.argv[1], sys.argv[2], reroute=rr)
