#!/usr/bin/env python3
"""Checks an MPEG-TS file from the recorder: packet structure, continuity counters,
timestamps and that every frame decodes.   python scripts/verify_ts.py file.ts"""
import sys

import av
import numpy as np

path = sys.argv[1]
data = open(path, 'rb').read()
n = len(data) // 188
bad_sync = sum(1 for i in range(n) if data[i * 188] != 0x47)
cc, cc_err, pids = {}, 0, {}
for i in range(n):
    p = data[i * 188:(i + 1) * 188]
    pid = ((p[1] & 0x1F) << 8) | p[2]
    pids[pid] = pids.get(pid, 0) + 1
    if p[3] & 0x10:  # has payload
        c = p[3] & 0x0F
        if pid in cc and c != (cc[pid] + 1) % 16:
            cc_err += 1
        cc[pid] = c
print('%s: %d bytes, %d packets, tail %d, bad sync %d, continuity errors %d, pids %s'
      % (path, len(data), n, len(data) % 188, bad_sync, cc_err, {hex(k): v for k, v in pids.items()}))

with av.open(path) as c:
    s = c.streams.video[0]
    print('format %s, codec %s %dx%d, SAR %s, DAR %s, avg rate %s'
          % (c.format.name, s.codec_context.name, s.width, s.height, s.codec_context.sample_aspect_ratio,
             s.codec_context.display_aspect_ratio, s.average_rate))
    pts = []
    frames = 0
    for f in c.decode(video=0):
        frames += 1
        if f.pts is not None:
            pts.append(float(f.pts * f.time_base))
    d = np.diff(pts)
    print('decoded %d frames, duration %.2f s, frame interval median %.4f s (min %.4f max %.4f)'
          % (frames, pts[-1] - pts[0], np.median(d), d.min(), d.max()))

# audio track, if any: decode it, report duration, start offset vs video, level, main frequency
with av.open(path) as c:
    if c.streams.audio:
        a = c.streams.audio[0]
        vstart = float(c.streams.video[0].start_time * c.streams.video[0].time_base)
        pcm, first = [], None
        for f in c.decode(audio=0):
            if first is None and f.pts is not None:
                first = float(f.pts * f.time_base)
            pcm.append(f.to_ndarray().reshape(-1))
        x = np.concatenate(pcm).astype(np.float64)
        if x.dtype.kind == 'f' or np.abs(x).max() <= 1.5:
            x = x * 32767
        rate = a.codec_context.sample_rate
        spec = np.abs(np.fft.rfft(x[: rate * 4] * np.hanning(min(len(x), rate * 4))))
        peak = np.argmax(spec[20:]) + 20
        print('audio %s %d Hz %d ch: %.2f s decoded, starts %+.3f s from video, RMS %.0f, main tone %.0f Hz'
              % (a.codec_context.name, rate, a.codec_context.channels, len(x) / rate, first - vstart,
                 np.sqrt((x ** 2).mean()), peak * rate / (2 * (len(spec) - 1))))
    else:
        print('no audio track')
