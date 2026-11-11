/*
 * LSM6DSOX gyro + accelerometer: setup, direct reads, FIFO streaming and the Gyroflow
 * log format. Bus access goes through two callbacks, so the same code runs on I2C, on
 * SPI and against the simulated chip in tests/imusim.c.
 */
#ifndef LSM6DSOX_H
#define LSM6DSOX_H

#include <stdio.h>

#define IMU_ID 0x6C          /* WHO_AM_I of the LSM6DSOX */
#define IMU_FIFO_WORDS 512   /* FIFO depth in 7-byte words (gyro + accel = 2 words) */

typedef struct {
    int (*rd)(void *ctx, unsigned char reg, unsigned char *buf, int n);  /* 0 = ok */
    int (*wr)(void *ctx, unsigned char reg, unsigned char val);          /* 0 = ok */
    void *ctx;
} imu_bus;

typedef struct { short g[3], a[3]; } imu_sample;   /* raw counts */

typedef struct {
    imu_bus bus;
    int odr_code;             /* ODR field of CTRL1_XL / CTRL2_G */
    double odr_nominal;       /* Hz, from the datasheet table */
    double odr_hz;            /* Hz, corrected with the chip's factory trim */
    double trim_pct;          /* that correction in percent */
    int gyro_dps, accel_g;    /* full scales */
    double dps_lsb, g_lsb;    /* deg/s and g per count */

    /* FIFO: gyro and accel words are paired into samples */
    imu_sample pend;
    int have_g, have_a, seen_g, last_cnt;
    int fill;                 /* words waiting at the last imu_fifo_read() */
    int chunk;                /* FIFO words per bus read, 1..16 (0 = 16) */
    unsigned char tags[4];    /* first tag bytes of the last read, for -v */
    int ntags;
    unsigned long long overruns, cnt_jumps, unpaired, other_words;
} imu_dev;

int imu_id(imu_dev *d);       /* WHO_AM_I, or -1 on a bus error */
/* rate in Hz (nearest of 12.5 .. 6667), gyro_dps 125..2000, accel_g 2..16 */
int imu_setup(imu_dev *d, double rate, int gyro_dps, int accel_g);
int imu_read_now(imu_dev *d, imu_sample *s, double *temp_c);
int imu_fifo_start(imu_dev *d);
/* drains the FIFO into out (room for max samples); returns the count or -1 */
int imu_fifo_read(imu_dev *d, imu_sample *out, int max);
void imu_stop(imu_dev *d);

/* Gyroflow .gcsv: raw counts with scale factors, t in microseconds */
void imu_gcsv_header(const imu_dev *d, FILE *f);
void imu_gcsv_row(FILE *f, long long t_us, const imu_sample *s);

#endif
