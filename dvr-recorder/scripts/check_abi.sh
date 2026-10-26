#!/bin/bash
# Prints toolchain C-library settings that must match the board's uClibc 0.9.33
# (32-bit time_t, 32-bit off_t by default).
T=$HOME/toolchains/armv5-eabi--uclibc--stable-2025.08-1
CC=$T/bin/arm-linux-gcc
SR=$("$CC" -print-sysroot)
grep -E 'TIME64|LFS|FILE_OFFSET' "$SR/usr/include/bits/uClibc_config.h"
cat > /tmp/abi.c <<'EOF'
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
int a[] = { sizeof(time_t), sizeof(struct timeval), sizeof(struct timespec), sizeof(off_t), sizeof(long) };
EOF
"$CC" -march=armv7-a -mfloat-abi=softfp -c /tmp/abi.c -o /tmp/abi.o
"$T/bin/arm-linux-objdump" -s -j .data /tmp/abi.o | tail -2
echo "(words: time_t, timeval, timespec, off_t, long)"
