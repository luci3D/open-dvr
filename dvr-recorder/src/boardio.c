/*
 * Button and LEDs over the SoC's GPIO banks. Register layout from OpenIPC openhisilicon
 * (kernel/sys_config/sys_config.c): bank n at 0x120b0000 + n * 0x1000, PL061 style.
 * DATA is at +0x000..+0x3fc, where address bits 9:2 select the pins a read or write
 * touches (so one pin can be written without disturbing the others), DIR at +0x400.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "boardio.h"

#define GPIO_BASE 0x120b0000UL
#define GPIO_LEN 0xa000UL  /* banks 0-9 */

static volatile unsigned int *gpio;

/* Pin function registers live in the three IO config blocks (offsets 0x00-0xfc used) */
static int mux_allowed(unsigned long a) {
    static const unsigned long base[] = { 0x112c0000UL, 0x120c0000UL, 0x100c0000UL };
    for (unsigned int i = 0; i < sizeof(base) / sizeof(base[0]); i++)
        if (a >= base[i] && a < base[i] + 0x100 && !(a & 3)) return 1;
    return 0;
}

static int parse_pin(const char *s, bio_pin *p) {
    int b, i, l;
    if (sscanf(s, " GPIO%d_%d %d", &b, &i, &l) != 3 || b < 0 || b > 9 || i < 0 || i > 7 || (l != 0 && l != 1))
        return -1;
    p->bank = b;
    p->bit = i;
    p->level = l;
    return 0;
}

int bio_load(const char *path, bio_cfg *c) {
    char line[160], key[32], val[96];
    FILE *f;
    int n = 0;

    memset(c, 0, sizeof(*c));
    c->button.bank = c->recLed.bank = c->powerLed.bank = -1;
    if (!(f = fopen(path, "r"))) return 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " %31[^=# ] = %95[^\n#]", key, val) != 2) continue;
        if (!strcmp(key, "Button")) n += !parse_pin(val, &c->button);
        else if (!strcmp(key, "RecLed")) n += !parse_pin(val, &c->recLed);
        else if (!strcmp(key, "PowerLed")) n += !parse_pin(val, &c->powerLed);
        else if (!strcmp(key, "Mux") && c->muxCount < BIO_MAX_MUX) {
            unsigned long a;
            unsigned int v;
            if (sscanf(val, "%lx %x", &a, &v) == 2 && mux_allowed(a)) {
                c->mux[c->muxCount].addr = a;
                c->mux[c->muxCount].value = v;
                c->muxCount++;
            } else {
                printf("  gpio.txt: ignored Mux line %s", line);
            }
        }
    }
    fclose(f);
    return n;
}

#define REG(off) gpio[(off) / 4]

static void set_dir(const bio_pin *p, int out) {
    unsigned int off = p->bank * 0x1000 + 0x400, d = REG(off);
    REG(off) = out ? d | (1u << p->bit) : d & ~(1u << p->bit);
}

int bio_open(const bio_cfg *c) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    void *m;

    if (fd < 0) return -1;
    for (unsigned int i = 0; i < c->muxCount; i++) {
        unsigned long page = c->mux[i].addr & ~0xfffUL;
        m = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page);
        if (m == MAP_FAILED) continue;
        ((volatile unsigned int *)m)[(c->mux[i].addr - page) / 4] = c->mux[i].value;
        munmap(m, 0x1000);
    }
    m = mmap(NULL, GPIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_BASE);
    close(fd);
    if (m == MAP_FAILED) return -1;
    gpio = (volatile unsigned int *)m;
    if (c->button.bank >= 0) set_dir(&c->button, 0);
    /* the level is set before the pin becomes an output, so it never glitches */
    if (c->recLed.bank >= 0) { bio_set(&c->recLed, 0); set_dir(&c->recLed, 1); }
    if (c->powerLed.bank >= 0) { bio_set(&c->powerLed, 1); set_dir(&c->powerLed, 1); }
    return 0;
}

int bio_get(const bio_pin *p) {
    if (!gpio || p->bank < 0) return 0;
    return (int)((REG(p->bank * 0x1000 + 0x3fc) >> p->bit) & 1) == p->level;
}

void bio_set(const bio_pin *p, int on) {
    if (!gpio || p->bank < 0) return;
    /* masked write: address bit (bit + 2) selects this pin only */
    REG(p->bank * 0x1000 + (4u << p->bit)) = (unsigned int)(on ? p->level : !p->level) << p->bit;
}
