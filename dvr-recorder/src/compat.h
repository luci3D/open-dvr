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

#if !defined(__arm__)
/* Host builds (tests in tests/): the syscall numbers below are ARM's, so use the host
 * libc, whose structs match its own kernel. */
#include <sys/statvfs.h>
#include <time.h>

static inline unsigned long long mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)ts.tv_nsec / 1000ULL;
}

static inline long long free_bytes(const char *path) {
    struct statvfs s;
    if (statvfs(path, &s)) return -1;
    return (long long)s.f_bavail * (long long)s.f_frsize;
}
#else

/* 32-bit ARM EABI clock_gettime, kernel 4.9 (no clock_gettime64 there) */
#define COMPAT_NR_CLOCK_GETTIME 263
#define COMPAT_CLOCK_MONOTONIC 1

/* Monotonic time in microseconds. */
static inline unsigned long long mono_us(void) {
    struct { long sec, nsec; } ts = { 0, 0 };
    syscall(COMPAT_NR_CLOCK_GETTIME, COMPAT_CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.sec * 1000000ULL + (unsigned long long)ts.nsec / 1000ULL;
}

/* ARM EABI statfs64 (kernel layout: packed, 4-byte aligned, 84 bytes). Called directly so
 * the struct always matches the kernel, whatever the two libcs think struct statfs is. */
#define COMPAT_NR_STATFS64 266
struct compat_statfs64 {
    unsigned int f_type, f_bsize;
    unsigned long long f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int f_fsid[2];
    unsigned int f_namelen, f_frsize, f_flags, f_spare[4];
} __attribute__((packed, aligned(4)));
_Static_assert(sizeof(struct compat_statfs64) == 84, "statfs64 layout");

/* Free space for unprivileged writers on the file system holding path, or -1. */
static inline long long free_bytes(const char *path) {
    struct compat_statfs64 s;
    if (syscall(COMPAT_NR_STATFS64, path, sizeof(s), &s)) return -1;
    return (long long)(s.f_bavail * s.f_bsize);
}
#endif
