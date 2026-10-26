#!/bin/bash
# Assembles the files that go on the DVR's SD card into sdcard/ (run after build.sh).
#   sdcard/encrypt        boot hook (kept in git, edited by hand)
#   sdcard/dvr/<programs> built programs
#   sdcard/dvr/lib/*.so   Goke MPP libraries from OpenIPC (vendor/lib)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p sdcard/dvr/lib
cp build/dvrrec sdcard/dvr/
cp vendor/lib/*.so sdcard/dvr/lib/
ls -lR sdcard
