#pragma once

/* Minimal MPEG-TS writer: one H.264 video stream and an optional MPEG audio (MP3) stream.
 * Every frame is self-contained in the file, so a power cut only loses what had not been
 * written yet. Timestamps are capture times in microseconds from the MPP clock, so audio
 * and video stay in sync by construction. */
#include <stdio.h>

typedef struct {
    FILE *f;
    int audio;                           /* the PMT announces an audio stream */
    unsigned char ccPat, ccPmt, ccVid, ccAud;  /* continuity counters */
    unsigned long long basePtsUs;        /* first video frame's capture time */
    int haveBase;
} ts_mux;

/* Opens a new .ts file; audio: also announce an MPEG audio stream. Returns 0 on success. */
int ts_open(ts_mux *ts, const char *path, int audio);

/* Writes one encoded video frame (Annex-B NAL units). idr: the frame starts with
 * SPS/PPS/IDR (PAT and PMT are repeated before it). */
int ts_write_frame(ts_mux *ts, const unsigned char *data, unsigned int len,
                   unsigned long long ptsUs, int idr);

/* Writes one MP3 frame captured at ptsUs. Frames from before the first video frame of
 * the file are dropped. */
int ts_write_audio(ts_mux *ts, const unsigned char *data, unsigned int len,
                   unsigned long long ptsUs);

void ts_close(ts_mux *ts);
