#!/usr/bin/env python3
"""Checks raw .h264 clips from the board (Windows Python with av, numpy, pillow).

For each clip: frame count, whether neighbouring frames are distinct (the stock firmware
repeated each picture twice), and whether the colour planes carry data. Saves one RGB
frame per clip next to it as <clip>.png.

    python scripts/analyze_clip.py results/round12/*.h264
"""
import os
import sys

import av
import numpy as np
from PIL import Image


def analyze(path, max_frames=300):
    grays, sample = [], None
    with av.open(path) as c:
        for i, f in enumerate(c.decode(video=0)):
            grays.append(f.to_ndarray(format='gray').astype(np.float32))
            if i == 100 or sample is None:
                sample = f
            if len(grays) >= max_frames:
                break
    d = np.abs(np.diff(np.array(grays), axis=0)).mean(axis=(1, 2)) if len(grays) > 1 else np.zeros(1)
    p = sample.to_ndarray(format='yuv420p')
    h, w = sample.height, sample.width
    y = p[:h].astype(np.float32)
    u = p[h:h + h // 4].astype(np.float32)
    v = p[h + h // 4:].astype(np.float32)
    Image.fromarray(sample.to_ndarray(format='rgb24')).save(os.path.splitext(path)[0] + '.png')
    colour = 'yes' if u.std() > 2 or v.std() > 2 else 'NO (flat)'
    print('%-28s %4d frames %dx%d | diffs even %.2f odd %.2f dup %d | Y %.0f/%.0f U %.0f/%.0f V %.0f/%.0f '
          '| colour %s | Y col alternation %.1f'
          % (os.path.basename(path), len(grays), w, h, d[0::2].mean(), d[1::2].mean(), int((d < 0.5).sum()),
             y.mean(), y.std(), u.mean(), u.std(), v.mean(), v.std(), colour,
             np.abs(y[:, 0::2] - y[:, 1::2]).mean()))


if __name__ == '__main__':
    for clip in sys.argv[1:]:
        try:
            analyze(clip)
        except Exception as e:  # empty or broken clip
            print('%-28s could not decode: %s' % (os.path.basename(clip), e))
