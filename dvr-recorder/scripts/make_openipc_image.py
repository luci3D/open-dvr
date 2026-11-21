#!/usr/bin/env python3
"""Build a full 16 MB NOR image for flashrom from the OpenIPC gk7202v300 release files.

    python scripts/make_openipc_image.py u-boot-gk7202v300-nor.bin \
        openipc.gk7202v300-nor-lite.tgz -o build/openipc-board2.bin

Layout is OpenIPC's default 8 MB map (see docs/openipc-plan.md, Phase 1); everything
not written (env, rootfs_data, the upper 8 MB) is left erased (0xFF), so U-Boot uses its
built-in environment and the overlay is formatted on first boot.

The .tgz is only read in memory (uImage + rootfs members); nothing is extracted to disk.

    python scripts/make_openipc_image.py --compare build/openipc-board2.bin readback.bin
"""
import argparse
import hashlib
import sys
import tarfile

FLASH = 16 * 1024 * 1024
UBOOT = (0x000000, 0x040000)    # offset, size
ENV = (0x040000, 0x010000)
KERNEL = (0x050000, 0x200000)
ROOTFS = (0x250000, 0x500000)

UIMAGE_MAGIC = bytes.fromhex('27051956')
SQUASHFS_MAGIC = b'hsqs'


def member(tgz, prefix):
    with tarfile.open(tgz, 'r:gz') as t:
        names = [m for m in t.getmembers() if m.isfile() and m.name.split('/')[-1].startswith(prefix)
                 and not m.name.endswith('.md5sum')]
        if len(names) != 1:
            sys.exit(f'{tgz}: expected one {prefix}* member, found {[m.name for m in names]}')
        return names[0].name, t.extractfile(names[0]).read()


def place(img, name, data, region):
    off, size = region
    if len(data) > size:
        sys.exit(f'{name}: {len(data)} bytes does not fit in {size} at 0x{off:06x}')
    img[off:off + len(data)] = data
    print(f'  0x{off:06x}  {len(data):8d} / {size:8d}  {name}')


def build(uboot, tgz, out):
    img = bytearray(b'\xff' * FLASH)
    ub = open(uboot, 'rb').read()
    kname, kernel = member(tgz, 'uImage')
    rname, rootfs = member(tgz, 'rootfs.squashfs')
    if kernel[:4] != UIMAGE_MAGIC:
        sys.exit(f'{kname}: not a uImage (magic {kernel[:4].hex()})')
    if rootfs[:4] != SQUASHFS_MAGIC:
        sys.exit(f'{rname}: not a squashfs (magic {rootfs[:4]!r})')
    place(img, uboot, ub, UBOOT)
    print(f'  0x{ENV[0]:06x}  {"erased":>8} / {ENV[1]:8d}  env')
    place(img, kname, kernel, KERNEL)
    place(img, rname, rootfs, ROOTFS)
    open(out, 'wb').write(img)
    print(f'{out}: {FLASH} bytes, sha256 {hashlib.sha256(img).hexdigest()}')


def compare(a, b):
    da, db = open(a, 'rb').read(), open(b, 'rb').read()
    if len(da) != len(db):
        sys.exit(f'size differs: {len(da)} vs {len(db)}')
    bad = [i for i in range(0, len(da), 4096) if da[i:i + 4096] != db[i:i + 4096]]
    if bad:
        print(f'{len(bad)} of {len(da) // 4096} 4 KB sectors differ, first at 0x{bad[0]:06x}')
        sys.exit(1)
    print(f'identical, sha256 {hashlib.sha256(da).hexdigest()}')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('files', nargs=2, help='u-boot.bin firmware.tgz, or two images with --compare')
    p.add_argument('-o', '--out', default='build/openipc-16m.bin')
    p.add_argument('--compare', action='store_true')
    a = p.parse_args()
    if a.compare:
        compare(*a.files)
    else:
        build(*a.files, a.out)


if __name__ == '__main__':
    main()
