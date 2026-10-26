/*
 * Minimal MPEG-TS (ISO/IEC 13818-1) writer: PAT + PMT + one H.264 elementary stream and
 * an optional MPEG-1 audio (MP3) stream.
 * PID 0x0000 PAT, 0x1000 PMT, 0x0100 video (also carries the PCR), 0x0101 audio.
 */
#include <string.h>

#include "tsmux.h"

#define TS_SIZE 188
#define PID_PMT 0x1000
#define PID_VIDEO 0x0100
#define PID_AUDIO 0x0101

static unsigned int crc32_mpeg(const unsigned char *p, unsigned int n) {
    unsigned int crc = 0xFFFFFFFF;
    while (n--) {
        crc ^= (unsigned int)*p++ << 24;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04C11DB7 : crc << 1;
    }
    return crc;
}

/* One TS packet holding a PSI section (PAT or PMT) */
static void write_section(ts_mux *ts, unsigned int pid, unsigned char *cc,
                          const unsigned char *sec, unsigned int len) {
    unsigned char pkt[TS_SIZE];
    memset(pkt, 0xFF, sizeof(pkt));
    pkt[0] = 0x47;
    pkt[1] = 0x40 | (pid >> 8);  /* payload_unit_start */
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10 | (*cc & 0x0F);  /* payload only */
    *cc = (*cc + 1) & 0x0F;
    pkt[4] = 0;  /* pointer_field */
    memcpy(pkt + 5, sec, len);
    fwrite(pkt, 1, TS_SIZE, ts->f);
}

static void write_tables(ts_mux *ts) {
    unsigned char s[40];
    unsigned int crc, n;

    /* PAT: program 1 -> PMT PID */
    n = 0;
    s[n++] = 0x00;                     /* table_id */
    s[n++] = 0xB0; s[n++] = 13;        /* section_syntax, section_length */
    s[n++] = 0x00; s[n++] = 0x01;      /* transport_stream_id */
    s[n++] = 0xC1;                     /* version 0, current */
    s[n++] = 0x00; s[n++] = 0x00;      /* section / last section */
    s[n++] = 0x00; s[n++] = 0x01;      /* program_number 1 */
    s[n++] = 0xE0 | (PID_PMT >> 8); s[n++] = PID_PMT & 0xFF;
    crc = crc32_mpeg(s, n);
    s[n++] = crc >> 24; s[n++] = crc >> 16; s[n++] = crc >> 8; s[n++] = crc;
    write_section(ts, 0x0000, &ts->ccPat, s, n);

    /* PMT: H.264 (type 0x1B) and optionally MPEG-1 audio (type 0x03), PCR on video */
    n = 0;
    s[n++] = 0x02;
    s[n++] = 0xB0; s[n++] = ts->audio ? 23 : 18;
    s[n++] = 0x00; s[n++] = 0x01;      /* program_number */
    s[n++] = 0xC1;
    s[n++] = 0x00; s[n++] = 0x00;
    s[n++] = 0xE0 | (PID_VIDEO >> 8); s[n++] = PID_VIDEO & 0xFF;  /* PCR_PID */
    s[n++] = 0xF0; s[n++] = 0x00;      /* program_info_length 0 */
    s[n++] = 0x1B;                     /* stream_type H.264 */
    s[n++] = 0xE0 | (PID_VIDEO >> 8); s[n++] = PID_VIDEO & 0xFF;
    s[n++] = 0xF0; s[n++] = 0x00;      /* ES_info_length 0 */
    if (ts->audio) {
        s[n++] = 0x03;                 /* stream_type MPEG-1 audio */
        s[n++] = 0xE0 | (PID_AUDIO >> 8); s[n++] = PID_AUDIO & 0xFF;
        s[n++] = 0xF0; s[n++] = 0x00;
    }
    crc = crc32_mpeg(s, n);
    s[n++] = crc >> 24; s[n++] = crc >> 16; s[n++] = crc >> 8; s[n++] = crc;
    write_section(ts, PID_PMT, &ts->ccPmt, s, n);
}

int ts_open(ts_mux *ts, const char *path, int audio) {
    memset(ts, 0, sizeof(*ts));
    ts->audio = audio;
    ts->f = fopen(path, "wb");
    if (!ts->f) return -1;
    setvbuf(ts->f, NULL, _IOFBF, 64 * 1024);
    write_tables(ts);
    return 0;
}

static void put_pts(unsigned char *p, unsigned long long pts) {
    p[0] = 0x21 | ((pts >> 29) & 0x0E);  /* '0010' + PTS[32..30] + marker */
    p[1] = (pts >> 22) & 0xFF;
    p[2] = 0x01 | ((pts >> 14) & 0xFE);
    p[3] = (pts >> 7) & 0xFF;
    p[4] = 0x01 | ((pts << 1) & 0xFE);
}

/* Splits one PES (header + optional prefix + payload) into TS packets on pid. The first
 * packet carries the PCR when pcr is non-NULL. */
