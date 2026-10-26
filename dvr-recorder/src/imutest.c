/*
 * imutest: bench test for an ST LSM6DSO / LSM6DSOX (or LSM6DS3TR-C) IMU wired to one of
 * the board's I2C buses, for the Gyroflow logging prototype.
 *
 *   imutest [-b bus] [-t seconds] [-g gyro_hz] [-a accel_hz] [-o file.gcsv]
 *
 * Looks for the chip at 0x6A and 0x6B, prints WHO_AM_I, then streams samples for -t
 * seconds and reports the real sample rates, the IMU clock against the board clock,
 * gyro bias/noise at rest and the accelerometer magnitude (about 1 g when still).
 * LSM6DSO/DSOX: tagged FIFO with hardware timestamps. LSM6DS3TR-C: plain polling
 * (its FIFO works differently), so rates there are limited by the 100 kHz bus.
 * With -o the samples are also written as a Gyroflow .gcsv log.
 *
 * Only the two IMU addresses are touched, so it can run while dvrrec records: the
 * kernel serialises transfers with dvrrec's TP2850 accesses (0x44) on bus 0.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "compat.h"

#define I2C_RDWR 0x0707
#define I2C_M_RD 0x0001

struct i2c_msg { uint16_t addr, flags, len; uint8_t *buf; };
struct i2c_rdwr_ioctl_data { struct i2c_msg *msgs; uint32_t nmsgs; };

/* registers (same addresses on LSM6DSO and LSM6DS3TR-C unless noted) */
#define WHO_AM_I    0x0f
#define FIFO_CTRL3  0x09  /* DSO: BDR_GY[7:4] BDR_XL[3:0] */
#define FIFO_CTRL4  0x0a  /* DSO: DEC_TS_BATCH[7:6] FIFO_MODE[2:0] */
#define CTRL1_XL    0x10
#define CTRL2_G     0x11
#define CTRL3_C     0x12
#define CTRL10_C    0x19  /* DSO: TIMESTAMP_EN bit 5 */
#define STATUS_REG  0x1e
#define OUTX_L_G    0x22  /* gyro x,y,z then accel x,y,z, 12 bytes */
#define TIMESTAMP0  0x40  /* DSO: 32-bit, 25 us nominal per tick */
#define FREQ_FINE   0x63  /* DSO: INTERNAL_FREQ_FINE, signed, 0.15 % per LSB */
#define FIFO_STAT1  0x3a
#define FIFO_DATA   0x78  /* DSO: tag byte + 6 data bytes per word */

#define ID_DSO  0x6c  /* LSM6DSO and LSM6DSOX */
#define ID_DS3  0x6a  /* LSM6DS3TR-C */

/* full scale: gyro +-2000 dps (70 mdps/LSB), accel +-16 g (0.488 mg/LSB) */
#define G_RAD_PER_LSB (0.070 * M_PI / 180.0)
#define A_G_PER_LSB   0.000488

static int fd = -1;
static uint16_t addr;
static unsigned long i2c_errs;

static int rd(uint8_t reg, uint8_t *buf, uint16_t len) {
    struct i2c_msg m[2] = { { addr, 0, 1, &reg }, { addr, I2C_M_RD, len, buf } };
    struct i2c_rdwr_ioctl_data d = { m, 2 };
    if (ioctl(fd, I2C_RDWR, &d) < 0) { i2c_errs++; return -1; }
    return 0;
}

static int wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    struct i2c_msg m = { addr, 0, 2, b };
    struct i2c_rdwr_ioctl_data d = { &m, 1 };
    if (ioctl(fd, I2C_RDWR, &d) < 0) { i2c_errs++; return -1; }
    return 0;
}

/* ODR code shared by CTRL1_XL/CTRL2_G and FIFO_CTRL3 (12.5 Hz = 1 ... 6667 Hz = 10) */
static int odr_code(int hz) {
    static const int rates[] = { 12, 26, 52, 104, 208, 416, 833, 1666, 3333, 6667 };
    for (int i = 0; i < 10; i++)
        if (hz <= rates[i]) return i + 1;
    return 10;
}

static const char *chip_name(uint8_t id) {
    return id == ID_DSO ? "LSM6DSO/LSM6DSOX" : id == ID_DS3 ? "LSM6DS3TR-C" : "unknown";
}

/* running statistics for the report */
static struct { double s[6], q[6]; unsigned long n[2]; } st;

static void add_gyro(const int16_t *g) {
    for (int i = 0; i < 3; i++) { st.s[i] += g[i]; st.q[i] += (double)g[i] * g[i]; }
    st.n[0]++;
}

static void add_accel(const int16_t *a) {
    double m = 0;
    for (int i = 0; i < 3; i++) { st.s[3 + i] += a[i]; m += (double)a[i] * a[i]; }
    st.q[3] += sqrt(m);
    st.n[1]++;
}

static FILE *out;
static int16_t last_a[3];

