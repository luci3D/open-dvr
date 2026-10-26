#!/usr/bin/env python3
"""Decodes build/host/level_*.mp3 and compares output level with the input level."""
import glob, os
import av
import numpy as np
for p in sorted(glob.glob('build/host/level_*.mp3')):
    with av.open(p) as c:
        x = np.concatenate([f.to_ndarray().reshape(-1) for f in c.decode(audio=0)]).astype(np.float64)
    if np.abs(x).max() <= 1.5:
        x *= 32767
    rms = np.sqrt((x[32000:] ** 2).mean())
    print('%-28s decoded RMS %8.1f (%6.1f dBFS), peak %6.0f' % (os.path.basename(p), rms,
          20 * np.log10(max(rms, 1e-3) / 32767), np.abs(x).max()))
