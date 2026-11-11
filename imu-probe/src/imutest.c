/*
 * imutest: bring-up test for an LSM6DSOX IMU on a Linux board (Luckfox Pico, Radxa).
 *
 * Without -o it finds the chip and prints live gyro and accel values, to check the
 * wiring and the axes. With -o it streams gyro + accel through the chip's FIFO into a
 * Gyroflow .gcsv log and reports whether any samples were lost.
 *
 *   imutest [-d device] [-a addr] [-r rate] [-g dps] [-x g] [-t seconds] [-s hz] [-o file.gcsv]
 *     -d: /dev/i2c-N or /dev/spidevX.Y (default: search every I2C bus)
 *     -a: I2C address, 0x6a or 0x6b (default: try both)
 *     -r: sample rate in Hz: 104, 208, 416 (default), 833, 1667, 3333, 6667
 *     -g: gyro full scale in deg/s: 125, 250, 500, 1000, 2000 (default)
 *     -x: accel full scale in g: 2, 4, 8, 16 (default)
 *     -t: run time in seconds (default 10 live, 30 logging; 0 = until Ctrl-C)
 *     -s: SPI clock in Hz (default 1000000)
 *     -o: log to this file
 *
 * Sample times in the log are on CLOCK_MONOTONIC (the clock V4L2 stamps video frames
 * with): the time the FIFO was started plus the sample number times the chip's sample
 * period. Exit status: 0 = pass, 1 = fail.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "lsm6dsox.h"

typedef struct { int fd, addr; } bus_ctx;

static volatile sig_atomic_t stop;

static void on_signal(int sig) { (void)sig; stop = 1; }

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* I2C: register address, repeated start, then the data */
static int i2c_rd(void *ctx, unsigned char reg, unsigned char *buf, int n) {
    bus_ctx *c = ctx;
    struct i2c_msg m[2] = {
        { .addr = c->addr, .flags = 0, .len = 1, .buf = &reg },
        { .addr = c->addr, .flags = I2C_M_RD, .len = n, .buf = buf },
    };
    struct i2c_rdwr_ioctl_data x = { m, 2 };
    return ioctl(c->fd, I2C_RDWR, &x) == 2 ? 0 : -1;
}

static int i2c_wr(void *ctx, unsigned char reg, unsigned char val) {
    bus_ctx *c = ctx;
    unsigned char b[2] = { reg, val };
    struct i2c_msg m = { .addr = c->addr, .flags = 0, .len = 2, .buf = b };
    struct i2c_rdwr_ioctl_data x = { &m, 1 };
    return ioctl(c->fd, I2C_RDWR, &x) == 1 ? 0 : -1;
}

/* SPI mode 3: bit 7 of the first byte set = read */
static int spi_rd(void *ctx, unsigned char reg, unsigned char *buf, int n) {
    bus_ctx *c = ctx;
    unsigned char cmd = reg | 0x80;
    struct spi_ioc_transfer t[2];
    memset(t, 0, sizeof(t));
    t[0].tx_buf = (unsigned long)&cmd;
    t[0].len = 1;
    t[1].rx_buf = (unsigned long)buf;
    t[1].len = n;
    return ioctl(c->fd, SPI_IOC_MESSAGE(2), t) < 0 ? -1 : 0;
}

static int spi_wr(void *ctx, unsigned char reg, unsigned char val) {
    bus_ctx *c = ctx;
    unsigned char b[2] = { reg & 0x7F, val };
    struct spi_ioc_transfer t;
    memset(&t, 0, sizeof(t));
    t.tx_buf = (unsigned long)b;
    t.len = 2;
    return ioctl(c->fd, SPI_IOC_MESSAGE(1), &t) < 0 ? -1 : 0;
}

