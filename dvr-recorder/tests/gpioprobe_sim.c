/*
 * Host test for gpioprobe's watching and detection, on a simulated board in memory:
 *   GPIO6_2 recording LED (output, lit at 1): 200 ms flash every second
 *   GPIO6_1 power LED (output, lit at 1): flashes 3 times in the first second, then on
 *   GPIO5_3 button (input, pull-up: reads 0 when pressed): 3 presses after 5 s, with bounce
 *   GPIO2_0 reset line (output): pulsed low once at 0.5 s (must not look like an LED)
 *   iocfg_ahb+0x010 changes 0x1003 -> 0x1000 at 0.3 s (the stock recorder's pin setup)
 * Runs 12 s with shortened thresholds, then checks the written gpio.txt with dvrrec's
 * parser (boardio.c).
 */
#define LATE_US 3000000ULL
#define BTN_AFTER_US 5000000ULL
#define main gpioprobe_main
#include "../src/gpioprobe.c"
#undef main

#include <pthread.h>

#include "../src/boardio.h"

static unsigned int gpioMem[0x10000 / 4], muxMem[MUX_BLKS][0x1000 / 4], crgMem[0x1000 / 4];

static void pin_set(int bank, int bit, int v) {
    unsigned int *d = &gpioMem[(bank * 0x1000 + 0x3fc) / 4];
    *d = v ? *d | (1u << bit) : *d & ~(1u << bit);
}

static volatile int simRunning = 1;

static void *board(void *arg) {
    unsigned long long t0 = mono_us();
    (void)arg;
    gpioMem[(6 * 0x1000 + 0x400) / 4] = (1u << 2) | (1u << 1);  /* LEDs are outputs */
    gpioMem[(2 * 0x1000 + 0x400) / 4] = 1u << 0;
    pin_set(2, 0, 1);
    pin_set(5, 3, 1);  /* button released */
    muxMem[2][0x10 / 4] = 0x1003;
    while (simRunning) {
        unsigned long long t = mono_us() - t0, ms = t / 1000;
        pin_set(6, 2, ms % 1000 < 200);
        pin_set(6, 1, ms < 1000 ? (ms / 100) % 2 : 1);
        pin_set(2, 0, !(ms >= 500 && ms < 520));
        if (ms >= 300) muxMem[2][0x10 / 4] = 0x1000;
        {
            int pressed = 0;
            for (int k = 0; k < 3; k++) {
                long long s = 6000 + k * 1500, e = s + 150;
                if ((long long)ms >= s && (long long)ms < e) pressed = 1;
                if ((long long)ms >= s && (long long)ms < s + 6 && ms % 2) pressed = 0;  /* bounce */
            }
            pin_set(5, 3, !pressed);
        }
        usleep(500);
    }
    return NULL;
}

int main(void) {
    pthread_t th;
    bio_cfg c;
    FILE *ev;
    int ok = 1;
    const char *dir = "build/host";

    gpio = gpioMem;
    crg = crgMem;
    lsadc = NULL;  /* clock bit is 0, so it is never read */
    for (int k = 0; k < MUX_BLKS; k++) { mux[k] = muxMem[k]; muxOk[k] = 1; }
    for (int b = 0; b < BANKS; b++) bankOk[b] = b != 3;
    pthread_create(&th, NULL, board, NULL);
    usleep(20000);
    read_mux(muxBoot);
    ev = fopen("build/host/17_gpio_events.txt", "w");
    watch(ev, 12);
    snapshot(ev, "registers at the end");
    fclose(ev);
    simRunning = 0;
    pthread_join(th, NULL);
    results(dir, "build/host/gpio.txt", 12);

    if (bio_load("build/host/gpio.txt", &c) != 3) { printf("FAIL: expected 3 pins\n"); ok = 0; }
#define CHECK(p, b, i, l) if (p.bank != b || p.bit != i || p.level != l) { \
        printf("FAIL: " #p " = GPIO%d_%d %d, expected GPIO%d_%d %d\n", p.bank, p.bit, p.level, b, i, l); ok = 0; }
    CHECK(c.button, 5, 3, 0)
    CHECK(c.recLed, 6, 2, 1)
    CHECK(c.powerLed, 6, 1, 1)
    if (c.muxCount != 1 || c.mux[0].addr != 0x100c0010UL || c.mux[0].value != 0x1000) {
        printf("FAIL: mux lines %u\n", c.muxCount);
        ok = 0;
    }
    printf("%s\n", ok ? "gpioprobe_sim: OK" : "gpioprobe_sim: FAILED");
    return !ok;
}
