# Open DVR developer extras. /home/encrypt sources this file when it is on the card; it is
# not part of the release zip. Marker files (empty files are fine) in the SD card root:
#   uartshell     root shell on the UART, /dev/ttyAMA0 at 115200
#   imutest       IMU prototype: 20 s after boot, run dvr/imutest for 30 s (bus 0, or the
#                 bus number written in the file) -> probe/16_imu.txt, probe/imu.gcsv
#   probe_gpio    one boot only: run the stock recorder while dvr/gpioprobe watches the
#                 pins for 3 min (tap the button meanwhile) -> probe/17_gpio_*.txt and,
#                 if the button and LED were found, dvr/gpio.txt. Then the marker
#                 deletes itself, so the next boot is dvrrec again.
# No "exit" in here: it would end the boot hook.

if [ -f /home/uartshell ] && [ -e /dev/ttyAMA0 ]; then
    stty -F /dev/ttyAMA0 115200 2>/dev/null
    ( while true; do
          setsid sh -c 'exec sh </dev/ttyAMA0 >/dev/ttyAMA0 2>&1'
          sleep 1
      done ) </dev/null >/dev/null 2>&1 &
fi

if [ -f /home/probe_gpio ] && [ -f /home/dvr/gpioprobe ]; then
    # first, before the stock recorder starts: the pins as the boot left them
    mkdir -p /home/probe
    /home/dvr/gpioprobe -s > /home/probe/17_gpio_boot.txt 2>&1
    { echo "### modules"; cat /proc/modules; echo "### interrupts at boot"; cat /proc/interrupts; } \
        > /home/probe/17_gpio_system.txt 2>&1
    ( /home/dvr/gpioprobe -t 180 -o /home/probe -w /home/dvr/gpio.txt > /home/probe/17_gpio_run.txt 2>&1
      { echo "### interrupts after 3 min"; cat /proc/interrupts; } >> /home/probe/17_gpio_system.txt 2>&1
      rm -f /home/probe_gpio
      sync ) </dev/null >/dev/null 2>&1 &
    echo "gpio probe boot (stock recorder) at uptime $(cut -d' ' -f1 /proc/uptime)" > /home/dvr/boot.txt
    DVR_USE_STOCK=1
fi

if [ -f /home/imutest ] && [ -f /home/dvr/imutest ]; then
    bus=$(tr -cd 0-9 < /home/imutest); bus=${bus:-0}
    mkdir -p /home/probe
    ( sleep 20
      { echo "### i2cdetect bus $bus, IMU addresses only"; i2cdetect -y -r $bus 0x68 0x6b
        echo "### imutest"; /home/dvr/imutest -b $bus -t 30 -o /home/probe/imu.gcsv
        echo "exit $?"; } > /home/probe/16_imu.txt 2>&1
      sync ) </dev/null >/dev/null 2>&1 &
fi