static int open_spi(imu_dev *d, bus_ctx *c, const char *path, unsigned hz) {
    unsigned char mode = SPI_MODE_3, bits = 8;
    c->fd = open(path, O_RDWR);
    if (c->fd < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    if (ioctl(c->fd, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(c->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(c->fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
        fprintf(stderr, "%s: SPI setup failed: %s\n", path, strerror(errno));
        return -1;
    }
    d->bus.rd = spi_rd;
    d->bus.wr = spi_wr;
    printf("%s (SPI, %u Hz): ", path, hz);
    return 0;
}

/* tries addr (or 0x6a and 0x6b when addr is 0) on one I2C bus; 0 when a chip answers */
static int open_i2c(imu_dev *d, bus_ctx *c, const char *path, int addr, int quiet) {
    c->fd = open(path, O_RDWR);
    if (c->fd < 0) {
        if (!quiet) fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    d->bus.rd = i2c_rd;
    d->bus.wr = i2c_wr;
    for (int a = addr ? addr : 0x6A; a <= (addr ? addr : 0x6B); a++) {
        c->addr = a;
        if (imu_id(d) >= 0) {
            printf("%s (I2C) address 0x%02x: ", path, a);
            return 0;
        }
    }
    close(c->fd);
    return -1;
}

static int find_chip(imu_dev *d, bus_ctx *c, const char *dev, int addr, unsigned spi_hz) {
    char path[32];
    int id;

    d->bus.ctx = c;
    if (dev && strstr(dev, "spidev")) {
        if (open_spi(d, c, dev, spi_hz)) return -1;
    } else if (dev) {
        if (open_i2c(d, c, dev, addr, 0)) {
            if (!access(dev, F_OK)) fprintf(stderr, "%s: nothing answers at 0x6a / 0x6b\n", dev);
            return -1;
        }
    } else {
        int n;
        for (n = 0; n < 16; n++) {
            snprintf(path, sizeof(path), "/dev/i2c-%d", n);
            if (!open_i2c(d, c, path, addr, 1)) break;
        }
        if (n == 16) {
            fprintf(stderr, "No chip at 0x6a / 0x6b on any /dev/i2c-N bus.\n"
                            "Check the four wires, and that the I2C bus is enabled (luckfox-config).\n");
            return -1;
        }
    }
    id = imu_id(d);
    if (id != IMU_ID) {
        printf("ID 0x%02x, expected 0x%02x (LSM6DSOX)\n", id & 0xFF, IMU_ID);
        return -1;
    }
    printf("LSM6DSOX found (ID 0x%02x)\n", id);
    return 0;
}

/* prints values four times a second, then the averages */
static int live(imu_dev *d, double seconds) {
    long long end = now_us() + (long long)(seconds * 1e6);
    double sg[3] = { 0 }, sa[3] = { 0 }, temp = 0;
    int n = 0;

    printf("\n      gyro X       Y       Z deg/s    accel X      Y      Z g     |accel|   temp\n");
    while (!stop && (seconds == 0 || now_us() < end)) {
        imu_sample s;
        double g[3], a[3];
        if (imu_read_now(d, &s, &temp)) {
            fprintf(stderr, "bus read failed\n");
            return 1;
        }
        for (int i = 0; i < 3; i++) {
            g[i] = s.g[i] * d->dps_lsb;
            a[i] = s.a[i] * d->g_lsb;
            sg[i] += g[i];
            sa[i] += a[i];
        }
        n++;
        printf("  %+8.1f %+7.1f %+7.1f          %+6.2f %+6.2f %+6.2f     %5.2f   %4.1f C\n",
               g[0], g[1], g[2], a[0], a[1], a[2], sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), temp);
        fflush(stdout);
        usleep(250000);
    }
    if (!n) return 1;
    for (int i = 0; i < 3; i++) {
        sg[i] /= n;
        sa[i] /= n;
    }
    printf("\nAverage: gyro %+.1f %+.1f %+.1f deg/s, |accel| %.2f g\n", sg[0], sg[1], sg[2],
           sqrt(sa[0] * sa[0] + sa[1] * sa[1] + sa[2] * sa[2]));
    printf("With the board lying still, expect the gyro within a few deg/s of 0 and |accel| near 1.00 g.\n");
    return 0;
}

static int logfifo(imu_dev *d, const char *file, double seconds) {
    static imu_sample buf[IMU_FIFO_WORDS];
    FILE *f = fopen(file, "w");
    long long t0, end, last_print, first_t = 0, last_t = 0;
    unsigned long long total = 0, first_n = 0, last_n = 0;
    double period_us = 1e6 / d->odr_hz, rate = 0, elapsed;
    int peak = 0, errors = 0, fail;

    if (!f) {
        fprintf(stderr, "%s: %s\n", file, strerror(errno));
        return 1;
    }
    imu_gcsv_header(d, f);
    if (imu_fifo_start(d)) {
        fprintf(stderr, "FIFO start failed\n");
        fclose(f);
        return 1;
    }
    t0 = last_print = now_us();
    end = t0 + (long long)(seconds * 1e6);
    printf("\nLogging to %s%s\n", file, seconds == 0 ? " until Ctrl-C" : "");

    while (!stop && (seconds == 0 || now_us() < end)) {
        long long tq = now_us();
        int n = imu_fifo_read(d, buf, IMU_FIFO_WORDS);
        if (n < 0) {
            if (++errors > 20) break;
            usleep(2000);
            continue;
        }
        for (int i = 0; i < n; i++)
            imu_gcsv_row(f, t0 + (long long)((total + i + 1) * period_us), &buf[i]);
        total += n;
        if (d->fill > peak) peak = d->fill;
        /* samples between the first and the last FIFO check, against the board's clock */
        if (!first_t && total) {
            first_t = tq;
            first_n = total;
        }
        last_t = tq;
        last_n = total;
        if (tq - last_print >= 1000000) {
            last_print = tq;
            fflush(f);
            printf("  %4.0f s  %8llu samples  FIFO peak %d/%d\n", (tq - t0) / 1e6, total, peak, IMU_FIFO_WORDS);
            fflush(stdout);
        }
        if (d->fill < 64) usleep(5000);       /* nearly empty: let it refill */
    }
    elapsed = (now_us() - t0) / 1e6;
    if (fclose(f)) {
        fprintf(stderr, "%s: write failed: %s\n", file, strerror(errno));
        errors++;
    }
    if (last_t > first_t) rate = (last_n - first_n) * 1e6 / (last_t - first_t);

    fail = !total || d->overruns || errors;
    printf("\nSamples       %llu in %.1f s\n", total, elapsed);
    printf("Rate          %.1f Hz measured, %.1f Hz expected (chip trim %+.2f%%)\n", rate, d->odr_hz, d->trim_pct);
    printf("FIFO peak     %d of %d words\n", peak, IMU_FIFO_WORDS);
    printf("Overruns      %llu\n", d->overruns);
    printf("Bus errors    %d\n", errors);
    printf("Slot jumps    %llu, unpaired words %llu, other words %llu\n", d->cnt_jumps, d->unpaired, d->other_words);
    if (d->overruns)
        printf("The FIFO overflowed: the bus is too slow for this rate. Lower -r, raise the I2C clock, or use SPI.\n");
    else if (rate && fabs(rate / d->odr_hz - 1) > 0.03)
        printf("Note: the measured rate is more than 3%% off the expected one.\n");
    printf("RESULT: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

static void usage(void) {
    fprintf(stderr,
            "imutest [-d device] [-a addr] [-r rate] [-g dps] [-x g] [-t seconds] [-s hz] [-o file.gcsv]\n"
            "  no -o: find the LSM6DSOX and print live values\n"
            "  -o:    log gyro + accel through the FIFO to a Gyroflow .gcsv file\n"
            "  -d /dev/i2c-N or /dev/spidevX.Y (default: search every I2C bus)\n"
            "  -a I2C address 0x6a / 0x6b     -s SPI clock in Hz (default 1000000)\n"
            "  -r rate in Hz: 104 208 416 833 1667 3333 6667 (default 416)\n"
            "  -g gyro deg/s: 125 250 500 1000 2000 (default 2000)\n"
            "  -x accel g: 2 4 8 16 (default 16)\n"
            "  -t seconds (default 10 live, 30 logging; 0 = until Ctrl-C)\n");
}

int main(int argc, char **argv) {
    const char *dev = NULL, *out = NULL;
    int addr = 0, gyro = 2000, accel = 16, opt, rc;
    double rate = 416, seconds = -1;
    unsigned spi_hz = 1000000;
    imu_dev d;
    bus_ctx c;

    while ((opt = getopt(argc, argv, "d:a:r:g:x:t:s:o:h")) != -1) {
        switch (opt) {
        case 'd': dev = optarg; break;
        case 'a': addr = (int)strtol(optarg, NULL, 0); break;
        case 'r': rate = atof(optarg); break;
        case 'g': gyro = atoi(optarg); break;
        case 'x': accel = atoi(optarg); break;
        case 't': seconds = atof(optarg); break;
        case 's': spi_hz = (unsigned)strtoul(optarg, NULL, 0); break;
        case 'o': out = optarg; break;
        default: usage(); return 2;
        }
    }
    if (seconds < 0) seconds = out ? 30 : 10;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    memset(&d, 0, sizeof(d));
    if (find_chip(&d, &c, dev, addr, spi_hz)) return 1;
    if (imu_setup(&d, rate, gyro, accel)) {
        fprintf(stderr, "Setup failed (bus error, or a -g / -x value that is not in the list)\n");
        return 1;
    }
    printf("Rate %.1f Hz (chip trim %+.2f%%), gyro +-%d deg/s, accel +-%d g\n", d.odr_hz, d.trim_pct, gyro, accel);
    rc = out ? logfifo(&d, out, seconds) : live(&d, seconds);
    imu_stop(&d);
    close(c.fd);
    return rc;
}
