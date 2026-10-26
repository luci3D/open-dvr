/*
 * Host test for the shine MP3 encoder at different input levels (32 kHz mono, 96 kbit/s).
 * Writes one raw .mp3 per signal into build/host/; check them with scripts/verify_levels.py.
 *   shinetest
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../third_party/shine/layer3.h"

static void encode(const char *name, double amp, int noise, int clip) {
    char path[128];
    shine_config_t c;
    shine_t enc;
    int spp, n;
    int16_t pcm[1152];
    unsigned int seed = 1;
    FILE *f;

    memset(&c, 0, sizeof(c));
    c.wave.channels = PCM_MONO; c.wave.samplerate = 32000;
    c.mpeg.mode = MONO; c.mpeg.bitr = 96; c.mpeg.emph = NONE; c.mpeg.original = 1;
    enc = shine_initialise(&c);
    spp = shine_samples_per_pass(enc);
    snprintf(path, sizeof(path), "build/host/level_%s.mp3", name);
    f = fopen(path, "wb");
    for (long k = 0; k < 32000 * 3 / spp; k++) {
        for (int i = 0; i < spp; i++) {
            double t = (k * spp + i) / 32000.0, v;
            if (noise) {
                seed = seed * 1103515245 + 12345;
                v = amp * (((seed >> 8) & 0xFFFF) / 32768.0 - 1.0);
            } else {
                v = amp * sin(2 * M_PI * 440 * t);
            }
            if (clip && v > 32767) v = 32767;
            if (clip && v < -32768) v = -32768;
            pcm[i] = (int16_t)v;
        }
        unsigned char *mp3 = shine_encode_buffer_interleaved(enc, pcm, &n);
        if (n > 0) fwrite(mp3, 1, n, f);
    }
    fclose(f);
    shine_close(enc);
}

int main(void) {
    encode("sine_-24dB", 2063, 0, 0);
    encode("sine_-12dB", 8231, 0, 0);
    encode("sine_-6dB", 16422, 0, 0);
    encode("sine_-1dB", 29204, 0, 0);
    encode("sine_clipped", 60000, 0, 1);
    encode("noise_-12dB", 8231, 1, 0);
    encode("noise_fullscale", 32767, 1, 0);
    encode("noise_clipped", 80000, 1, 1);
    printf("wrote build/host/level_*.mp3\n");
    return 0;
}
