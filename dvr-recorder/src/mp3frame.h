#pragma once

/* Re-frames an MP3 byte stream (as shine returns it, not aligned to frames) into whole
 * frames, using the length encoded in each frame header. */

typedef struct {
    unsigned char buf[8192];
    unsigned int len;
} mp3_framer;

void mp3_framer_reset(mp3_framer *m);

/* Appends encoder output. Returns 0, or -1 if the buffer overflowed (data dropped). */
int mp3_push(mp3_framer *m, const unsigned char *data, unsigned int n);

/* Copies the next complete frame to out (up to max bytes) and returns its length, or 0
 * if no complete frame is buffered yet. */
unsigned int mp3_pop(mp3_framer *m, unsigned char *out, unsigned int max);
