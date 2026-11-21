"""Turn the luci3D logo bitmap into silkscreen polygons (needs numpy and Pillow).

    python vectorize.py ../../art/luci3d-logo.png logo.json --height 12

Keeps the white strokes only (the red mark is not part of the logo), thickens strokes thinner
than the silkscreen minimum (0.15 mm at JLC) and traces the outlines into polygons in mm,
centred on the logo, y down. place_logo.py puts them on the board.
"""
import argparse
import json

import numpy as np
from PIL import Image

MIN_STROKE = 0.16      # mm, a little over JLC's 0.15 mm silkscreen minimum


def shift_or(m, r, op):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r:
                continue
            s = np.roll(np.roll(m, dy, 0), dx, 1)
            out = (out | s) if op == 'or' else (out & s)
    return out


def trace(m):
    """Pixel-edge loops with the strokes on the left (outer loops have negative area)."""
    h, w = m.shape
    p = np.pad(m, 1)
    nxt = {}
    for r, c in zip(*np.nonzero(m)):
        pr, pc = r + 1, c + 1
        if not p[pr - 1, pc]:
            nxt.setdefault((c + 1, r), []).append((c, r))
        if not p[pr + 1, pc]:
            nxt.setdefault((c, r + 1), []).append((c + 1, r + 1))
        if not p[pr, pc - 1]:
            nxt.setdefault((c, r), []).append((c, r + 1))
        if not p[pr, pc + 1]:
            nxt.setdefault((c + 1, r + 1), []).append((c + 1, r))
    loops = []
    while nxt:
        start = next(iter(nxt))
        loop, v = [start], start
        while True:
            outs = nxt[v]
            u = outs.pop()
            if not outs:
                del nxt[v]
            if u == start:
                break
            loop.append(u)
            v = u
        loops.append(loop)
    return loops


def dp(pts, tol):
    """Douglas-Peucker on an open polyline."""
    if len(pts) < 3:
        return pts
    a, b = np.array(pts[0], float), np.array(pts[-1], float)
    d = b - a
    n = np.hypot(*d)
    arr = np.array(pts, float)
    if n == 0:
        dist = np.hypot(*(arr - a).T)
    else:
        dist = np.abs(d[0] * (arr[:, 1] - a[1]) - d[1] * (arr[:, 0] - a[0])) / n
    i = int(dist.argmax())
    if dist[i] > tol:
        return dp(pts[:i + 1], tol)[:-1] + dp(pts[i:], tol)
    return [pts[0], pts[-1]]


def simplify_loop(loop, tol):
    # split at the point farthest from the first one so both halves are open polylines
    a = np.array(loop[0], float)
    far = int(np.hypot(*(np.array(loop, float) - a).T).argmax())
    first = dp(loop[:far + 1], tol)
    second = dp(loop[far:] + [loop[0]], tol)
    return first[:-1] + second[:-1]


def area(loop):
    x = np.array([p[0] for p in loop], float)
    y = np.array([p[1] for p in loop], float)
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(np.roll(x, -1), y))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('out')
    ap.add_argument('--height', type=float, default=12.0, help='logo height in mm')
    a = ap.parse_args()
    im = np.asarray(Image.open(a.image).convert('RGB')).astype(int)
    m = (im[..., 0] > 160) & (im[..., 1] > 160) & (im[..., 2] > 160)    # white only
    ys, xs = np.nonzero(m)
    m = m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    mm_px = a.height / m.shape[0]
    # strokes thinner than MIN_STROKE: what an opening of that width removes gets thickened
    r = max(1, int(round(MIN_STROKE / mm_px / 2)))
    m = np.pad(m, r + 2)
    opened = shift_or(shift_or(m, r, 'and'), r, 'or')
    thin = m & ~opened
    m = m | shift_or(thin, max(1, r // 2 + 1), 'or')
    loops = [simplify_loop(lp, 0.8) for lp in trace(m)]
    loops = [lp for lp in loops if len(lp) >= 3 and abs(area(lp)) > 4]
    h, w = m.shape
    to_mm = lambda lp: [[round((x - w / 2) * mm_px, 4), round((y - h / 2) * mm_px, 4)] for x, y in lp]
    outer = [to_mm(lp) for lp in loops if area(lp) < 0]
    holes = [to_mm(lp) for lp in loops if area(lp) > 0]
    json.dump({'height_mm': a.height, 'width_mm': round(w * mm_px, 3), 'outer': outer, 'holes': holes},
              open(a.out, 'w'), indent=0)
    print('%.2f x %.2f mm, %d outlines, %d holes, %d points, thin strokes thickened: %d px' % (
        w * mm_px, h * mm_px, len(outer), len(holes), sum(map(len, loops)), int(thin.sum())))


if __name__ == '__main__':
    main()
