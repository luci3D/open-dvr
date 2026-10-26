#!/bin/bash
# Builds and runs host-side tests (WSL): muxes a board recording into MPEG-TS, with and
# without an MP3 test tone.
#   bash scripts/host_test.sh results/round13/rec_r1_w1920.h264
set -euo pipefail
cd "$(dirname "$0")/.."
HCC=$HOME/toolchains/x86-64--glibc--stable-2025.08-1/bin/x86_64-linux-gcc
SHINE=$(ls third_party/shine/*.c)
mkdir -p build/host
"$HCC" -O2 -Wall -Wextra -static -o build/host/tstest tests/tstest.c src/tsmux.c src/mp3frame.c $SHINE \
    -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable -lm
IN=${1:-results/round13/rec_r1_w1920.h264}
build/host/tstest "$IN" build/host/tstest.ts 50
build/host/tstest "$IN" build/host/tstest_tone.ts 50 tone
ls -l build/host/*.ts

# shine at different input levels (loud flight audio came out silent)
"$HCC" -O2 -static -o build/host/shinetest tests/shinetest.c $SHINE -w -lm
build/host/shinetest
