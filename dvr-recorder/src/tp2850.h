#pragma once

/* Techpoint TP2850 analog video decoder on the DVR board: I2C bus 0, address 0x44. */

enum tp_std { TP_PAL, TP_NTSC };

/* Opens /dev/i2c-0; returns 0 on success. */
int tp_open(void);
void tp_close(void);

/* Programs the decoder for PAL or NTSC CVBS.
 * fieldMode = 1: one field per MIPI frame (288/240 lines at 50/60 fps)
 * fieldMode = 0: both fields stacked in one MIPI frame (576/480 lines at 25/30 fps),
 *                the way the stock firmware runs it. */
int tp_init(enum tp_std std, int fieldMode);

/* Reads a decoder-page register (page 0); returns the value or -1. */
int tp_read(unsigned char reg);

/* Prints chip ID and status registers to stdout. */
void tp_dump_status(void);
