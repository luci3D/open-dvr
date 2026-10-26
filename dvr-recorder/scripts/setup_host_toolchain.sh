#!/bin/bash
# Installs a native x86-64 toolchain into ~/toolchains (WSL) for host-side unit tests
# (e.g. the MPEG-TS muxer), no root needed.
set -euo pipefail
T=x86-64--glibc--stable-2025.08-1
URL=https://toolchains.bootlin.com/downloads/releases/toolchains/x86-64/tarballs
mkdir -p "$HOME/toolchains"
cd "$HOME/toolchains"
if [ ! -x "$T/bin/x86_64-linux-gcc" ]; then
    curl -sSLO "$URL/$T.tar.xz"
    curl -sSLO "$URL/$T.sha256"
    sha256sum -c "$T.sha256"
    tar xf "$T.tar.xz"
    rm -f "$T.tar.xz"
fi
"$T/bin/x86_64-linux-gcc" --version | head -1
