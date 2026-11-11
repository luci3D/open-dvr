/*
 * Host test for src/lsm6dsox.c against a simulated chip: checks the setup register
 * values, the direct read, FIFO pairing in both word orders, overrun reporting and the
 * .gcsv output.
 *   imusim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/lsm6dsox.h"

#define QMAX (IMU_FIFO_WORDS * 7)

static unsigned char reg[128];
static unsigned char q[QMAX];          /* FIFO contents, oldest first */
static int qlen, overrun, slot;
static int slots_per_poll, accel_first, extra_words, bus_down;
static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void push_word(int tag, int cnt, int x, int y, int z) {
    unsigned char w[7] = { (unsigned char)(tag << 3 | cnt << 1), x & 0xFF, (x >> 8) & 0xFF,
                           y & 0xFF, (y >> 8) & 0xFF, z & 0xFF, (z >> 8) & 0xFF };
    if (qlen == QMAX) {                /* continuous mode: the oldest word is lost */
        memmove(q, q + 7, QMAX - 7);
        qlen -= 7;
        overrun = 1;
    }
    memcpy(q + qlen, w, 7);
    qlen += 7;
}

/* one time slot: gyro (i, -i, 2i) and accel (i + 1000, 7, -7) */
static void push_slot(void) {
    int i = slot++, cnt = i & 3;
    if (accel_first) push_word(2, cnt, i + 1000, 7, -7);
    push_word(1, cnt, i, -i, 2 * i);
    if (!accel_first) push_word(2, cnt, i + 1000, 7, -7);
    if (extra_words) push_word(4, cnt, 1, 2, 3);     /* a timestamp word */
}

static int sim_rd(void *ctx, unsigned char r, unsigned char *buf, int n) {
    (void)ctx;
    if (bus_down) return -1;
    if (r == 0x3A && n == 2) {         /* FIFO status: new slots arrive between polls */
        if (reg[0x0A] == 0x06)
            for (int i = 0; i < slots_per_poll; i++) push_slot();
        buf[0] = (qlen / 7) & 0xFF;
        buf[1] = (unsigned char)((qlen / 7) >> 8 | (overrun ? 0x48 : 0));
        overrun = 0;
        return 0;
    }
    if (r == 0x78) {                   /* FIFO data, the address wraps every 7 bytes */
        if (n % 7 || n > qlen) return -1;
        memcpy(buf, q, n);
        memmove(q, q + n, qlen - n);
        qlen -= n;
        return 0;
    }
    if (r + n > (int)sizeof(reg)) return -1;
    memcpy(buf, reg + r, n);
    return 0;
}

static int sim_wr(void *ctx, unsigned char r, unsigned char v) {
    (void)ctx;
    if (r >= sizeof(reg)) return -1;
    if (r == 0x12) v &= ~1;            /* software reset finishes at once */
    if (r == 0x0A && v == 0) qlen = 0; /* bypass mode clears the FIFO */
    reg[r] = v;
    return 0;
}

static void sim_reset(imu_dev *d) {
    memset(reg, 0, sizeof(reg));
    memset(d, 0, sizeof(*d));
    reg[0x0F] = IMU_ID;
    reg[0x63] = (unsigned char)-3;     /* oscillator trim: -0.45% */
    qlen = overrun = slot = accel_first = extra_words = bus_down = 0;
    slots_per_poll = 10;
    d->bus.rd = sim_rd;
    d->bus.wr = sim_wr;
}

/* drains the FIFO for some polls and checks every sample against its slot number */
static unsigned long long drain(imu_dev *d, int polls, int first) {
    static imu_sample s[IMU_FIFO_WORDS];
    unsigned long long total = 0;
    for (int p = 0; p < polls; p++) {
        int n = imu_fifo_read(d, s, IMU_FIFO_WORDS);
        CHECK(n >= 0);
        for (int i = 0; i < n; i++) {
            int k = first + (int)total + i;
            CHECK(s[i].g[0] == (short)k && s[i].g[1] == (short)-k && s[i].g[2] == (short)(2 * k));
            CHECK(s[i].a[0] == (short)(k + 1000) && s[i].a[1] == 7 && s[i].a[2] == -7);
        }
        total += n;
    }
    return total;
}

