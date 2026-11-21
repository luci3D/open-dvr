#!/bin/bash
# Dry run of the boot hook (sdcard/encrypt, with and without sdcard/dvr/dev.sh) in a
# sandbox: /home, /boot, /proc and the /dev files it touches are redirected to a temp
# folder and "mount" only logs. Run in WSL or Linux (uses dash, close to the board's
# busybox sh):   bash tests/hook_test.sh
set -u
SRC="$(cd "$(dirname "$0")/.." && pwd)/sdcard"
fails=0

# check <name> <expected mount count> <expected text in boot.txt or -> <setup function>
check() {
    local name=$1 want=$2 boot=$3 setup=$4 T n got
    T=$(mktemp -d)
    mkdir -p "$T/home/dvr" "$T/boot" "$T/dev" "$T/proc" "$T/bin"
    echo "1.23 4.56" > "$T/proc/uptime"
    echo modules > "$T/proc/modules"; echo irq > "$T/proc/interrupts"
    echo stock > "$T/boot/appRecord"
    cp "$SRC/dvr/start.sh" "$T/home/dvr/start.sh"
    echo elf > "$T/home/dvr/dvrrec"
    printf '#!/bin/sh\necho "mount $*" >> %s/mounts.log\n' "$T" > "$T/bin/mount"
    printf '#!/bin/sh\necho "gpioprobe $*"\n' > "$T/home/dvr/gpioprobe"
    chmod +x "$T/bin/mount" "$T/home/dvr/gpioprobe"
    $setup "$T"
    redirect="s#/home#$T/home#g; s#/boot/#$T/boot/#g; s#/proc/#$T/proc/#g; s#/dev/appRecord#$T/dev/appRecord#g; s#/dev/ttyAMA0#$T/dev/ttyAMA0#g"
    sed -e "$redirect" "$SRC/encrypt" > "$T/home/encrypt"
    [ -f "$T/home/dvr/dev.sh" ] && sed -i -e "$redirect" "$T/home/dvr/dev.sh"
    [ -f "$T/home/encrypt.orig" ] && chmod +x "$T/home/encrypt.orig"
    ( cd "$T/home" && PATH="$T/bin:$PATH" dash ./encrypt ) || { echo "FAIL $name: hook exit code"; fails=$((fails + 1)); }
    sleep 0.5  # background jobs of dev.sh
    n=$(cat "$T/mounts.log" 2>/dev/null | wc -l)
    got=$(cat "$T/home/dvr/boot.txt" 2>/dev/null)
    if [ "$n" != "$want" ]; then echo "FAIL $name: $n mounts, expected $want"; fails=$((fails + 1))
    elif [ "$boot" != "-" ] && ! echo "$got" | grep -q "$boot"; then echo "FAIL $name: boot.txt '$got'"; fails=$((fails + 1))
    elif [ -n "${EXTRA:-}" ] && ! eval "$EXTRA"; then echo "FAIL $name: $EXTRA"; fails=$((fails + 1))
    else echo "ok   $name"; fi
    [ "$want" = 2 ] && ! grep -q "bind $T/home/dvr/start.sh $T/boot/appRecord" "$T/mounts.log" && { echo "FAIL $name: wrong mounts"; fails=$((fails + 1)); }
    rm -rf "$T"
}

release()      { :; }
stock_marker() { : > "$1/home/stock"; }
dev_idle()     { cp "$SRC/dvr/dev.sh" "$1/home/dvr/dev.sh"; }
dev_probe()    { cp "$SRC/dvr/dev.sh" "$1/home/dvr/dev.sh"; : > "$1/home/probe_gpio"; }
no_recorder()  { rm "$1/home/dvr/dvrrec"; }
orig_hook()    { printf '#!/bin/sh\n: > ran_orig\n' > "$1/home/encrypt.orig"; }

EXTRA=""
check "release layout: our recorder"          2 "open DVR active" release
check "stock marker: stock recorder"          0 -                 stock_marker
check "dev.sh without markers: our recorder"  2 "open DVR active" dev_idle
EXTRA='[ ! -f "$T/home/probe_gpio" ] && grep -q "gpioprobe -t 180" "$T/home/probe/17_gpio_run.txt"'
check "probe_gpio: stock recorder, one shot"  0 "gpio probe boot" dev_probe
EXTRA=""
check "dvrrec missing: stock recorder"        0 -                 no_recorder
EXTRA='[ -f "$T/home/ran_orig" ]'
check "encrypt.orig is run"                   2 "open DVR active" orig_hook

[ $fails = 0 ] && echo "hook_test: OK" || echo "hook_test: FAILED ($fails)"
exit $fails
