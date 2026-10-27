#pragma once

/*
 * The board's button and LEDs, as plain GPIO pins through /dev/mem (this kernel has no
 * GPIO sysfs). The pins come from a small text file, /home/dvr/gpio.txt, written by
 * gpioprobe or by hand:
 *     Button = GPIO5_3 0          pin, then its level while pressed
 *     RecLed = GPIO6_2 1          pin, then the level that lights it
 *     PowerLed = GPIO6_1 1
 *     Mux = 0x100c0010 0x1000     optional: pin function register to set first
 */

#define BIO_MAX_MUX 16

typedef struct { int bank, bit, level; } bio_pin;  /* bank -1: not configured */

typedef struct {
    bio_pin button, recLed, powerLed;
    unsigned int muxCount;
    struct { unsigned long addr; unsigned int value; } mux[BIO_MAX_MUX];
} bio_cfg;

/* Reads the pin file. Returns how many pins it configures (0 if there is no file). */
int bio_load(const char *path, bio_cfg *c);

/* Maps the GPIO banks, writes the Mux lines, makes the button an input and the LEDs
 * outputs (recording LED off, power LED on). Returns 0 on success. */
int bio_open(const bio_cfg *c);

/* 1 while the pin is at its configured level (button pressed). */
int bio_get(const bio_pin *p);

/* Lights (on = 1) or darkens an LED pin. Unconfigured pins are ignored. */
void bio_set(const bio_pin *p, int on);
