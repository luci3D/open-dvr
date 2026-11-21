#!/usr/bin/env python3
"""Builds the release zip: what a user extracts onto the DVR's SD card.

    python scripts/make_release.py      -> dist/open-dvr-<version>.zip (and the same tree
                                           unpacked in dist/open-dvr-<version>/)

Run scripts/build.sh and scripts/stage_sdcard.sh first. The version is DVRREC_VERSION in
src/dvrrec.c, and a dvrrec binary that does not carry it (a stale build) is refused.
Developer files (probe, tests, dev.sh) are left out. Needs pyelftools, like check_links.py.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))

# (source, relative to dvr-recorder/) -> (path on the card, kind)
#   lf    runs on the board: Unix line endings, checked for stray CRs
#   crlf  text the user opens on a PC (Notepad): Windows line endings
#   bin   copied as is
#   an "x" marks the file executable inside the zip
FILES = [
    ('release/README.txt', 'README.txt', 'crlf'),
    ('release/config.txt', 'config.txt', 'config'),
    ('sdcard/encrypt', 'encrypt', 'lf x'),
    ('sdcard/dvr/dvrrec', 'dvr/dvrrec', 'bin x'),
    ('sdcard/dvr/start.sh', 'dvr/start.sh', 'lf x'),
    ('sdcard/dvr/gpio.txt', 'dvr/gpio.txt', 'lf'),
    ('release/NOTICE.txt', 'dvr/licenses/NOTICE.txt', 'crlf'),
    ('../LICENSE', 'dvr/licenses/GPL-3.0.txt', 'crlf'),
    ('third_party/shine/COPYING', 'dvr/licenses/LGPL-2.0.txt', 'crlf'),
]


def fail(msg):
    print('make_release: ' + msg)
    sys.exit(1)


def read(rel):
    with open(os.path.join(ROOT, rel), 'rb') as f:
        return f.read()


def convert(data, kind, name):
    if kind == 'bin':
        return data
    if kind == 'lf':
        if b'\r' in data:
            fail('%s has Windows line endings; it would not run on the board' % name)
        return data
    data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    if kind == 'config':
        data = data.rstrip(b'\r\n')  # like the file the stock firmware writes: no final newline
    return data


def main():
    m = re.search(rb'#define DVRREC_VERSION "([^"]+)"', read('src/dvrrec.c'))
    if not m:
        fail('DVRREC_VERSION not found in src/dvrrec.c')
    version = m.group(1).decode()

    dvrrec = read('sdcard/dvr/dvrrec')
    if ('dvrrec %s:' % version).encode() not in dvrrec:
        fail('sdcard/dvr/dvrrec is not version %s: run build.sh and stage_sdcard.sh' % version)
    built = os.path.join(ROOT, 'build', 'dvrrec')
    if os.path.exists(built) and read('build/dvrrec') != dvrrec:
        fail('sdcard/dvr/dvrrec differs from build/dvrrec: run stage_sdcard.sh')
    if subprocess.call([sys.executable, os.path.join(HERE, 'check_links.py'),
                        os.path.join(ROOT, 'sdcard', 'dvr', 'dvrrec')]):
        fail('check_links.py failed for dvrrec')

    # the libraries dvrrec asks for that are ours to ship (the rest is on the board)
    with open(os.path.join(ROOT, 'sdcard', 'dvr', 'dvrrec'), 'rb') as f:
        needed = [t.needed for t in ELFFile(f).get_section_by_name('.dynamic').iter_tags()
                  if t.entry.d_tag == 'DT_NEEDED']
    files = list(FILES)
    for lib in needed:
        if os.path.exists(os.path.join(ROOT, 'vendor', 'lib', lib)):
            if read('vendor/lib/' + lib) != read('sdcard/dvr/lib/' + lib):
                fail('sdcard/dvr/lib/%s differs from vendor/lib: run stage_sdcard.sh' % lib)
            files.append(('sdcard/dvr/lib/' + lib, 'dvr/lib/' + lib, 'bin'))
        elif not os.path.exists(os.path.join(ROOT, 'sysroot-board', 'lib', lib)):
            fail('dvrrec needs %s, which is neither in vendor/lib nor on the board' % lib)

    name = 'open-dvr-%s' % version
    dist = os.path.join(ROOT, 'dist')
    tree = os.path.join(dist, name)
    zpath = os.path.join(dist, name + '.zip')
    if os.path.isdir(tree):
        shutil.rmtree(tree)
    os.makedirs(tree)

    now = time.localtime()[:6]
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, dst, kind in sorted(files, key=lambda f: f[1]):
            data = convert(read(src), kind.split()[0], src)
            out = os.path.join(tree, *dst.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, 'wb') as f:
                f.write(data)
            info = zipfile.ZipInfo(dst, now)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3  # Unix, so the mode below is kept by unzip on Linux/macOS
            info.external_attr = (0o755 if 'x' in kind.split() else 0o644) << 16
            z.writestr(info, data)
            print('  %-28s %8d bytes' % (dst, len(data)))

    with zipfile.ZipFile(zpath) as z:
        if z.testzip():
            fail('the zip does not read back')
    with open(zpath, 'rb') as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    print('%s: %d bytes\nsha256 %s' % (os.path.relpath(zpath, ROOT), os.path.getsize(zpath), digest))


if __name__ == '__main__':
    main()
