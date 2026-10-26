/*
 * dvrtest: first on-board check for the open DVR recorder.
 * Proves that a binary built with the Bootlin toolchain runs on the board's
 * uClibc 0.9.33 userland and can call the Goke MPP library (libgk_api.so).
 * Read-only: queries versions/IDs only, no video pipeline is started.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

typedef struct {
    char version[64];
} mpp_version;

extern int GK_API_SYS_GetVersion(mpp_version *version);
extern int GK_API_SYS_GetChipId(unsigned int *chipId);

static void *worker(void *arg) {
    *(int *)arg = 42;
    return NULL;
}

int main(void) {
    struct utsname u;
    pthread_t t;
    int answer = 0, ret;
    unsigned int chip = 0;
    mpp_version ver;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("dvrtest: hello from pid %d\n", (int)getpid());
    if (uname(&u) == 0)
        printf("uname: %s %s %s\n", u.sysname, u.release, u.machine);

    ret = pthread_create(&t, NULL, worker, &answer);
    if (ret == 0) pthread_join(t, NULL);
    printf("pthread: create=%d answer=%d\n", ret, answer);

    memset(&ver, 0, sizeof(ver));
    ret = GK_API_SYS_GetVersion(&ver);
    printf("GK_API_SYS_GetVersion: ret=%#x version='%s'\n", ret, ver.version);

    ret = GK_API_SYS_GetChipId(&chip);
    printf("GK_API_SYS_GetChipId: ret=%#x chip=%#x\n", ret, chip);

    printf("dvrtest: done\n");
    return 0;
}
