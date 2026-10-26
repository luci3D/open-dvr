/*
 * TP2850 setup over Linux i2c-dev.
 *
 * Register values: the decoder tables match the TP2850 register dumps taken from this
 * board while the stock firmware ran (PAL and NTSC), and the write sequence follows the
 * open-source Rockchip techpoint TP9950 driver (GPL-2.0), a register-compatible sibling.
 * Page select is register 0x40: 0x00 = decoder page, 0x08 = MIPI page.
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "tp2850.h"

#define TP_BUS "/dev/i2c-0"
#define TP_ADDR 0x44
#define I2C_SLAVE_FORCE 0x0706

typedef struct { unsigned char reg, val; } tp_reg;

/* MIPI page then decoder page, common to all CVBS modes */
static const tp_reg common[] = {
    {0x41, 0x00}, {0x40, 0x08},
    {0x01, 0xf8}, {0x02, 0x01}, {0x08, 0x03}, {0x20, 0x12}, {0x39, 0x00},
    {0x40, 0x00},
    {0x4c, 0x40}, {0x4e, 0x00}, {0x27, 0x2d}, {0xfd, 0x80},
};

static const tp_reg pal[] = {
    {0x02, 0x47}, {0x07, 0x80}, {0x0b, 0x80}, {0x0c, 0x13}, {0x0d, 0x51}, {0x15, 0x13},
    {0x16, 0x76}, {0x17, 0x80}, {0x18, 0x17}, {0x19, 0x20}, {0x1a, 0x17}, {0x1c, 0x09},
    {0x1d, 0x48}, {0x20, 0x48}, {0x21, 0x84}, {0x22, 0x37}, {0x23, 0x3f}, {0x2b, 0x70},
    {0x2c, 0x2a}, {0x2d, 0x64}, {0x2e, 0x56}, {0x30, 0x7a}, {0x31, 0x4a}, {0x32, 0x4d},
    {0x33, 0xf0}, {0x35, 0x65}, {0x38, 0x00}, {0x39, 0x04},
};

static const tp_reg ntsc[] = {
    {0x02, 0x47}, {0x07, 0x80}, {0x0b, 0x80}, {0x0c, 0x13}, {0x0d, 0x50}, {0x15, 0x13},
    {0x16, 0x60}, {0x17, 0x80}, {0x18, 0x12}, {0x19, 0xf0}, {0x1a, 0x07}, {0x1c, 0x09},
    {0x1d, 0x38}, {0x20, 0x40}, {0x21, 0x84}, {0x22, 0x36}, {0x23, 0x3c}, {0x2b, 0x70},
    {0x2c, 0x2a}, {0x2d, 0x68}, {0x2e, 0x57}, {0x30, 0x62}, {0x31, 0xbb}, {0x32, 0x96},
    {0x33, 0xc0}, {0x35, 0x65}, {0x38, 0x00}, {0x39, 0x04},
};

/* MIPI page output setup; the last 0x23 write picks field or frame output */
static const tp_reg mipi[] = {
    {0x40, 0x08}, {0x23, 0x02}, {0x13, 0x24}, {0x14, 0x57}, {0x15, 0x0e}, {0x25, 0x02},
    {0x26, 0x00}, {0x27, 0x03}, {0x10, 0x88}, {0x10, 0x08},
};

static int fd = -1;

int tp_open(void) {
    fd = open(TP_BUS, O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE_FORCE, TP_ADDR) < 0) {
        close(fd);
        fd = -1;
        return -1;
    }
    return 0;
}

void tp_close(void) {
    if (fd >= 0) close(fd);
    fd = -1;
}

static int wr(unsigned char reg, unsigned char val) {
    unsigned char buf[2] = { reg, val };
    return write(fd, buf, 2) == 2 ? 0 : -1;
}

static int rd(unsigned char reg) {
    unsigned char v;
    if (write(fd, &reg, 1) != 1 || read(fd, &v, 1) != 1) return -1;
    return v;
}

static int wr_table(const tp_reg *t, int n) {
    int errors = 0;
    for (int i = 0; i < n; i++)
        if (wr(t[i].reg, t[i].val)) errors++;
    return errors;
}

int tp_init(enum tp_std std, int fieldMode) {
    int errors = 0;
    if (fd < 0) return -1;
    errors += wr_table(common, sizeof(common) / sizeof(*common));
    if (std == TP_PAL) errors += wr_table(pal, sizeof(pal) / sizeof(*pal));
    else errors += wr_table(ntsc, sizeof(ntsc) / sizeof(*ntsc));
    errors += wr_table(mipi, sizeof(mipi) / sizeof(*mipi));
    errors += wr(0x23, fieldMode ? 0x00 : 0x40);
    errors += wr(0x40, 0x00);
    return errors;
}

int tp_read(unsigned char reg) {
    if (fd < 0 || wr(0x40, 0x00)) return -1;
    return rd(reg);
}

void tp_dump_status(void) {
    int mipi23;
    printf("  tp2850: id %02x%02x  reg01 %02x  reg03 %02x  reg0d %02x",
        tp_read(0xfe), tp_read(0xff), tp_read(0x01), tp_read(0x03), tp_read(0x0d));
    wr(0x40, 0x08);
    mipi23 = rd(0x23);
    wr(0x40, 0x00);
    printf("  mipi23 %02x\n", mipi23);
}
