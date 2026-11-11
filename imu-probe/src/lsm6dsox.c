/*
 * LSM6DSOX gyro + accelerometer, see lsm6dsox.h.
 *
 * Register names and values are from the ST LSM6DSOX datasheet. The FIFO holds 7-byte
 * words: a tag byte (sensor in bits 7:3, a 2-bit time slot counter in bits 2:1) and
 * three 16-bit little-endian values. With gyro and accel batched at the same rate, each
 * time slot adds one gyro word and one accel word.
 */
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "lsm6dsox.h"

#define FIFO_CTRL1 0x07
#define FIFO_CTRL2 0x08
#define FIFO_CTRL3 0x09      /* batch rates: gyro in 7:4, accel in 3:0 */
#define FIFO_CTRL4 0x0A      /* mode in 2:0: 0 = bypass (off, cleared), 6 = continuous */
#define WHO_AM_I 0x0F
#define CTRL1_XL 0x10        /* accel: ODR in 7:4, full scale in 3:2 */
#define CTRL2_G 0x11         /* gyro: ODR in 7:4, full scale in 3:2, 125 dps in bit 1 */
#define CTRL3_C 0x12
#define CTRL3_BDU 0x40       /* output registers update only between reads */
#define CTRL3_IF_INC 0x04    /* register address auto-increment */
#define CTRL3_SW_RESET 0x01
#define CTRL9_XL 0x18
#define CTRL9_I3C_OFF 0xE2   /* reset value 0xE0 plus I3C_disable */
#define OUT_TEMP_L 0x20      /* then gyro X/Y/Z (0x22) and accel X/Y/Z (0x28) */
#define FIFO_STATUS1 0x3A    /* unread words, low byte; high bits in FIFO_STATUS2 1:0 */
#define FIFO_OVR 0x48        /* FIFO_STATUS2: overrun now (bit 6) or latched (bit 3) */
#define INTERNAL_FREQ_FINE 0x63
#define FIFO_DATA_OUT_TAG 0x78

#define TAG_GYRO 0x01
#define TAG_ACCEL 0x02
#define WORD 7
#define CHUNK_WORDS 16       /* most FIFO words per bus read (the address wraps 0x7E -> 0x78) */

/* ODR code 1 = 12.5 Hz, doubling up to code 10 = 6667 Hz */
static double odr_of(int code) { return 6667.0 / (1 << (10 - code)); }

static int rd(imu_dev *d, unsigned char reg, unsigned char *buf, int n) {
    return d->bus.rd(d->bus.ctx, reg, buf, n);
}

static int wr(imu_dev *d, unsigned char reg, unsigned char val) {
    return d->bus.wr(d->bus.ctx, reg, val);
}

static short le16(const unsigned char *p) { return (short)(p[0] | p[1] << 8); }

int imu_id(imu_dev *d) {
    unsigned char v;
    return rd(d, WHO_AM_I, &v, 1) ? -1 : v;
}

int imu_setup(imu_dev *d, double rate, int gyro_dps, int accel_g) {
    static const int gfs[] = { 125, 250, 500, 1000, 2000 };
    static const unsigned char gbits[] = { 0x02, 0x00, 0x04, 0x08, 0x0C };
    static const int afs[] = { 2, 4, 8, 16 };
    static const unsigned char abits[] = { 0x00, 0x08, 0x0C, 0x04 };
    unsigned char v, g = 0xFF, a = 0xFF;
    int i;

    for (i = 0; i < 5; i++) if (gfs[i] == gyro_dps) g = gbits[i];
    for (i = 0; i < 4; i++) if (afs[i] == accel_g) a = abits[i];
    if (g == 0xFF || a == 0xFF) return -1;
    d->odr_code = 1;
    for (i = 2; i <= 10; i++)
        if (fabs(odr_of(i) - rate) < fabs(odr_of(d->odr_code) - rate)) d->odr_code = i;
    d->odr_nominal = odr_of(d->odr_code);
    d->gyro_dps = gyro_dps;
    d->accel_g = accel_g;
    d->dps_lsb = gyro_dps * 0.035 / 1000;    /* 70 mdps per count at 2000 dps */
    d->g_lsb = accel_g * 0.0000305;          /* 0.061 mg per count at 2 g */

    if (wr(d, CTRL3_C, CTRL3_SW_RESET)) return -1;
    for (i = 0; i < 100; i++) {
        usleep(1000);
        if (rd(d, CTRL3_C, &v, 1)) return -1;
        if (!(v & CTRL3_SW_RESET)) break;
    }
    if (i == 100) return -1;
    if (wr(d, CTRL3_C, CTRL3_BDU | CTRL3_IF_INC) || wr(d, CTRL9_XL, CTRL9_I3C_OFF)) return -1;

    /* factory trim of the chip's oscillator, in steps of 0.15% */
    if (rd(d, INTERNAL_FREQ_FINE, &v, 1)) return -1;
    d->trim_pct = 0.15 * (signed char)v;
    d->odr_hz = d->odr_nominal * (1 + d->trim_pct / 100);

    if (wr(d, FIFO_CTRL4, 0)) return -1;
    if (wr(d, CTRL1_XL, d->odr_code << 4 | a) || wr(d, CTRL2_G, d->odr_code << 4 | g)) return -1;
    usleep(200000);                          /* gyro start-up and filter settling */
    return 0;
}