static void gcsv_header(int gyro_hz) {
    fprintf(out, "GYROFLOW IMU LOG\nversion,1.3\nid,open-dvr-imutest\norientation,xyz\n"
                 "note,gyro %d Hz, t in microseconds of IMU time\n"
                 "tscale,0.000001\ngscale,%.10f\nascale,%.6f\nt,gx,gy,gz,ax,ay,az\n",
            gyro_hz, G_RAD_PER_LSB, A_G_PER_LSB);
}

static void gcsv_row(unsigned long long t_us, const int16_t *g) {
    if (out)
        fprintf(out, "%llu,%d,%d,%d,%d,%d,%d\n", t_us, g[0], g[1], g[2],
                last_a[0], last_a[1], last_a[2]);
}

static void le16x3(const uint8_t *b, int16_t *v) {
    for (int i = 0; i < 3; i++) v[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
}

/* LSM6DSO: FIFO in continuous mode, gyro + accel batched, a timestamp word every 32 */
static int run_fifo(int secs, int ghz, int ahz) {
    int8_t fine = 0;
    rd(FREQ_FINE, (uint8_t *)&fine, 1);
    double tick_us = 25.0 / (1.0 + 0.0015 * fine);  /* datasheet: 1/(40 kHz * (1 + 0.0015 * fine)) */

    wr(CTRL10_C, 0x20);                                    /* timestamp on */
    wr(FIFO_CTRL4, 0x00);                                  /* bypass = empty the FIFO */
    wr(FIFO_CTRL3, (odr_code(ghz) << 4) | odr_code(ahz));
    wr(FIFO_CTRL4, 0xc6);                                  /* TS every 32 batches, continuous */

    unsigned long long t0 = mono_us(), t_end = t0 + secs * 1000000ULL;
    unsigned long words = 0, bad_tags = 0, reads = 0, max_level = 0;
    uint32_t ts_first = 0, ts_last = 0;
    unsigned long long host_first = 0, host_last = 0;
    unsigned long g_at_ts = 0, ts_count = 0;
    double g_period_ticks = 0;  /* gyro period in IMU ticks, from the timestamps */
    static uint8_t buf[7 * 32];

    while (mono_us() < t_end) {
        uint8_t s[2];
        if (rd(FIFO_STAT1, s, 2) < 0) { usleep(10000); continue; }
        unsigned level = s[0] | ((s[1] & 0x03) << 8);
        if (s[1] & 0x40) fprintf(stderr, "FIFO overrun at %.1f s\n", (mono_us() - t0) / 1e6);
        if (level > max_level) max_level = level;
        while (level > 0) {
            unsigned n = level > 32 ? 32 : level;  /* short reads keep bus 0 free for dvrrec */
            /* the address rolls back from 0x7E to 0x78, so one read returns n words */
            if (rd(FIFO_DATA, buf, 7 * n) < 0) break;
            reads++;
            for (unsigned i = 0; i < n; i++) {
                uint8_t *w = buf + 7 * i, tag = w[0] >> 3;
                int16_t v[3];
                words++;
                if (tag == 0x01) {                         /* gyro */
                    le16x3(w + 1, v);
                    add_gyro(v);
                    unsigned long k = st.n[0] - 1;
                    double t = ts_count ? ts_last + (k - g_at_ts) * (g_period_ticks ? g_period_ticks : 1e6 / ghz / 25.0)
                                        : k * (1e6 / ghz / 25.0);
                    gcsv_row((unsigned long long)(t * tick_us), v);
                } else if (tag == 0x02) {                  /* accel */
                    le16x3(w + 1, v);
                    add_accel(v);
                    memcpy(last_a, v, sizeof last_a);
                } else if (tag == 0x04) {                  /* timestamp */
                    uint32_t ts = w[1] | (w[2] << 8) | (w[3] << 16) | ((uint32_t)w[4] << 24);
                    unsigned long long now = mono_us();
                    if (!ts_count) { ts_first = ts; host_first = now; }
                    else if (st.n[0] > g_at_ts) g_period_ticks = (double)(ts - ts_last) / (st.n[0] - g_at_ts);
                    ts_last = ts; host_last = now; g_at_ts = st.n[0]; ts_count++;
                } else {
                    bad_tags++;
                }
            }
            level -= n;
        }
        usleep(20000);
    }
    wr(FIFO_CTRL4, 0x00);
    double secs_real = (mono_us() - t0) / 1e6;

    printf("FIFO: %lu words in %lu reads, max level %lu of 512, %lu unknown tags%s\n",
           words, reads, max_level, bad_tags, bad_tags ? " (burst read not wrapping?)" : "");
    printf("rates: gyro %.1f Hz, accel %.1f Hz (asked %d / %d)\n",
           st.n[0] / secs_real, st.n[1] / secs_real, ghz, ahz);
    if (ts_count > 1 && host_last > host_first) {
        double imu_s = (ts_last - ts_first) * tick_us / 1e6, host_s = (host_last - host_first) / 1e6;
        printf("IMU clock: freq_fine %d -> %.4f us/tick; %.3f s IMU vs %.3f s board = %+.0f ppm "
               "(includes read latency jitter)\n", fine, tick_us, imu_s, host_s, (imu_s / host_s - 1) * 1e6);
    }
    return 0;
}

/* LSM6DS3TR-C (or DSO fallback): poll STATUS_REG and read the 12 output bytes */
static int run_poll(int secs) {
    unsigned long long t0 = mono_us(), t_end = t0 + secs * 1000000ULL;
    while (mono_us() < t_end) {
        uint8_t s, b[12];
        if (rd(STATUS_REG, &s, 1) < 0) { usleep(10000); continue; }
        if ((s & 0x03) != 0x03) { usleep(500); continue; }
        if (rd(OUTX_L_G, b, 12) < 0) continue;
        int16_t g[3], a[3];
        le16x3(b, g);
        le16x3(b + 6, a);
        add_gyro(g);
        add_accel(a);
        memcpy(last_a, a, sizeof last_a);
        gcsv_row(mono_us() - t0, g);
    }
    double secs_real = (mono_us() - t0) / 1e6;
    printf("polling: %.1f samples/s (host timestamps)\n", st.n[0] / secs_real);
    return 0;
}

int main(int argc, char **argv) {
    int bus = 0, secs = 10, ghz = 416, ahz = 104, opt;
    const char *path = NULL;
    while ((opt = getopt(argc, argv, "b:t:g:a:o:")) != -1) {
        switch (opt) {
        case 'b': bus = atoi(optarg); break;
        case 't': secs = atoi(optarg); break;
        case 'g': ghz = atoi(optarg); break;
        case 'a': ahz = atoi(optarg); break;
        case 'o': path = optarg; break;
        default:
            fprintf(stderr, "usage: %s [-b bus] [-t s] [-g gyro_hz] [-a accel_hz] [-o out.gcsv]\n", argv[0]);
            return 2;
        }
    }

    char dev[32];
    snprintf(dev, sizeof dev, "/dev/i2c-%d", bus);
    fd = open(dev, O_RDWR);
    if (fd < 0) { printf("%s: %s\n", dev, strerror(errno)); return 1; }

    uint8_t id = 0;
    static const uint16_t cand[] = { 0x6a, 0x6b };
    for (unsigned i = 0; i < 2 && !addr; i++) {
        addr = cand[i];
        if (rd(WHO_AM_I, &id, 1) < 0) { printf("bus %d addr 0x%02x: no answer\n", bus, addr); addr = 0; }
    }
    if (!addr) { printf("no IMU found (check wiring, power, pull-ups)\n"); return 1; }
    printf("bus %d addr 0x%02x: WHO_AM_I 0x%02x = %s\n", bus, addr, id, chip_name(id));
    if (id != ID_DSO && id != ID_DS3) return 1;

    wr(CTRL3_C, 0x01);                                     /* software reset */
    usleep(50000);
    wr(CTRL3_C, 0x44);                                     /* BDU, address auto-increment */
    wr(CTRL1_XL, (odr_code(ahz) << 4) | 0x04);             /* accel ODR, +-16 g */
    wr(CTRL2_G, (odr_code(ghz) << 4) | 0x0c);              /* gyro ODR, +-2000 dps */
    usleep(100000);                                        /* let the gyro start up */

    if (path) {
        out = fopen(path, "w");
        if (!out) printf("%s: %s\n", path, strerror(errno));
        else gcsv_header(ghz);
    }
    printf("recording %d s, keep the board still for the bias/noise numbers\n", secs);
    fflush(stdout);
    if (id == ID_DSO) run_fifo(secs, ghz, ahz);
    else run_poll(secs);

    if (st.n[0]) {
        printf("gyro bias  (dps): ");
        for (int i = 0; i < 3; i++) printf("%+.2f ", st.s[i] / st.n[0] * 0.070);
        printf("\ngyro noise (dps rms): ");
        for (int i = 0; i < 3; i++) {
            double m = st.s[i] / st.n[0], var = st.q[i] / st.n[0] - m * m;
            printf("%.3f ", sqrt(var > 0 ? var : 0) * 0.070);
        }
        printf("\n");
    }
    if (st.n[1]) {
        printf("accel mean (g): ");
        for (int i = 0; i < 3; i++) printf("%+.3f ", st.s[3 + i] / st.n[1] * A_G_PER_LSB);
        printf(" |a| %.3f g (expect ~1.000)\n", st.q[3] / st.n[1] * A_G_PER_LSB);
    }
    printf("i2c errors: %lu\n", i2c_errs);
    wr(CTRL1_XL, 0x00);                                    /* power down */
    wr(CTRL2_G, 0x00);
    if (out) fclose(out);
    close(fd);
    return 0;
}
