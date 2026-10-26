#pragma once

/*
 * The toolchain's uClibc-ng is built with 64-bit time_t (__UCLIBC_USE_TIME64__), but the
 * board runs uClibc 0.9.33 with 32-bit time_t. Any libc call that passes a time struct
 * (gettimeofday, select, nanosleep, clock_gettime, time, stat, localtime...) would use
 * the wrong layout, so programs here must not use them. scripts/check_links.py rejects
 * binaries that import them. Use these helpers and poll() instead.
 */
#include <sys/syscall.h>
#include <unistd.h>

/* 32-bit ARM EABI clock_gettime, kernel 4.9 (no clock_gettime64 there) */
#define COMPAT_NR_CLOCK_GETTIME 263
#define COMPAT_CLOCK_MONOTONIC 1

/* Monotonic time in microseconds. */
static inline unsigned long long mono_us(void) {
    struct { long sec, nsec; } ts = { 0, 0 };
    syscall(COMPAT_NR_CLOCK_GETTIME, COMPAT_CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.sec * 1000000ULL + (unsigned long long)ts.nsec / 1000ULL;
}
