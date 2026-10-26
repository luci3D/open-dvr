#!/bin/bash
# Builds the DVR programs for the GK7202V300 board (run inside WSL).
#   bash scripts/build.sh            -> build/dvrtest
# Compiler, headers and crt files come from the Bootlin uClibc-ng toolchain, but the
# program is linked against the board's own uClibc 0.9.33 libraries (sysroot-board/)
# so its DT_NEEDED entries and symbols match what is on the board at runtime.
set -euo pipefail
cd "$(dirname "$0")/.."

T=$HOME/toolchains/armv5-eabi--uclibc--stable-2025.08-1
CC=$T/bin/arm-linux-gcc
SR=$("$CC" -print-sysroot)
BOARD=sysroot-board/lib
VEND=vendor/lib
OUT=build
mkdir -p "$OUT"

CFLAGS="-march=armv7-a -mtune=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall -Wextra -fno-stack-protector -D_GNU_SOURCE"
CRT_BEGIN="$SR/usr/lib/crt1.o $SR/usr/lib/crti.o $("$CC" -print-file-name=crtbegin.o)"
CRT_END="$("$CC" -print-file-name=crtend.o) $SR/usr/lib/crtn.o"
LIBGCC=$("$CC" -print-libgcc-file-name)
BOARD_LIBS="$BOARD/libc.so.0 $BOARD/libpthread.so.0 $BOARD/libdl.so.0 $BOARD/libm.so.0 $BOARD/librt.so.0 $BOARD/ld-uClibc.so.0"

link() {  # link <output> <objects/libs...>
    local out=$1; shift
    "$CC" $CFLAGS -nostdlib -o "$out" $CRT_BEGIN "$@" $BOARD_LIBS "$LIBGCC" $BOARD/libc.so.0 $CRT_END \
        -Wl,--dynamic-linker=/lib/ld-uClibc.so.0 -Wl,-rpath-link,$VEND -Wl,--allow-shlib-undefined \
        -Wl,--hash-style=sysv
}

"$CC" $CFLAGS -c src/dvrtest.c -o $OUT/dvrtest.o
link $OUT/dvrtest $OUT/dvrtest.o -L$VEND -lgk_api -lsecurec -lupvqe -ldnvqe -lvoice_engine
"$T/bin/arm-linux-strip" $OUT/dvrtest

# dvrcap: round-5 capture experiment, uses OpenIPC Divinus (MIT) v4 definitions
DIVINUS="-Ithird_party/divinus/hal/hisi -Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers -Wno-parentheses"
"$CC" $CFLAGS $DIVINUS -c src/dvrcap.c -o $OUT/dvrcap.o
"$CC" $CFLAGS -c src/tp2850.c -o $OUT/tp2850.o
link $OUT/dvrcap $OUT/dvrcap.o $OUT/tp2850.o -L$VEND -lhi_mpi -lgk_api -lsecurec -lupvqe -ldnvqe -lvoice_engine
"$T/bin/arm-linux-strip" $OUT/dvrcap

# dvrrec: the recorder, on OpenIPC openhisilicon headers (GPL-3.0)
OHS="-Ithird_party/openhisilicon/include"
"$CC" $CFLAGS $OHS -Ithird_party/shine -c src/dvrrec.c -o $OUT/dvrrec.o
"$CC" $CFLAGS -c src/tsmux.c -o $OUT/tsmux.o
"$CC" $CFLAGS -c src/mp3frame.c -o $OUT/mp3frame.o
# shine MP3 encoder (LGPL, from OpenIPC Divinus; ARM mode so it uses its ARM multiply code)
SHINE_OBJS=""
for c in third_party/shine/*.c; do
    o=$OUT/shine_$(basename "$c" .c).o
    "$CC" $CFLAGS -marm -w -c "$c" -o "$o"
    SHINE_OBJS="$SHINE_OBJS $o"
done
link $OUT/dvrrec $OUT/dvrrec.o $OUT/tp2850.o $OUT/tsmux.o $OUT/mp3frame.o $SHINE_OBJS -L$VEND -lgk_api -lsecurec -lupvqe -ldnvqe -lvoice_engine
"$T/bin/arm-linux-strip" $OUT/dvrrec

"$T/bin/arm-linux-readelf" -d $OUT/dvrtest | grep -E 'NEEDED|interpreter' || true
"$T/bin/arm-linux-readelf" -l $OUT/dvrtest | grep -i interpreter || true
ls -l $OUT/dvrtest $OUT/dvrcap $OUT/dvrrec
