#!/bin/bash
T=$HOME/toolchains/armv5-eabi--uclibc--stable-2025.08-1
SR=$("$T/bin/arm-linux-gcc" -print-sysroot)
echo "sysroot=$SR"
ls "$SR/lib"
ls "$SR"/usr/lib/crt*.o
for f in "$SR"/lib/libc.so* "$SR"/lib/ld*-uClibc*; do echo "$f: $("$T/bin/arm-linux-readelf" -d "$f" 2>/dev/null | grep -i soname)"; done
grep -E 'UCLIBC_HAS_SSP|UCLIBC_HAS_THREADS_NATIVE|UCLIBC_HAS_TLS|__UCLIBC_MAJOR__|__UCLIBC_MINOR__|__UCLIBC_SUBLEVEL__' "$SR/usr/include/bits/uClibc_config.h" | head