int main(void) {
    imu_dev d;
    imu_sample s;
    double temp;
    char *text = NULL;
    size_t len = 0;
    FILE *f;

    /* setup: 416 Hz, 2000 dps, 16 g */
    sim_reset(&d);
    CHECK(imu_id(&d) == IMU_ID);
    CHECK(imu_setup(&d, 416, 2000, 16) == 0);
    CHECK(reg[0x10] == 0x64 && reg[0x11] == 0x6C && reg[0x12] == 0x44 && reg[0x18] == 0xE2);
    CHECK(d.odr_nominal > 416.6 && d.odr_nominal < 416.8);
    CHECK(d.odr_hz > 414.7 && d.odr_hz < 414.9);
    CHECK(d.dps_lsb > 0.0699 && d.dps_lsb < 0.0701 && d.g_lsb > 0.000487 && d.g_lsb < 0.000489);
    CHECK(imu_setup(&d, 833, 300, 16) == -1 && imu_setup(&d, 833, 500, 3) == -1);
    CHECK(imu_setup(&d, 1660, 125, 2) == 0 && reg[0x10] == 0x80 && reg[0x11] == 0x82);
    CHECK(imu_setup(&d, 833, 250, 4) == 0 && reg[0x10] == 0x78 && reg[0x11] == 0x70);
    CHECK(imu_setup(&d, 104, 500, 8) == 0 && reg[0x10] == 0x4C && reg[0x11] == 0x44);
    CHECK(imu_setup(&d, 208, 1000, 2) == 0 && reg[0x10] == 0x50 && reg[0x11] == 0x58);

    /* direct read: temp 26.5 C, gyro (1, -2, 3), accel (-4, 5, 16384) */
    {
        static const unsigned char out[14] = { 0x80, 0x01, 1, 0, 0xFE, 0xFF, 3, 0,
                                               0xFC, 0xFF, 5, 0, 0x00, 0x40 };
        memcpy(reg + 0x20, out, sizeof(out));
        CHECK(imu_read_now(&d, &s, &temp) == 0);
        CHECK(temp == 26.5 && s.g[0] == 1 && s.g[1] == -2 && s.g[2] == 3);
        CHECK(s.a[0] == -4 && s.a[1] == 5 && s.a[2] == 16384);
    }

    /* FIFO, gyro word first: 10 slots per poll, nothing lost */
    sim_reset(&d);
    CHECK(imu_setup(&d, 416, 2000, 16) == 0 && imu_fifo_start(&d) == 0);
    CHECK(reg[0x09] == 0x66 && reg[0x0A] == 0x06);
    CHECK(drain(&d, 100, 0) == 1000);
    CHECK(d.overruns == 0 && d.cnt_jumps == 0 && d.unpaired == 0 && d.other_words == 0 && d.fill == 20);

    /* smaller bus reads give the same stream */
    for (int k = 1; k <= 5; k += 4) {
        sim_reset(&d);
        d.chunk = k;
        CHECK(imu_setup(&d, 416, 2000, 16) == 0 && imu_fifo_start(&d) == 0);
        CHECK(drain(&d, 100, 0) == 1000);
        CHECK(d.overruns == 0 && d.cnt_jumps == 0 && d.unpaired == 0 && d.ntags == 4 && d.tags[0] >> 3 == 1 && d.tags[1] >> 3 == 2);
    }

    /* accel word first, with a timestamp word in every slot */
    sim_reset(&d);
    accel_first = extra_words = 1;
    CHECK(imu_setup(&d, 416, 2000, 16) == 0 && imu_fifo_start(&d) == 0);
    CHECK(drain(&d, 100, 0) == 1000);
    CHECK(d.overruns == 0 && d.cnt_jumps == 0 && d.unpaired == 0 && d.other_words == 1000);

    /* 300 slots between polls do not fit in 512 words: the overrun must be reported */
    sim_reset(&d);
    slots_per_poll = 300;
    CHECK(imu_setup(&d, 416, 2000, 16) == 0 && imu_fifo_start(&d) == 0);
    CHECK(drain(&d, 1, 300 - IMU_FIFO_WORDS / 2) == IMU_FIFO_WORDS / 2);
    CHECK(d.overruns == 1 && d.fill == IMU_FIFO_WORDS);

    /* a bus error comes back as -1 */
    sim_reset(&d);
    CHECK(imu_setup(&d, 416, 2000, 16) == 0 && imu_fifo_start(&d) == 0);
    bus_down = 1;
    CHECK(imu_fifo_read(&d, &s, 1) == -1 && imu_id(&d) == -1 && imu_read_now(&d, &s, &temp) == -1);

    /* .gcsv text */
    sim_reset(&d);
    CHECK(imu_setup(&d, 416, 2000, 16) == 0);
    f = open_memstream(&text, &len);
    imu_gcsv_header(&d, f);
    s.g[0] = 1; s.g[1] = -2; s.g[2] = 3; s.a[0] = -4; s.a[1] = 5; s.a[2] = 2049;
    imu_gcsv_row(f, 123456789012LL, &s);
    fclose(f);
    CHECK(!strcmp(text, "GYROFLOW IMU LOG\nversion,1.3\nid,open-dvr imutest\norientation,XYZ\n"
                        "note,LSM6DSOX 416.7 Hz 2000 dps 16 g\ntscale,0.000001\n"
                        "gscale,0.0012217304764\nascale,0.000488\n"
                        "t,gx,gy,gz,ax,ay,az\n123456789012,1,-2,3,-4,5,2049\n"));
    if (fails) printf("%s", text);
    free(text);

    printf(fails ? "imusim: %d check(s) failed\n" : "imusim: all checks passed\n", fails);
    return fails != 0;
}
