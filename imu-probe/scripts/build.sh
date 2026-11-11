#!/bin/bash
# Builds imutest for the board and runs the host test (WSL or Linux).
#   bash scripts/build.sh            -> build/imutest (also copied to board/), build/imusim
# The board program is linked statically with Ubuntu's ARM hard-float cross-compiler
#   sudo apt install gcc-arm-linux-gnueabihf
# so it does not depend on the board's C library: it runs on the Luckfox Buildroot image
# (uClibc) and on Ubuntu/Debian images (Radxa) alike.
set -euo pipefail
cd "$(dirname "$0")/.."

CC=arm-linux-gnueabihf-gcc
CFLAGS="-O2 -Wall -Wextra -D_GNU_SOURCE"
OUT=build
mkdir -p "$OUT" board

# host test first: the chip driver against a simulated LSM6DSOX
gcc $CFLAGS -o $OUT/imusim tests/imusim.c src/lsm6dsox.c -lm
$OUT/imusim

"$CC" $CFLAGS -static -o $OUT/imutest src/imutest.c src/lsm6dsox.c -lm
arm-linux-gnueabihf-strip $OUT/imutest
cp $OUT/imutest board/imutest
ls -l board/imutest
