/*
 * Host test for src/tsmux.c: splits a raw H.264 (Annex-B) file into access units and
 * muxes them at a fixed frame rate like the board's writer thread, plus (optionally) a
 * 1 kHz test tone encoded to MP3 with shine at 32 kHz, interleaved by timestamp.
 *   tstest in.h264 out.ts [fps] [tone]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mp3frame.h"
#include "../src/tsmux.h"
#include "../third_party/shine/layer3.h"

static long next_start(const unsigned char *d, long n, long from) {
    for (long i = from; i + 3 < n; i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) return i > 0 && d[i - 1] == 0 ? i - 1 : i;
    return n;
}

static int nal_type(const unsigned char *d, long at) {
    while (d[at] == 0) at++;
    return d[at + 1] & 0x1F;  /* skip the 0x01 of the start code */
}

static shine_t enc;
static int spp;               /* samples per MP3 frame */
static unsigned long long audioSamples, framesOut, encodes, bytesIn;
static mp3_framer framer;

/* writes all audio up to time tUs, one whole MP3 frame per PES */
static void audio_until(ts_mux *ts, unsigned long long tUs) {
    static int16_t pcm[2304];
    static unsigned char frame[2048];
    while (enc && audioSamples * 1000000ULL / 32000 <= tUs) {
        int n = 0;
        unsigned int len;
        unsigned char *mp3;
        for (int i = 0; i < spp; i++)
            pcm[i] = (int16_t)(8000 * sin(2 * M_PI * 1000 * (audioSamples + i) / 32000.0));
        mp3 = shine_encode_buffer_interleaved(enc, pcm, &n);
        encodes++;
        bytesIn += n > 0 ? n : 0;
        {   /* raw encoder output, for inspection */
            static FILE *raw;
            if (!raw) raw = fopen("build/host/tone_raw.mp3", "wb");
            if (raw && n > 0) fwrite(mp3, 1, n, raw);
        }
        if (n > 0) mp3_push(&framer, mp3, n);
        while ((len = mp3_pop(&framer, frame, sizeof(frame)))) {
            ts_write_audio(ts, frame, len, framesOut * spp * 1000000ULL / 32000);
            framesOut++;
        }
        audioSamples += spp;
    }
}

int main(int argc, char **argv) {
    FILE *f;
    unsigned char *d;
    long n, pos, au = 0, frames = 0, idrs = 0;
    int fps = argc > 3 ? atoi(argv[3]) : 50, tone = argc > 4, idr = 0, sawSlice = 0;
    ts_mux ts;

    if (argc < 3) { fprintf(stderr, "usage: tstest in.h264 out.ts [fps] [tone]\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    if (tone) {
        shine_config_t c;
        memset(&c, 0, sizeof(c));
        c.wave.channels = PCM_MONO; c.wave.samplerate = 32000;
        c.mpeg.mode = MONO; c.mpeg.bitr = 96; c.mpeg.emph = NONE; c.mpeg.original = 1;
        enc = shine_initialise(&c);
        spp = shine_samples_per_pass(enc);
    }
    if (ts_open(&ts, argv[2], tone)) return 1;

    pos = next_start(d, n, 0);
    au = pos;
    while (pos < n) {
        long nxt = next_start(d, n, pos + 3);
        int t = nal_type(d, pos);
        int slice = (t == 1 || t == 5);
        /* a new access unit starts at the NAL after a slice */
        if (sawSlice) {
            unsigned long long pts = (unsigned long long)frames * 1000000ULL / fps;
            ts_write_frame(&ts, d + au, pos - au, pts, idr);
            audio_until(&ts, pts);
            frames++; idrs += idr;
            au = pos; idr = 0; sawSlice = 0;
        }
        if (t == 7 || t == 5) idr = 1;
        if (slice) sawSlice = 1;
        pos = nxt;
    }
    if (sawSlice) {
        ts_write_frame(&ts, d + au, n - au, (unsigned long long)frames * 1000000ULL / fps, idr);
        frames++; idrs += idr;
    }
    ts_close(&ts);
    printf("muxed %ld frames (%ld IDR) at %d fps%s into %s\n", frames, idrs, fps,
        tone ? " + 1 kHz MP3 tone" : "", argv[2]);
    if (tone)
        printf("audio: %d samples/pass, %llu encodes, %llu bytes from shine, %llu MP3 frames out\n",
            spp, encodes, bytesIn, framesOut);
    return 0;
}
