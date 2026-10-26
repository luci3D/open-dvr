#!/bin/bash
# Installs the cross-compiler for the GK7202V300 DVR board into ~/toolchains (WSL).
# Bootlin armv5-eabi uClibc toolchain: soft-float EABI, same calling convention as the
# board's uClibc 0.9.33 userland (Cortex-A7 runs armv5te code fine).
set -euo pipefail
T=armv5-eabi--uclibc--stable-2025.08-1
URL=https://toolchains.bootlin.com/downloads/releases/toolchains/armv5-eabi/tarballs
mkdir -p "$HOME/toolchains"
cd "$HOME/toolchains"
if [ ! -x "$T/bin/arm-linux-gcc" ]; then
    curl -sSLO "$URL/$T.tar.xz"
    curl -sSLO "$URL/$T.sha256"
    sha256sum -c "$T.sha256"
    tar xf "$T.tar.xz"
    rm -f "$T.tar.xz"
fi
"$T/bin/arm-linux-gcc" --version | head -1
grep -iE '^(uclibc|gcc-final|binutils),' "$T/summary.csv" || true
echo "TOOLCHAIN=$HOME/toolchains/$T"
