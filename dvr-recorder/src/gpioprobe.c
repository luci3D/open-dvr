/*
 * gpioprobe: finds the board's button and LEDs. It watches the GPIO, pin-function
 * (pinmux) and low-speed ADC registers while the stock recorder runs and someone taps
 * the button. It only reads hardware registers; it never writes one.
 *
 *   gpioprobe -s                          print every register once and exit
 *   gpioprobe [-t seconds] [-o dir] [-w file]
 *       watch for -t seconds (default 180). Every change goes to <dir>/17_gpio_events.txt,
 *       the result to <dir>/17_gpio_summary.txt and <dir>/gpio_suggested.txt. With -w the
 *       suggestion is also written to that file, but only when exactly one button and a
 *       recording LED were found (dvrrec reads it as /home/dvr/gpio.txt).
 *
 * Register addresses are from OpenIPC openhisilicon (kernel/sys_config/sys_config.c and
 * kernel/adc/adc.c) for the hi3516ev200 / gk7205v200 family this chip belongs to:
 *   GPIO bank n at 0x120b0000 + n * 0x1000: DATA (all 8 pins) at +0x3fc, DIR at +0x400
 *   pin functions: three IO config blocks at 0x112c0000, 0x120c0000 and 0x100c0000
 *   LSADC at 0x120a0000 (channel values at +0x2c, +0x30), read only while its clock is on
 *   (CRG 0x120101bc bit 23), so an unclocked block is never touched
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "compat.h"

#define GPIO_BASE 0x120b0000UL
#define BANKS 10
#define PINS (BANKS * 8)
#define CRG_BASE 0x12010000UL
#define CRG_LSADC 0x1bc
#define LSADC_CLK (1u << 23)
#define LSADC_BASE 0x120a0000UL
#define MUX_REGS 64             /* offsets 0x00-0xfc of each IO config block */
#define SAMPLE_US 2000
#ifndef LATE_US                 /* (tests/gpioprobe_sim.c shortens these) */
#define LATE_US 30000000ULL     /* changes after 30 s: the stock recorder is recording */
#endif
#ifndef BTN_AFTER_US
#define BTN_AFTER_US 5000000ULL /* button presses come after the stock recorder started */
#endif
#define PULSE_MIN_US 30000      /* shorter excursions are contact bounce */

static const struct { const char *name; unsigned long base; } muxBlk[] = {
    { "iocfg_vio", 0x112c0000UL }, { "iocfg_core", 0x120c0000UL }, { "iocfg_ahb", 0x100c0000UL },
};
#define MUX_BLKS 3

static volatile unsigned int *gpio, *crg, *lsadc, *mux[MUX_BLKS];
static int bankOk[BANKS], muxOk[MUX_BLKS];
static unsigned int muxBoot[MUX_BLKS][MUX_REGS], muxNow[MUX_BLKS][MUX_REGS];

#define RD(p, off) ((p)[(off) / 4])

static unsigned int gdata(int b) { return RD(gpio, b * 0x1000 + 0x3fc) & 0xff; }
static unsigned int gdir(int b) { return RD(gpio, b * 0x1000 + 0x400) & 0xff; }
static int adc_on(void) { return crg && (RD(crg, CRG_LSADC) & LSADC_CLK); }
static unsigned int adc_val(int ch) { return RD(lsadc, 0x2c + ch * 4) & 0x3ff; }

static volatile unsigned int *map(int fd, unsigned long base, unsigned long len) {
    void *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, base);
    return p == MAP_FAILED ? NULL : (volatile unsigned int *)p;
}

/* Reads one register in a child process first: a block that is not there ends the child
 * with a bus error instead of this program. */