int imu_read_now(imu_dev *d, imu_sample *s, double *temp_c) {
    unsigned char b[14];
    if (rd(d, OUT_TEMP_L, b, sizeof(b))) return -1;
    if (temp_c) *temp_c = 25 + le16(b) / 256.0;
    for (int i = 0; i < 3; i++) {
        s->g[i] = le16(b + 2 + 2 * i);
        s->a[i] = le16(b + 8 + 2 * i);
    }
    return 0;
}

int imu_fifo_start(imu_dev *d) {
    d->have_g = d->have_a = d->seen_g = d->fill = 0;
    d->overruns = d->cnt_jumps = d->unpaired = d->other_words = 0;
    memset(&d->pend, 0, sizeof(d->pend));
    if (wr(d, FIFO_CTRL4, 0) || wr(d, FIFO_CTRL1, 0) || wr(d, FIFO_CTRL2, 0)) return -1;
    if (wr(d, FIFO_CTRL3, d->odr_code << 4 | d->odr_code)) return -1;
    return wr(d, FIFO_CTRL4, 0x06);
}

/* one FIFO word in; returns 1 when it completes a sample */
static int fifo_word(imu_dev *d, const unsigned char *w, imu_sample *out) {
    int tag = w[0] >> 3, cnt = (w[0] >> 1) & 3, n = 0;
    short *v;

    if (tag == TAG_GYRO) {
        if (d->seen_g && cnt != ((d->last_cnt + 1) & 3)) d->cnt_jumps++;
        d->seen_g = 1;
        d->last_cnt = cnt;
        if (d->have_g) {          /* no accel word came in between: reuse the last one */
            *out = d->pend;
            d->unpaired++;
            n = 1;
        }
        d->have_g = 1;
        v = d->pend.g;
    } else if (tag == TAG_ACCEL) {
        if (d->have_a) d->unpaired++;
        d->have_a = 1;
        v = d->pend.a;
    } else {
        d->other_words++;
        return 0;
    }
    for (int i = 0; i < 3; i++) v[i] = le16(w + 1 + 2 * i);
    if (d->have_g && d->have_a) {
        *out = d->pend;
        d->have_g = d->have_a = 0;
        n = 1;
    }
    return n;
}

int imu_fifo_read(imu_dev *d, imu_sample *out, int max) {
    unsigned char st[2], buf[CHUNK_WORDS * WORD];
    int words, n = 0, chunk = d->chunk > 0 && d->chunk < CHUNK_WORDS ? d->chunk : CHUNK_WORDS;

    d->ntags = 0;
    if (rd(d, FIFO_STATUS1, st, 2)) return -1;
    words = st[0] | (st[1] & 0x03) << 8;
    if (st[1] & FIFO_OVR) d->overruns++;
    d->fill = words;
    if (words > max) words = max;            /* a word completes at most one sample */
    while (words > 0) {
        int k = words < chunk ? words : chunk;
        if (rd(d, FIFO_DATA_OUT_TAG, buf, k * WORD)) return -1;
        for (int i = 0; i < k; i++) {
            if (d->ntags < 4) d->tags[d->ntags++] = buf[i * WORD];
            n += fifo_word(d, buf + i * WORD, out + n);
        }
        words -= k;
    }
    return n;
}

void imu_stop(imu_dev *d) {
    wr(d, FIFO_CTRL4, 0);
    wr(d, CTRL1_XL, 0);
    wr(d, CTRL2_G, 0);
}

void imu_gcsv_header(const imu_dev *d, FILE *f) {
    fprintf(f, "GYROFLOW IMU LOG\nversion,1.3\nid,open-dvr imutest\norientation,XYZ\n");
    fprintf(f, "note,LSM6DSOX %.1f Hz %d dps %d g\n", d->odr_nominal, d->gyro_dps, d->accel_g);
    fprintf(f, "tscale,0.000001\n");
    fprintf(f, "gscale,%.11g\n", d->dps_lsb * M_PI / 180);   /* counts to rad/s */
    fprintf(f, "ascale,%.11g\n", d->g_lsb);                  /* counts to g */
    fprintf(f, "t,gx,gy,gz,ax,ay,az\n");
}

void imu_gcsv_row(FILE *f, long long t_us, const imu_sample *s) {
    fprintf(f, "%lld,%d,%d,%d,%d,%d,%d\n", t_us, s->g[0], s->g[1], s->g[2],
            s->a[0], s->a[1], s->a[2]);
}
