/* MP3 (MPEG-1/2 Layer III) frame re-framing, see mp3frame.h */
#include <string.h>

#include "mp3frame.h"

static const unsigned short kbpsV1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
static const unsigned short kbpsV2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
static const unsigned int rateV1[4] = { 44100, 48000, 32000, 0 };

/* Frame length from a 4-byte header, or 0 if it is not a valid Layer III header. */
static unsigned int frame_len(const unsigned char *h) {
    unsigned int ver, br, sr, pad;
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    if (((h[1] >> 1) & 3) != 1) return 0;  /* Layer III */
    ver = (h[1] >> 3) & 3;                 /* 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5 */
    if (ver == 1 || ((h[2] >> 2) & 3) == 3) return 0;
    pad = (h[2] >> 1) & 1;
    sr = rateV1[(h[2] >> 2) & 3] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    br = (ver == 3 ? kbpsV1 : kbpsV2)[h[2] >> 4];
    if (!br || !sr) return 0;
    return (ver == 3 ? 144000 : 72000) * br / sr + pad;
}

void mp3_framer_reset(mp3_framer *m) {
    m->len = 0;
}

int mp3_push(mp3_framer *m, const unsigned char *data, unsigned int n) {
    if (m->len + n > sizeof(m->buf)) {
        m->len = 0;  /* lost sync; start again from the next data */
        return -1;
    }
    memcpy(m->buf + m->len, data, n);
    m->len += n;
    return 0;
}

/* Same stream parameters (version, layer, bitrate, sample rate, mode), padding bit aside */
static int same_stream(const unsigned char *a, const unsigned char *b) {
    return a[0] == 0xFF && a[1] == b[1] && (a[2] & 0xFD) == (b[2] & 0xFD) &&
           (a[3] & 0xC0) == (b[3] & 0xC0);
}

unsigned int mp3_pop(mp3_framer *m, unsigned char *out, unsigned int max) {
    static const int adjust[3] = { 0, -1, 1 };
    unsigned int skip = 0, flen, i;
    /* find a frame header */
    while (skip + 4 <= m->len && !frame_len(m->buf + skip)) skip++;
    if (skip) {
        memmove(m->buf, m->buf + skip, m->len - skip);
        m->len -= skip;
    }
    if (m->len < 4) return 0;
    flen = frame_len(m->buf);
    /* The padding bit is not always reliable (shine sets it at 32 kHz / 96 kbit/s where
     * frames are exactly 432 bytes), so confirm the length by finding the next header. */
    if (m->len < flen + 1 + 4) return 0;  /* wait until the next header can be checked */
    for (i = 0; i < 3; i++) {
        unsigned int d = flen + adjust[i];
        if (same_stream(m->buf + d, m->buf)) { flen = d; break; }
    }
    if (flen > max) return 0;
    memcpy(out, m->buf, flen);
    memmove(m->buf, m->buf + flen, m->len - flen);
    m->len -= flen;
    return flen;
}