static int readable(volatile unsigned int *p, unsigned int off) {
    int status = 0;
    pid_t pid;
    if (!p) return 0;
    pid = fork();
    if (pid == 0) {
        volatile unsigned int v = RD(p, off);
        (void)v;
        _exit(0);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int setup(void) {
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return -1; }
    gpio = map(fd, GPIO_BASE, 0x10000);
    crg = map(fd, CRG_BASE, 0x1000);
    lsadc = map(fd, LSADC_BASE, 0x1000);
    for (int k = 0; k < MUX_BLKS; k++) {
        mux[k] = map(fd, muxBlk[k].base, 0x1000);
        muxOk[k] = readable(mux[k], 0);
    }
    close(fd);
    if (!gpio) { printf("cannot map the GPIO banks\n"); return -1; }
    for (int b = 0; b < BANKS; b++) bankOk[b] = readable(gpio, b * 0x1000 + 0x400);
    if (!readable(crg, CRG_LSADC)) crg = NULL;
    return 0;
}

static void read_mux(unsigned int dst[MUX_BLKS][MUX_REGS]) {
    for (int k = 0; k < MUX_BLKS; k++)
        for (int i = 0; i < MUX_REGS; i++) dst[k][i] = muxOk[k] ? RD(mux[k], i * 4) : 0;
}

static void snapshot(FILE *f, const char *title) {
    unsigned int m[MUX_BLKS][MUX_REGS];
    fprintf(f, "### %s (uptime %.1f s)\n", title, mono_us() / 1e6);
    fprintf(f, "bank   DIR  DATA   (DIR bit 1 = output)\n");
    for (int b = 0; b < BANKS; b++) {
        if (bankOk[b]) fprintf(f, "GPIO%d  %02x   %02x\n", b, gdir(b), gdata(b));
        else fprintf(f, "GPIO%d  (no access)\n", b);
    }
    read_mux(m);
    for (int k = 0; k < MUX_BLKS; k++) {
        fprintf(f, "%s %#lx%s", muxBlk[k].name, muxBlk[k].base, muxOk[k] ? ":" : ": no access");
        for (int i = 0; muxOk[k] && i < MUX_REGS; i++)
            fprintf(f, "%s%03x=%04x", i % 8 ? " " : "\n  ", i * 4, m[k][i]);
        fprintf(f, "\n");
    }
    if (!crg) fprintf(f, "lsadc: clock register not readable\n");
    else if (!adc_on()) fprintf(f, "lsadc: clock off\n");
    else fprintf(f, "lsadc: config %08x ch0 %u ch1 %u\n", RD(lsadc, 0), adc_val(0), adc_val(1));
}

/* ---- watching ------------------------------------------------------------- */
typedef struct {
    int level, start, dir;              /* level now and at the start; DIR bit last seen */
    unsigned int n, nLate, pulses, logged;
    unsigned long long since, firstT, lastT, pulseMax, at[2];  /* microseconds */
} pin_t;

static pin_t pin[PINS];
static unsigned int adcMin[2] = { 0x3ff, 0x3ff }, adcMax[2], adcSeen;

static void watch(FILE *ev, unsigned int secs) {
    unsigned long long t0 = mono_us(), now, t, lastMux = 0, lastAdc = 0, lastSync = 0;
    unsigned int dirPrev[BANKS], adcLogged[2] = { 0, 0 }, events = 0, loop = 0;
    int adcWasOn = 0;

    for (int b = 0; b < BANKS; b++) {
        unsigned int d = bankOk[b] ? gdata(b) : 0, r = bankOk[b] ? gdir(b) : 0;
        dirPrev[b] = r;
        for (int i = 0; i < 8; i++) {
            pin_t *p = &pin[b * 8 + i];
            p->level = p->start = (d >> i) & 1;
            p->dir = (r >> i) & 1;
            p->since = t0;
        }
    }
    read_mux(muxNow);
    fprintf(ev, "# times in seconds from the start (uptime %.1f s); pin old->new and direction\n", t0 / 1e6);

    while ((now = mono_us()) - t0 < secs * 1000000ULL) {
        t = now - t0;
        for (int b = 0; b < BANKS; b++) {
            unsigned int d, r;
            if (!bankOk[b]) continue;
            d = gdata(b);
            r = gdir(b);
            if (r != dirPrev[b]) {
                if (events++ < 20000) fprintf(ev, "%9.3f GPIO%d DIR %02x -> %02x\n", t / 1e6, b, dirPrev[b], r);
                dirPrev[b] = r;
            }
            for (int i = 0; i < 8; i++) {
                pin_t *p = &pin[b * 8 + i];
                int lv = (d >> i) & 1;
                p->dir = (r >> i) & 1;
                if (lv == p->level) continue;
                p->at[p->level] += now - p->since;
                if (lv == p->start) {  /* back at the resting level: that was one pulse */
                    unsigned long long w = now - p->since;
                    if (w >= PULSE_MIN_US) {
                        p->pulses++;
                        if (w > p->pulseMax) p->pulseMax = w;
                    }
                }
                if (!p->n) p->firstT = t;
                p->lastT = t;
                p->n++;
                if (t >= LATE_US) p->nLate++;
                p->level = lv;
                p->since = now;
                if (p->logged++ < 300 && events++ < 20000)
                    fprintf(ev, "%9.3f GPIO%d_%d %d->%d %s\n", t / 1e6, b, i, !lv, lv, p->dir ? "out" : "in");
            }
        }
        if (now - lastMux >= 50000) {  /* pin functions, every 50 ms */
            lastMux = now;
            for (int k = 0; k < MUX_BLKS; k++)
                for (int i = 0; muxOk[k] && i < MUX_REGS; i++) {
                    unsigned int v = RD(mux[k], i * 4);
                    if (v == muxNow[k][i]) continue;
                    if (events++ < 20000)
                        fprintf(ev, "%9.3f %s+%03x %04x -> %04x\n", t / 1e6, muxBlk[k].name, i * 4, muxNow[k][i], v);
                    muxNow[k][i] = v;
                }
        }
        if (now - lastAdc >= 10000) {  /* low-speed ADC, every 10 ms while it is clocked */
            int on = adc_on();
            lastAdc = now;
            if (on != adcWasOn && events++ < 20000) fprintf(ev, "%9.3f lsadc clock %s\n", t / 1e6, on ? "on" : "off");
            adcWasOn = on;
            for (int ch = 0; on && ch < 2; ch++) {
                unsigned int v = adc_val(ch);
                adcSeen = 1;
                if (v < adcMin[ch]) adcMin[ch] = v;
                if (v > adcMax[ch]) adcMax[ch] = v;
                if ((v > adcLogged[ch] + 24 || v + 24 < adcLogged[ch]) && events++ < 20000) {
                    fprintf(ev, "%9.3f lsadc ch%d %u\n", t / 1e6, ch, v);
                    adcLogged[ch] = v;
                }
            }
        }
        if (now - lastSync >= 2000000ULL) {  /* keep the log on the card if power is cut */
            lastSync = now;
            fflush(ev);
            fsync(fileno(ev));
        }
        if (++loop % 5000 == 0) fprintf(ev, "%9.3f still watching\n", t / 1e6);
        usleep(SAMPLE_US);
    }
    for (int k = 0; k < PINS; k++) pin[k].at[pin[k].level] += mono_us() - pin[k].since;
}

/* ---- results -------------------------------------------------------------- */
static void pin_name(char *s, int k) { sprintf(s, "GPIO%d_%d", k / 8, k % 8); }

/* An output the stock recorder switched exactly once while starting, then held: on this
 * board the red (power) LED looks like that. So can an enable line, so it is only
 * reported, never written as a working line. */
static int steady_guess(int k) {
    return pin[k].dir && pin[k].n == 1 && pin[k].firstT < BTN_AFTER_US;
}

static void results(const char *dir, const char *autoFile, unsigned int secs) {
    char path[256], name[16];
    int led = -1, pwr = -1, btn = -1, nBtn = 0, muxChanged = 0;
    FILE *f, *g;

    snprintf(path, sizeof(path), "%s/17_gpio_summary.txt", dir);
    if (!(f = fopen(path, "w"))) { perror(path); return; }
    fprintf(f, "gpioprobe: %u s watched. Pins that changed:\n", secs);
    fprintf(f, "pin        dir  changes  after30s  pulses  longest   time at 0 / 1   first / last change\n");
    for (int k = 0; k < PINS; k++) {
        pin_t *p = &pin[k];
        if (!p->n) continue;
        pin_name(name, k);
        fprintf(f, "%-9s  %-3s  %7u  %8u  %6u  %5.2f s  %6.1f / %6.1f s  %6.1f / %6.1f s\n", name,
            p->dir ? "out" : "in", p->n, p->nLate, p->pulses, p->pulseMax / 1e6,
            p->at[0] / 1e6, p->at[1] / 1e6, p->firstT / 1e6, p->lastT / 1e6);
    }

    fprintf(f, "\nPin functions changed since boot (the stock recorder set these):\n");
    for (int k = 0; k < MUX_BLKS; k++)
        for (int i = 0; i < MUX_REGS; i++)
            if (muxNow[k][i] != muxBoot[k][i]) {
                fprintf(f, "  %s+%03x (%#lx): %04x -> %04x\n", muxBlk[k].name, i * 4,
                    muxBlk[k].base + i * 4, muxBoot[k][i], muxNow[k][i]);
                muxChanged++;
            }
    if (!muxChanged) fprintf(f, "  none\n");
    if (adcSeen)
        fprintf(f, "\nLSADC: ch0 %u..%u, ch1 %u..%u\n", adcMin[0], adcMax[0], adcMin[1], adcMax[1]);
    else
        fprintf(f, "\nLSADC: clock never on (not used by the stock recorder)\n");

    /* recording LED: an output that keeps blinking while the stock recorder records (but
     * not faster than a few times a second, which would be some other signal) */
    for (int k = 0; k < PINS; k++)
        if (pin[k].dir && pin[k].nLate >= 4 && pin[k].nLate <= 1000 &&
            (led < 0 || pin[k].nLate > pin[led].nLate)) led = k;
    /* power LED: another output that blinked a few times at start, then stayed put */
    for (int k = 0; k < PINS; k++)
        if (k != led && pin[k].dir && pin[k].n >= 4 && !pin[k].nLate && (pwr < 0 || pin[k].n > pin[pwr].n)) pwr = k;
    /* button: an input with a few separate presses, each shorter than 3 s, all after 5 s */
    for (int k = 0; k < PINS; k++) {
        pin_t *p = &pin[k];
        if (p->dir || p->pulses < 2 || p->pulses > 30 || p->pulseMax >= 3000000ULL || p->firstT < BTN_AFTER_US) continue;
        nBtn++;
        if (btn < 0 || p->pulses > pin[btn].pulses) btn = k;
    }

    fprintf(f, "\nResult:\n");
    if (btn >= 0) {
        pin_name(name, btn);
        fprintf(f, "  button: %s, reads %d when pressed (%u presses)%s\n", name, !pin[btn].start,
            pin[btn].pulses, nBtn > 1 ? ", BUT other inputs changed too (see the table)" : "");
    } else {
        fprintf(f, "  button: no GPIO input changed like a button%s\n",
            adcSeen && (adcMax[0] - adcMin[0] > 100 || adcMax[1] - adcMin[1] > 100) ? " (the LSADC moved: maybe an ADC button)" : "");
    }
    if (led >= 0) {
        pin_name(name, led);
        fprintf(f, "  recording LED: %s, lit at level %d\n", name, pin[led].at[0] < pin[led].at[1] ? 0 : 1);
    } else {
        fprintf(f, "  recording LED: no output kept blinking\n");
    }
    if (pwr >= 0) {
        pin_name(name, pwr);
        fprintf(f, "  power LED (guess): %s, lit at level %d\n", name, pin[pwr].level);
    }
    for (int k = 0; k < PINS; k++)
        if (pwr < 0 && steady_guess(k)) {
            pin_name(name, k);
            fprintf(f, "  set once at start and held (a steady LED, or an enable line): %s = %d\n", name, pin[k].level);
        }

    snprintf(path, sizeof(path), "%s/gpio_suggested.txt", dir);
    for (int pass = 0; pass < 2; pass++) {
        const char *out = pass ? autoFile : path;
        if (pass && (!autoFile || btn < 0 || nBtn != 1 || led < 0)) break;
        if (!(g = fopen(out, "w"))) { perror(out); continue; }
        fprintf(g, "# Button and LEDs for dvrrec, found by gpioprobe with the stock recorder running.\n"
                   "# Pin, then its level when pressed (button) or lit (LEDs).\n");
        if (btn >= 0) { pin_name(name, btn); fprintf(g, "Button = %s %d\n", name, !pin[btn].start); }
        if (led >= 0) { pin_name(name, led); fprintf(g, "RecLed = %s %d\n", name, pin[led].at[0] < pin[led].at[1] ? 0 : 1); }
        if (pwr >= 0) { pin_name(name, pwr); fprintf(g, "PowerLed = %s %d\n", name, pin[pwr].level); }
        for (int k = 0; k < PINS; k++)
            if (pwr < 0 && steady_guess(k)) {
                pin_name(name, k);
                fprintf(g, "# PowerLed = %s %d    (guess: switched once at start and held; check before using)\n",
                    name, pin[k].level);
            }
        fprintf(g, "# Pin functions the stock recorder changed after boot (written before the pins are used)\n");
        for (int k = 0; k < MUX_BLKS; k++)
            for (int i = 0; i < MUX_REGS; i++)
                if (muxNow[k][i] != muxBoot[k][i])
                    fprintf(g, "Mux = %#lx %#x\n", muxBlk[k].base + i * 4, muxNow[k][i]);
        fclose(g);
        fprintf(f, "  wrote %s\n", out);
    }
    if (autoFile && !(btn >= 0 && nBtn == 1 && led >= 0))
        fprintf(f, "  not sure enough to write %s: dvrrec keeps running without the button and LEDs\n", autoFile);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = "/home/probe", *autoFile = NULL;
    unsigned int secs = 180;
    int opt, snap = 0;
    char path[256];
    FILE *ev;

    while ((opt = getopt(argc, argv, "st:o:w:")) != -1) {
        switch (opt) {
            case 's': snap = 1; break;
            case 't': secs = atoi(optarg); break;
            case 'o': dir = optarg; break;
            case 'w': autoFile = optarg; break;
            default:
                fprintf(stderr, "usage: gpioprobe -s | [-t seconds] [-o dir] [-w gpio.txt]\n");
                return 2;
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (setup()) return 1;
    if (snap) {
        snapshot(stdout, "registers");
        return 0;
    }
    read_mux(muxBoot);
    snprintf(path, sizeof(path), "%s/17_gpio_events.txt", dir);
    if (!(ev = fopen(path, "w"))) { perror(path); return 1; }
    printf("gpioprobe: watching for %u s, events in %s\n", secs, path);
    watch(ev, secs);
    snapshot(ev, "registers at the end");
    fclose(ev);
    results(dir, autoFile, secs);
    printf("gpioprobe: done, see %s/17_gpio_summary.txt\n", dir);
    return 0;
}