static void write_pes(ts_mux *ts, unsigned int pid, unsigned char *cc,
                      const unsigned char *hdr0, unsigned int hdrLen,
                      const unsigned char *pre, unsigned int preLen,
                      const unsigned char *data, unsigned int len,
                      const unsigned long long *pcr, int randomAccess) {
    unsigned int total = hdrLen + preLen + len, done = 0, first = 1;
    while (done < total) {
        unsigned char pkt[TS_SIZE];
        unsigned int hdr = 4, space, take, pos;

        pkt[0] = 0x47;
        pkt[1] = (first ? 0x40 : 0x00) | (pid >> 8);
        pkt[2] = pid & 0xFF;

        if (first && pcr) {
            /* adaptation field with PCR (and random access flag on IDR frames) */
            unsigned long long base = *pcr;
            pkt[4] = 7;
            pkt[5] = 0x10 | (randomAccess ? 0x40 : 0x00);
            pkt[6] = (base >> 25) & 0xFF;
            pkt[7] = (base >> 17) & 0xFF;
            pkt[8] = (base >> 9) & 0xFF;
            pkt[9] = (base >> 1) & 0xFF;
            pkt[10] = ((base & 1) << 7) | 0x7E;
            pkt[11] = 0x00;
            hdr = 12;
        }
        space = TS_SIZE - hdr;
        take = total - done < space ? total - done : space;

        if (take < space) {
            /* last packet: pad with adaptation-field stuffing */
            unsigned int pad = space - take;
            if (hdr == 4) {
                pkt[4] = pad - 1;
                if (pad > 1) {
                    pkt[5] = 0x00;
                    memset(pkt + 6, 0xFF, pad - 2);
                }
            } else {
                pkt[4] += pad;
                memset(pkt + hdr, 0xFF, pad);
            }
            hdr += pad;
        }
        pkt[3] = (hdr > 4 ? 0x30 : 0x10) | (*cc & 0x0F);
        *cc = (*cc + 1) & 0x0F;

        /* copy the next 'take' bytes of PES header + prefix + payload */
        for (pos = 0; pos < take; ) {
            unsigned int at = done + pos, n;
            const unsigned char *src;
            if (at < hdrLen) { src = hdr0 + at; n = hdrLen - at; }
            else if (at < hdrLen + preLen) { src = pre + (at - hdrLen); n = hdrLen + preLen - at; }
            else { src = data + (at - hdrLen - preLen); n = total - at; }
            if (n > take - pos) n = take - pos;
            memcpy(pkt + hdr + pos, src, n);
            pos += n;
        }
        fwrite(pkt, 1, TS_SIZE, ts->f);
        done += take;
        first = 0;
    }
}

/* 90 kHz clock, starting at 1 s so the PCR (100 ms earlier) never goes negative */
static unsigned long long to_90k(ts_mux *ts, unsigned long long ptsUs) {
    return (ptsUs - ts->basePtsUs) * 9 / 100 + 90000;
}

int ts_write_frame(ts_mux *ts, const unsigned char *data, unsigned int len,
                   unsigned long long ptsUs, int idr) {
    static const unsigned char aud[6] = { 0, 0, 0, 1, 0x09, 0xF0 };  /* access unit delimiter */
    unsigned char pes[14];
    unsigned long long pts, pcr;
    unsigned int n = 0;

    if (!ts->f) return -1;
    if (!ts->haveBase) { ts->basePtsUs = ptsUs; ts->haveBase = 1; }
    if (idr) write_tables(ts);
    pts = to_90k(ts, ptsUs);
    pcr = pts - 9000;

    pes[n++] = 0x00; pes[n++] = 0x00; pes[n++] = 0x01;
    pes[n++] = 0xE0;                  /* video stream 0 */
    pes[n++] = 0x00; pes[n++] = 0x00; /* PES_packet_length 0: unbounded */
    pes[n++] = 0x80;                  /* marker bits */
    pes[n++] = 0x80;                  /* PTS only */
    pes[n++] = 5;                     /* header data length */
    put_pts(pes + n, pts); n += 5;
    write_pes(ts, PID_VIDEO, &ts->ccVid, pes, n, aud, sizeof(aud), data, len, &pcr, idr);
    return 0;
}

int ts_write_audio(ts_mux *ts, const unsigned char *data, unsigned int len,
                   unsigned long long ptsUs) {
    unsigned char pes[14];
    unsigned int n = 0, pesLen = 8 + len;

    if (!ts->f || !ts->audio || !ts->haveBase || ptsUs < ts->basePtsUs) return -1;
    pes[n++] = 0x00; pes[n++] = 0x00; pes[n++] = 0x01;
    pes[n++] = 0xC0;                  /* audio stream 0 */
    pes[n++] = pesLen >> 8; pes[n++] = pesLen & 0xFF;
    pes[n++] = 0x80;
    pes[n++] = 0x80;                  /* PTS only */
    pes[n++] = 5;
    put_pts(pes + n, to_90k(ts, ptsUs)); n += 5;
    write_pes(ts, PID_AUDIO, &ts->ccAud, pes, n, NULL, 0, data, len, NULL, 0);
    return 0;
}

void ts_close(ts_mux *ts) {
    if (ts->f) fclose(ts->f);
    ts->f = NULL;
}
