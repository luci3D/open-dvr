#!/bin/sh
# Open DVR launcher. /home/encrypt bind-mounts this file over /boot/appRecord, so the
# stock run.sh ("cd /boot; ./appRecord &") starts it instead of the stock recorder.
#
# Runs /home/dvr/dvrrec and restarts it if it stops. Exit code 3 means "no video" (camera
# off or switched standard): it is restarted so PAL/NTSC detection runs again. Any other
# exit within 20 s of starting counts as a failure; after three in a row the stock
# recorder is started instead, so the board still records.
# Extra dvrrec options can go in /home/dvr/args (one line), e.g. "-c 1".
export LD_LIBRARY_PATH=/home/dvr/lib
LOG=/home/dvr/log.txt
ARGS=$(cat /home/dvr/args 2>/dev/null)

fails=0
while true; do
    if [ -f $LOG ] && [ "$(wc -c < $LOG)" -gt 1000000 ]; then
        mv $LOG /home/dvr/log.old.txt
    fi
    t0=$(cut -d. -f1 /proc/uptime)
    echo "=== dvrrec $ARGS: start at uptime $t0 s ===" >> $LOG
    /home/dvr/dvrrec $ARGS >> $LOG 2>&1
    rc=$?
    t1=$(cut -d. -f1 /proc/uptime)
    echo "=== dvrrec exit $rc after $((t1 - t0)) s ===" >> $LOG
    sync
    if [ $rc -eq 3 ]; then
        fails=0
    elif [ $((t1 - t0)) -lt 20 ]; then
        fails=$((fails + 1))
    else
        fails=0
    fi
    if [ $fails -ge 3 ] && [ -e /dev/appRecord.stock ]; then
        echo "=== dvrrec keeps failing: starting the stock recorder ===" >> $LOG
        sync
        cd /boot && exec /dev/appRecord.stock
    fi
    sleep 1
done
