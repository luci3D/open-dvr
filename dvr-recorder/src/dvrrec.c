/*
 * dvrrec: open DVR recorder for the GK7202V300 + TP2850 board.
 *
 * The TP2850 sends one analog field per MIPI frame (288 lines PAL / 240 NTSC, 50/60 per
 * second) as 8-bit UYVY. This chip's VI cannot split 8-bit serial YUV into luma and
 * chroma, so the VI captures each line as raw bytes (1920 per line) and we split them
 * here with NEON into a YVU 4:2:2 semi-planar field, which goes to VPSS (scaled to full
 * height) and on to the H.264 encoder.
 *
 *   TP2850 -> MIPI rx -> VI (bytes) -> [capture thread: UYVY -> YVU422SP] -> VPSS -> VENC
 *                                                        [writer thread: VENC -> file]
 *
 * Output: MPEG-TS clips DCIM/DVR_0001.ts, DVR_0002.ts, ... (each starts on an IDR frame,
 * flushed to the card about once a second, so a power cut loses at most ~1 s), 960x576
 * flagged 4:3 (SAR 4:5).
 *
 * Settings come from the stock /home/config.txt (MaxBitRate in Mbit/s, FileIntervalTime
 * in minutes, VideoFormat PAL/NTSC/AUTO); command-line options override them:
 *   dvrrec [-s pal|ntsc|auto] [-t seconds] [-c clip_minutes] [-b kbps] [-o file.ts]
 *          [-d dir] [-W out_w] [-H out_h] [-w mipi_width] [-k]
 *     -t: stop after this many seconds (default 0 = run until stopped)
 *     -o: write one file instead of numbered clips
 *     -w: MIPI rx line width setting, 1920 (default, as the stock firmware)
 *     -k: skip the sensor clock/reset ioctls
 *
 * Built on OpenIPC openhisilicon headers (GPL-3.0, third_party/openhisilicon) and the
 * Goke MPP library from OpenIPC firmware.
 */
#include <arm_neon.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "audio_acodec.h"
#include "gk_api_audio.h"
#include "gk_api_sys.h"
#include "gk_api_vb.h"
#include "gk_api_venc.h"
#include "gk_api_vi.h"
#include "gk_api_vpss.h"
#include "mipi.h"

#include "compat.h"
#include "layer3.h"  /* shine MP3 encoder (third_party/shine) */
#include "mp3frame.h"
#include "tp2850.h"
#include "tsmux.h"

#define VI_DEV 0
#define VI_PIPE_ID 0
#define VI_CHN_ID 0
#define VPSS_GRP_ID 0
#define VPSS_CHN_ID 0
#define VENC_CHN_ID 0

static int failed;
#define STD_FILE "/home/dvr/std"  /* last detected standard, for a faster start */

static volatile int running = 1;
static int noVideo;  /* stopped by the no-video watchdog: exit code 3 */

#define CALL(expr) ({ int _r = (expr); \
    printf("  %-58s -> %#x\n", #expr, _r); if (_r) failed = 1; _r; })

/* ---- configuration ------------------------------------------------------- */
static struct {
    enum tp_std std;
    int autoStd;
    unsigned int fieldW, fieldH, fps;   /* field size in pixels, fields per second */
    unsigned int outW, outH, kbps, secs, mipiW, clipSec;
    int sensorCtl;
    int audio;                          /* record the board's microphone (config "Audio") */
    unsigned int audioKbps;
    int micBoost;                       /* codec boost stage (config "MicBoost"); off by default:
                                         * with it on, prop noise clipped the input in flight */
    int micGain;                        /* codec analog mic gain step, -1 = leave as is */
    int audioGainDb;                    /* extra software gain (config "AudioGain", dB) */
    const char *path;                   /* single output file, or NULL for numbered clips */
    const char *dir;                    /* where numbered clips go */
} cfg;

/* ---- statistics (written by the threads, printed by main) ---------------- */
static struct {
    volatile unsigned int captured, sent, sendFail, getFail, blkFail, encoded, files, idrs;
    volatile unsigned int audioFrames, mp3Frames, audioDrops;
    volatile unsigned long long bytes, convUs, firstPts, lastPts, firstAudioTs, audioUs;
    volatile long long audioPeak;
} st;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

/* Reads MaxBitRate / FileIntervalTime / VideoFormat from the stock config file. */
static void read_config(const char *path) {
    char line[128], key[64], val[64];
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " %63[^= ] = %63s", key, val) != 2) continue;
        if (!strcmp(key, "Audio")) cfg.audio = atoi(val) != 0;
        else if (!strcmp(key, "MicBoost")) cfg.micBoost = atoi(val) != 0;
        else if (!strcmp(key, "MicGain")) cfg.micGain = atoi(val);
        else if (!strcmp(key, "AudioGain")) cfg.audioGainDb = atoi(val);
        else if (!strcmp(key, "MaxBitRate") && atoi(val) > 0) cfg.kbps = atoi(val) * 1000;
        else if (!strcmp(key, "FileIntervalTime") && atoi(val) > 0) cfg.clipSec = atoi(val) * 60;
        else if (!strcmp(key, "VideoFormat")) {
            if (!strcmp(val, "NTSC")) { cfg.std = TP_NTSC; cfg.autoStd = 0; }
            else if (!strcmp(val, "PAL")) { cfg.std = TP_PAL; cfg.autoStd = 0; }
            else cfg.autoStd = 1;
        }
    }
    fclose(f);
}

/* ---- cached mappings of MMZ blocks (VB blocks are reused, so map once) --- */
static struct { GK_U64 phys; void *virt; GK_U32 size; } maps[32];

static void *map_cached(GK_U64 phys, GK_U32 size) {
    int i;
    for (i = 0; i < 32 && maps[i].virt; i++)
        if (maps[i].phys == phys && maps[i].size >= size) return maps[i].virt;
    if (i == 32) return NULL;
    maps[i].virt = GK_API_SYS_MmapCache(phys, size);
    if (!maps[i].virt) return NULL;
    maps[i].phys = phys;
    maps[i].size = size;
    return maps[i].virt;
}

static void unmap_all(void) {
    for (int i = 0; i < 32 && maps[i].virt; i++) {
        GK_API_SYS_Munmap(maps[i].virt, maps[i].size);
        maps[i].virt = NULL;
    }
}

/* ---- UYVY bytes -> YVU 4:2:2 semi-planar (luma plane + V/U interleaved plane) */
static void uyvy_to_yvu422sp(const uint8_t *src, unsigned int srcStride, uint8_t *dstY,
                             uint8_t *dstC, unsigned int dstStride, unsigned int width,
                             unsigned int height) {
    for (unsigned int r = 0; r < height; r++) {
        const uint8_t *s = src + r * srcStride;
        uint8_t *y = dstY + r * dstStride, *c = dstC + r * dstStride;
        unsigned int x = 0;
        for (; x + 32 <= width; x += 32) {  /* 32 pixels = 64 source bytes */
            uint8x16x4_t v = vld4q_u8(s + 2 * x);  /* [0]=U [1]=Y even [2]=V [3]=Y odd */
            uint8x16x2_t yy = { { v.val[1], v.val[3] } };
            uint8x16x2_t vu = { { v.val[2], v.val[0] } };
            vst2q_u8(y + x, yy);
            vst2q_u8(c + x, vu);
        }
        for (; x + 2 <= width; x += 2) {
            const uint8_t *p = s + 2 * x;
            y[x] = p[1]; y[x + 1] = p[3];
            c[x] = p[2]; c[x + 1] = p[0];
        }
    }
}

/* ---- capture thread: VI raw field -> converted field -> VPSS ------------- */
static VB_POOL fieldPool = VB_INVALID_POOLID;

static void *capture_thread(void *arg) {
    const GK_U32 lumaSize = cfg.fieldW * cfg.fieldH, fieldSize = lumaSize * 2;
    (void)arg;
    while (running) {
        VIDEO_FRAME_INFO_S src, dst;
        VB_BLK blk;
        GK_U64 dstPhys, srcPhys;
        GK_U32 srcSize;
        uint8_t *srcVirt, *dstVirt;
        unsigned long long t0;
        int r;

        if ((r = GK_API_VI_GetChnFrame(VI_PIPE_ID, VI_CHN_ID, &src, 1000))) {
            if (++st.getFail <= 3) printf("  VI_GetChnFrame -> %#x\n", r);
            continue;
        }
        st.captured++;

        blk = GK_API_VB_GetBlock(fieldPool, fieldSize, NULL);
        if (blk == VB_INVALID_HANDLE) {
            st.blkFail++;
            GK_API_VI_ReleaseChnFrame(VI_PIPE_ID, VI_CHN_ID, &src);
            continue;
        }
        dstPhys = GK_API_VB_Handle2PhysAddr(blk);
        srcPhys = src.stVFrame.u64PhyAddr[0];
        srcSize = src.stVFrame.u32Stride[0] * src.stVFrame.u32Height;
        srcVirt = map_cached(srcPhys, srcSize);
        dstVirt = map_cached(dstPhys, fieldSize);
        if (!srcVirt || !dstVirt) {
            if (++st.blkFail <= 3) printf("  mmap failed (src %p dst %p)\n", (void *)srcVirt, (void *)dstVirt);
            GK_API_VB_ReleaseBlock(blk);
            GK_API_VI_ReleaseChnFrame(VI_PIPE_ID, VI_CHN_ID, &src);
            continue;
        }

        t0 = mono_us();
        GK_API_SYS_MflushCache(srcPhys, srcVirt, srcSize);  /* drop stale cache lines */
        uyvy_to_yvu422sp(srcVirt, src.stVFrame.u32Stride[0], dstVirt, dstVirt + lumaSize,
                         cfg.fieldW, cfg.fieldW, cfg.fieldH);
        GK_API_SYS_MflushCache(dstPhys, dstVirt, fieldSize);  /* write back for VPSS */
        st.convUs += mono_us() - t0;

        memset(&dst, 0, sizeof(dst));
        dst.u32PoolId = fieldPool;
        dst.enModId = MOD_ID_USER;
        dst.stVFrame.u32Width = cfg.fieldW;
        dst.stVFrame.u32Height = cfg.fieldH;
        dst.stVFrame.enField = VIDEO_FIELD_FRAME;
        dst.stVFrame.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_422;
        dst.stVFrame.enVideoFormat = VIDEO_FORMAT_LINEAR;
        dst.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
        dst.stVFrame.enDynamicRange = DYNAMIC_RANGE_SDR8;
        dst.stVFrame.enColorGamut = COLOR_GAMUT_BT601;
        dst.stVFrame.u32Stride[0] = cfg.fieldW;
        dst.stVFrame.u32Stride[1] = cfg.fieldW;
        dst.stVFrame.u64PhyAddr[0] = dstPhys;
        dst.stVFrame.u64PhyAddr[1] = dstPhys + lumaSize;
        dst.stVFrame.u64VirAddr[0] = (GK_U64)(unsigned long)dstVirt;
        dst.stVFrame.u64VirAddr[1] = (GK_U64)(unsigned long)(dstVirt + lumaSize);
        dst.stVFrame.u32TimeRef = src.stVFrame.u32TimeRef;
        dst.stVFrame.u64PTS = src.stVFrame.u64PTS;
        if (!st.firstPts) st.firstPts = src.stVFrame.u64PTS;
        st.lastPts = src.stVFrame.u64PTS;

        GK_API_VI_ReleaseChnFrame(VI_PIPE_ID, VI_CHN_ID, &src);
        if ((r = GK_API_VPSS_SendFrame(VPSS_GRP_ID, 0, &dst, 1000))) {
            if (++st.sendFail <= 3) printf("  VPSS_SendFrame -> %#x\n", r);
        } else {
            st.sent++;
        }
        GK_API_VB_ReleaseBlock(blk);  /* VPSS keeps its own reference while it works */
    }
    return NULL;
}

/* ---- audio: microphone -> MP3 frames with capture timestamps ------------- */
#define AUDIO_DEV 0
#define AUDIO_CHN 0
#define AUDIO_RATE 32000
#define AQ_LEN 64

static int audioOn;  /* audio input started */

/* MP3 frames waiting for the writer thread */
static struct {
    pthread_mutex_t mtx;
    unsigned int head, tail;
    struct { unsigned short len; unsigned long long pts; unsigned char data[1442]; } q[AQ_LEN];
} aq = { .mtx = PTHREAD_MUTEX_INITIALIZER };

static void aq_put(const unsigned char *data, unsigned int len, unsigned long long pts) {
    pthread_mutex_lock(&aq.mtx);
    if (aq.head - aq.tail >= AQ_LEN || len > sizeof(aq.q[0].data)) {
        st.audioDrops++;
    } else {
        unsigned int i = aq.head % AQ_LEN;
        memcpy(aq.q[i].data, data, len);
        aq.q[i].len = len;
        aq.q[i].pts = pts;
        aq.head++;
    }
    pthread_mutex_unlock(&aq.mtx);
}

/* Writes all queued MP3 frames into the open clip (frames from before it are dropped) */
static void aq_drain(ts_mux *ts) {
    pthread_mutex_lock(&aq.mtx);
    while (aq.tail != aq.head) {
        unsigned int i = aq.tail % AQ_LEN;
        if (ts->f) ts_write_audio(ts, aq.q[i].data, aq.q[i].len, aq.q[i].pts);
        aq.tail++;
    }
    pthread_mutex_unlock(&aq.mtx);
}

static void acodec_setup(void);

static int audio_start(void) {
    AIO_ATTR_S attr;
    memset(&attr, 0, sizeof(attr));
    attr.enSamplerate = AUDIO_SAMPLE_RATE_32000;
    attr.enBitwidth = AUDIO_BIT_WIDTH_16;
    attr.enWorkmode = AIO_MODE_I2S_MASTER;
    attr.enSoundmode = AUDIO_SOUND_MODE_MONO;
    attr.u32FrmNum = 30;
    attr.u32PtNumPerFrm = 320;  /* 10 ms blocks */
    attr.u32ChnCnt = 1;
    attr.enI2sType = AIO_I2STYPE_INNERCODEC;  /* the board's built-in codec and microphone input */
    if (CALL(GK_API_AI_SetPubAttr(AUDIO_DEV, &attr))) return -1;
    if (CALL(GK_API_AI_Enable(AUDIO_DEV))) return -1;
    if (CALL(GK_API_AI_EnableChn(AUDIO_DEV, AUDIO_CHN))) { GK_API_AI_Disable(AUDIO_DEV); return -1; }
    acodec_setup();
    return 0;
}

static void print_acodec_state(const char *when) {
    char line[160];
    FILE *f = fopen("/proc/umap/acodec", "r");
    printf("  codec state %s:\n", when);
    if (!f) return;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, "Gain") || (line[0] == ' ' && strchr(line, '.'))) printf("    %s", line);
    fclose(f);
}

/* Internal codec input settings: boost stage and analog gain (/dev/acodec) */
static void acodec_setup(void) {
    unsigned int v;
    int fd = open("/dev/acodec", O_RDWR);
    print_acodec_state("before");
    if (fd < 0) { printf("  cannot open /dev/acodec\n"); return; }
    v = cfg.micBoost;
    printf("  ACODEC_ENABLE_BOOSTL %u -> %d\n", v, ioctl(fd, ACODEC_ENABLE_BOOSTL, &v));
    if (cfg.micGain >= 0) {
        v = cfg.micGain;
        printf("  ACODEC_SET_GAIN_MICL %u -> %d\n", v, ioctl(fd, ACODEC_SET_GAIN_MICL, &v));
    }
    v = 0;
    if (!ioctl(fd, ACODEC_GET_GAIN_MICL, &v)) printf("  mic gain step now %u\n", v);
    close(fd);
    print_acodec_state("after");
}

static void audio_stop(void) {
    GK_API_AI_DisableChn(AUDIO_DEV, AUDIO_CHN);
    GK_API_AI_Disable(AUDIO_DEV);
}

static void *audio_thread(void *arg) {
    static int16_t pcm[1152];
    static unsigned char frame[1442];
    unsigned long long blockTs[16];   /* capture time of each block given to the encoder */
    unsigned int blocksIn = 0, blocksOut = 0, pos = 0;
    /* ~80 Hz first-order low-cut (hum, rumble, wind) and software gain, fixed point */
    const int hpA = 32259;  /* Q15, 1 / (1 + 2*pi*80/32000) */
    int hpX = 0, hpY = 0, gainQ8 = 256;
    shine_config_t sc;
    shine_t enc;
    mp3_framer framer;
    int spp;
    (void)arg;

    memset(&sc, 0, sizeof(sc));
    sc.wave.channels = PCM_MONO;
    sc.wave.samplerate = AUDIO_RATE;
    sc.mpeg.mode = MONO;
    sc.mpeg.bitr = cfg.audioKbps;
    sc.mpeg.emph = NONE;
    sc.mpeg.original = 1;
    if (shine_check_config(AUDIO_RATE, cfg.audioKbps) < 0 || !(enc = shine_initialise(&sc))) {
        printf("  audio: MP3 encoder setup failed\n");
        return NULL;
    }
    spp = shine_samples_per_pass(enc);
    mp3_framer_reset(&framer);
    for (int d = 0; d < cfg.audioGainDb && d < 30; d++) gainQ8 = gainQ8 * 1122 / 1000;  /* +1 dB */
    printf("  audio: MP3 %u kbit/s, low-cut 80 Hz, software gain %+d dB (x%.2f)\n",
        cfg.audioKbps, cfg.audioGainDb, gainQ8 / 256.0);

    while (running) {
        AUDIO_FRAME_S fr;
        AEC_FRAME_S aec;
        const int16_t *s;
        unsigned int n, i;
        unsigned long long t0;

        memset(&aec, 0, sizeof(aec));
        if (GK_API_AI_GetFrame(AUDIO_DEV, AUDIO_CHN, &fr, &aec, 500)) continue;
        s = (const int16_t *)fr.u64VirAddr[0];
        n = fr.u32Len / 2;
        if (!st.firstAudioTs) st.firstAudioTs = fr.u64TimeStamp;
        st.audioFrames++;
        for (i = 0; i < n; i++) {
            int y;
            long long v;
            hpY = (int)(((long long)hpA * (hpY + s[i] - hpX) + 16384) >> 15);  /* rounded */
            hpX = s[i];
            y = (hpY * gainQ8) >> 8;
            if (y > 32767) y = 32767;
            if (y < -32768) y = -32768;
            v = y < 0 ? -y : y;
            if (v > st.audioPeak) st.audioPeak = v;
            if (pos == 0)  /* first sample of a new encoder block: remember its time */
                blockTs[blocksIn % 16] = fr.u64TimeStamp + (unsigned long long)i * 1000000ULL / AUDIO_RATE;
            pcm[pos++] = (int16_t)y;
            if ((int)pos == spp) {
                int written = 0;
                unsigned char *mp3;
                unsigned int len;
                t0 = mono_us();
                mp3 = shine_encode_buffer_interleaved(enc, pcm, &written);
                st.audioUs += mono_us() - t0;
                blocksIn++;
                pos = 0;
                if (written > 0) mp3_push(&framer, mp3, written);
                /* constant bitrate: MP3 frame k encodes block k, so it carries that
                 * block's capture time (the framer holds one frame back) */
                while ((len = mp3_pop(&framer, frame, sizeof(frame)))) {
                    aq_put(frame, len, blockTs[blocksOut % 16]);
                    blocksOut++;
                    st.mp3Frames++;
                }
            }
        }
        GK_API_AI_ReleaseFrame(AUDIO_DEV, AUDIO_CHN, &fr, &aec);
    }
    shine_close(enc);
    return NULL;
}

/* ---- writer thread: encoded stream -> MPEG-TS clips ---------------------- */

/* Next free clip number in cfg.dir (DVR_0001.ts, DVR_0002.ts, ...) */
static unsigned int next_clip_number(void) {
    unsigned int best = 0, n;
    struct dirent *e;
    DIR *d = opendir(cfg.dir);
    if (!d) return 1;
    while ((e = readdir(d)))
        if (sscanf(e->d_name, "DVR_%u.ts", &n) == 1 && n > best) best = n;
    closedir(d);
    return best + 1;
}

static int open_clip(ts_mux *ts, unsigned int *clipNo) {
    char path[256];
    if (cfg.path) snprintf(path, sizeof(path), "%s", cfg.path);
    else snprintf(path, sizeof(path), "%s/DVR_%04u.ts", cfg.dir, (*clipNo)++);
    if (ts_open(ts, path, audioOn)) {
        printf("  cannot create %s\n", path);
        return -1;
    }
    st.files++;
    printf("  recording to %s\n", path);
    return 0;
}

static void sync_clip(ts_mux *ts) {
    fflush(ts->f);
    fdatasync(fileno(ts->f));
}

static void *writer_thread(void *arg) {
    static unsigned char frame[2 * 1024 * 1024];  /* one encoded frame */
    int fd = GK_API_VENC_GetFd(VENC_CHN_ID);
    unsigned int clipNo = next_clip_number();
    unsigned long long clipStart = 0, lastSync = 0;
    int idrRequested = 0;
    ts_mux ts;
    (void)arg;

    memset(&ts, 0, sizeof(ts));
    printf("  writer: venc fd %d, %s\n", fd, cfg.path ? cfg.path : "numbered clips");
    while (running && fd >= 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        VENC_CHN_STATUS_S stat;
        VENC_STREAM_S strm;
        VENC_PACK_S packs[16];
        unsigned int len = 0;
        unsigned long long pts = 0, now;
        int idr = 0;

        if (poll(&pfd, 1, 1000) <= 0) { aq_drain(&ts); continue; }
        if (GK_API_VENC_QueryStatus(VENC_CHN_ID, &stat) || !stat.u32CurPacks) continue;
        memset(&strm, 0, sizeof(strm));
        strm.pstPack = packs;
        strm.u32PackCount = stat.u32CurPacks > 16 ? 16 : stat.u32CurPacks;
        if (GK_API_VENC_GetStream(VENC_CHN_ID, &strm, 100)) continue;
        for (GK_U32 i = 0; i < strm.u32PackCount; i++) {
            VENC_PACK_S *p = &strm.pstPack[i];
            GK_U32 n = p->u32Len - p->u32Offset;
            if (i == 0) pts = p->u64PTS;
            if (p->DataType.enH264EType == H264E_NALU_SPS ||
                p->DataType.enH264EType == H264E_NALU_IDRSLICE) idr = 1;
            if (len + n <= sizeof(frame)) {
                memcpy(frame + len, p->pu8Addr + p->u32Offset, n);
                len += n;
            }
        }
        GK_API_VENC_ReleaseStream(VENC_CHN_ID, &strm);
        st.encoded++;
        st.idrs += idr;

        now = mono_us();
        /* new clip: on the first IDR, and on the first IDR after the clip time is up */
        if (idr && (!ts.f || (idrRequested && !cfg.path))) {
            if (ts.f) { sync_clip(&ts); ts_close(&ts); }
            if (open_clip(&ts, &clipNo)) break;
            clipStart = lastSync = now;
            idrRequested = 0;
        }
        if (!ts.f) continue;  /* wait for the first IDR */
        ts_write_frame(&ts, frame, len, pts, idr);
        aq_drain(&ts);  /* audio captured up to now, interleaved after its video */
        st.bytes += len;

        if (now - lastSync >= 1000000ULL) {
            sync_clip(&ts);
            lastSync = now;
        }
        if (cfg.clipSec && !cfg.path && !idrRequested && now - clipStart >= cfg.clipSec * 1000000ULL) {
            GK_API_VENC_RequestIDR(VENC_CHN_ID, GK_TRUE);
            idrRequested = 1;
        }
    }
    if (ts.f) { sync_clip(&ts); ts_close(&ts); }
    return NULL;
}

/* ---- MIPI receiver ------------------------------------------------------- */
static int mipi_config(void) {
    combo_dev_attr_t attr;
    combo_dev_t dev = 0;
    sns_clk_source_t clk = 0;
    sns_rst_source_t rst = 0;
    lane_divide_mode_t lanes = LANE_DIVIDE_MODE_0;
    int fd, ret;

    memset(&attr, 0, sizeof(attr));
    attr.devno = 0;
    attr.input_mode = INPUT_MODE_MIPI;
    attr.data_rate = MIPI_DATA_RATE_X1;
    attr.img_rect.width = cfg.mipiW;
    attr.img_rect.height = cfg.fieldH;
    attr.mipi_attr.input_data_type = DATA_TYPE_YUV422_8BIT;
    attr.mipi_attr.wdr_mode = MIPI_WDR_MODE_NONE;
    attr.mipi_attr.lane_id[0] = 2;  /* lane order as the stock firmware set it */
    attr.mipi_attr.lane_id[1] = 0;
    attr.mipi_attr.lane_id[2] = -1;
    attr.mipi_attr.lane_id[3] = -1;

    if ((fd = open("/dev/mipi", O_RDWR)) < 0) { printf("  cannot open /dev/mipi\n"); return -1; }
    printf("  ioctl HS_MODE      -> %d\n", ioctl(fd, MIPI_SET_HS_MODE, &lanes));
    printf("  ioctl MIPI_CLOCK   -> %d\n", ioctl(fd, MIPI_ENABLE_MIPI_CLOCK, &dev));
    printf("  ioctl RESET_MIPI   -> %d\n", ioctl(fd, MIPI_RESET_MIPI, &dev));
    if (cfg.sensorCtl) {
        printf("  ioctl SENSOR_CLOCK -> %d\n", ioctl(fd, MIPI_ENABLE_SENSOR_CLOCK, &clk));
        printf("  ioctl RESET_SENSOR -> %d\n", ioctl(fd, MIPI_RESET_SENSOR, &rst));
    }
    ret = ioctl(fd, MIPI_SET_DEV_ATTR, &attr);
    printf("  ioctl SET_DEV_ATTR -> %d (width %u)\n", ret, cfg.mipiW);
    printf("  ioctl UNRESET_MIPI -> %d\n", ioctl(fd, MIPI_UNRESET_MIPI, &dev));
    if (cfg.sensorCtl)
        printf("  ioctl UNRESET_SENS -> %d\n", ioctl(fd, MIPI_UNRESET_SENSOR, &rst));
    close(fd);
    if (ret) failed = 1;
    return ret;
}

/* Field size, rate and output height for a video standard */
static void set_std(enum tp_std std, unsigned int outHUser) {
    cfg.std = std;
    cfg.fieldH = std == TP_PAL ? 288 : 240;
    cfg.fps = std == TP_PAL ? 50 : 60;
    cfg.outH = outHUser ? outHUser : cfg.fieldH * 2;
}

/* After tp_init: wait up to ms for the decoder to lock (status reg 0x01 bit 6; seen as
 * 0x7c on PAL and 0x78 on NTSC with the matching tables, 0x28 for NTSC on PAL tables).
 * Returns the status value, or -1 if it did not lock. */
static int wait_lock(int ms) {
    int r = -1;
    for (int t = 0; t < ms; t += 100) {
        r = tp_read(0x01);
        if (r >= 0 && (r & 0x40)) return r;
        usleep(100000);
    }
    printf("  no lock within %d ms (reg01 %02x)\n", ms, r & 0xff);
    return -1;
}

int main(int argc, char **argv) {
    unsigned int rawW, outHUser = 0;
    int opt;
    pthread_t capT, wrT, audT;
    MPP_CHN_S vpssSrc = { MOD_ID_VPSS, VPSS_GRP_ID, VPSS_CHN_ID };
    MPP_CHN_S vencDst = { MOD_ID_VENC, 0, VENC_CHN_ID };

    /* defaults, then the stock config file, then command-line options */
    cfg.std = TP_PAL; cfg.autoStd = 1; cfg.secs = 0; cfg.kbps = 8000; cfg.clipSec = 300;
    cfg.outW = 960; cfg.outH = 0; cfg.mipiW = 1920; cfg.sensorCtl = 1;
    cfg.path = NULL; cfg.dir = "/home/DCIM"; cfg.audio = 1; cfg.audioKbps = 96;
    cfg.micBoost = 0; cfg.micGain = -1; cfg.audioGainDb = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    read_config("/home/config.txt");
    if (cfg.autoStd) {
        /* auto: start with the standard seen last time (detection switches if needed) */
        char last[8] = "";
        FILE *f = fopen(STD_FILE, "r");
        if (f) {
            if (fgets(last, sizeof(last), f) && !strncmp(last, "NTSC", 4)) cfg.std = TP_NTSC;
            fclose(f);
        }
    }
    while ((opt = getopt(argc, argv, "s:t:c:b:o:d:W:H:w:a:k")) != -1) {
        switch (opt) {
            case 's':
                cfg.autoStd = !strcmp(optarg, "auto");
                cfg.std = !strcmp(optarg, "ntsc") ? TP_NTSC : TP_PAL;
                break;
            case 't': cfg.secs = atoi(optarg); break;
            case 'c': cfg.clipSec = atoi(optarg) * 60; break;
            case 'b': cfg.kbps = atoi(optarg); break;
            case 'o': cfg.path = optarg; break;
            case 'd': cfg.dir = optarg; break;
            case 'W': cfg.outW = atoi(optarg); break;
            case 'H': outHUser = atoi(optarg); break;
            case 'w': cfg.mipiW = atoi(optarg); break;
            case 'k': cfg.sensorCtl = 0; break;
            case 'a': cfg.audio = atoi(optarg) != 0; break;
            default: fprintf(stderr, "see the header of dvrrec.c for options\n"); return 2;
        }
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    cfg.fieldW = 960;
    set_std(cfg.std, outHUser);
    rawW = cfg.fieldW * 2;  /* UYVY: 2 bytes per pixel, captured as one byte per VI sample */
    printf("dvrrec: %s%s, %u kbps, clips %u min, %s s, mipi width %u, %s\n",
        cfg.autoStd ? "auto, starting with " : "", cfg.std == TP_PAL ? "PAL" : "NTSC",
        cfg.kbps, cfg.clipSec / 60, cfg.secs ? "time-limited" : "run until stopped", cfg.mipiW,
        cfg.path ? cfg.path : cfg.dir);

    printf("[system]\n");
    printf("  SYS_Exit (cleanup) -> %#x, VB_Exit (cleanup) -> %#x\n", GK_API_SYS_Exit(), GK_API_VB_Exit());
    {
        /* sized for PAL, the larger standard, so auto-detection can switch freely */
        unsigned int maxOutH = outHUser ? outHUser : 576;
        VB_CONFIG_S vb;
        memset(&vb, 0, sizeof(vb));
        vb.u32MaxPoolCnt = 16;
        vb.astCommPool[0].u64BlkSize = rawW * 288 * 3 / 2 + 4096;          /* VI raw fields */
        vb.astCommPool[0].u32BlkCnt = 6;
        vb.astCommPool[1].u64BlkSize = cfg.outW * maxOutH * 3 / 2 + 4096;  /* VPSS output */
        vb.astCommPool[1].u32BlkCnt = 6;
        if (CALL(GK_API_VB_SetConfig(&vb))) goto out;
    }
    if (CALL(GK_API_VB_Init())) goto out;
    if (CALL(GK_API_SYS_Init())) goto out_vb;
    {
        VB_POOL_CONFIG_S pool;
        memset(&pool, 0, sizeof(pool));
        pool.u64BlkSize = cfg.fieldW * 288 * 2;
        pool.u32BlkCnt = 6;
        pool.enRemapMode = VB_REMAP_MODE_NONE;
        fieldPool = GK_API_VB_CreatePool(&pool);
        printf("  VB_CreatePool (converted fields) -> %#x\n", fieldPool);
        if (fieldPool == VB_INVALID_POOLID) { failed = 1; goto out_sys; }
    }
    {
        VI_VPSS_MODE_S mode;
        memset(&mode, 0, sizeof(mode));
        CALL(GK_API_SYS_GetVIVPSSMode(&mode));
        mode.aenMode[VI_PIPE_ID] = VI_ONLINE_VPSS_OFFLINE;
        if (CALL(GK_API_SYS_SetVIVPSSMode(&mode))) goto out_pool;
    }

    printf("[mipi]\n");
    if (mipi_config()) goto out_pool;

    printf("[tp2850]\n");
    if (tp_open()) {
        printf("  cannot open /dev/i2c-0\n");
    } else {
        usleep(20000);
        printf("  tp_init(%s, field mode) -> %d write errors\n",
            cfg.std == TP_PAL ? "PAL" : "NTSC", tp_init(cfg.std, 1));
        usleep(500000);
        tp_dump_status();
        /* auto: the decoder only locks with the tables of the standard it receives, so try
         * the first standard, and if it does not lock, the other one */
        if (cfg.autoStd && wait_lock(1500) < 0) {
            enum tp_std first = cfg.std;
            set_std(first == TP_PAL ? TP_NTSC : TP_PAL, outHUser);
            cfg.sensorCtl = 0;  /* switch MIPI rx frame height without resetting the TP2850 */
            if (mipi_config()) goto out_pool;
            printf("  tp_init(%s, field mode) -> %d write errors\n",
                cfg.std == TP_PAL ? "PAL" : "NTSC", tp_init(cfg.std, 1));
            if (wait_lock(1500) < 0) {
                /* no camera signal yet: keep the first standard; the no-video watchdog
                 * restarts the recorder so detection runs again */
                set_std(first, outHUser);
                if (mipi_config()) goto out_pool;
                tp_init(cfg.std, 1);
                printf("  no signal on either standard\n");
            }
            usleep(200000);
            tp_dump_status();
        }
    }
    if (cfg.autoStd && tp_read(0x01) >= 0 && (tp_read(0x01) & 0x40)) {
        /* locked: remember the standard for the next power-on */
        FILE *f = fopen(STD_FILE, "w");
        if (f) {
            fputs(cfg.std == TP_PAL ? "PAL\n" : "NTSC\n", f);
            fclose(f);
        }
    }
    printf("  using %s: field %ux%u @%u -> out %ux%u\n", cfg.std == TP_PAL ? "PAL" : "NTSC",
        cfg.fieldW, cfg.fieldH, cfg.fps, cfg.outW, cfg.outH);

    printf("[vi]\n");
    {
        VI_DEV_ATTR_S dev;
        memset(&dev, 0, sizeof(dev));
        dev.enIntfMode = VI_MODE_MIPI_YUV422;
        dev.enWorkMode = VI_WORK_MODE_1Multiplex;
        dev.au32ComponentMask[0] = 0xFF000000;  /* every byte arrives on this lane */
        dev.au32ComponentMask[1] = 0;
        dev.enScanMode = VI_SCAN_PROGRESSIVE;
        for (unsigned int i = 0; i < VI_MAX_ADCHN_NUM; i++) dev.as32AdChnId[i] = -1;
        dev.enDataSeq = VI_DATA_SEQ_UVUV;
        dev.enInputDataType = VI_DATA_TYPE_YUV;
        dev.stSize.u32Width = rawW;
        dev.stSize.u32Height = cfg.fieldH;
        dev.stBasAttr.stSacleAttr.stBasSize.u32Width = rawW;
        dev.stBasAttr.stSacleAttr.stBasSize.u32Height = cfg.fieldH;
        dev.stWDRAttr.enWDRMode = WDR_MODE_NONE;
        dev.stWDRAttr.u32CacheLine = cfg.fieldH;
        dev.enDataRate = DATA_RATE_X1;
        if (CALL(GK_API_VI_SetDevAttr(VI_DEV, &dev))) goto out_pool;
    }
    if (CALL(GK_API_VI_EnableDev(VI_DEV))) goto out_pool;
    {
        VI_DEV_BIND_PIPE_S bind;
        memset(&bind, 0, sizeof(bind));
        bind.u32Num = 1;
        bind.PipeId[0] = VI_PIPE_ID;
        if (CALL(GK_API_VI_SetDevBindPipe(VI_DEV, &bind))) goto out_dev;
    }
    {
        VI_PIPE_ATTR_S pipe;
        memset(&pipe, 0, sizeof(pipe));
        pipe.enPipeBypassMode = VI_PIPE_BYPASS_NONE;
        pipe.bIspBypass = GK_TRUE;
        pipe.u32MaxW = rawW;
        pipe.u32MaxH = cfg.fieldH;
        pipe.enPixFmt = PIXEL_FORMAT_YVU_SEMIPLANAR_422;
        pipe.enCompressMode = COMPRESS_MODE_NONE;
        pipe.enBitWidth = DATA_BITWIDTH_8;
        pipe.stFrameRate.s32SrcFrameRate = -1;
        pipe.stFrameRate.s32DstFrameRate = -1;
        if (CALL(GK_API_VI_CreatePipe(VI_PIPE_ID, &pipe))) goto out_dev;
    }
    if (CALL(GK_API_VI_StartPipe(VI_PIPE_ID))) goto out_pipe;
    {
        VI_CHN_ATTR_S chn;
        memset(&chn, 0, sizeof(chn));
        chn.stSize.u32Width = rawW;
        chn.stSize.u32Height = cfg.fieldH;
        chn.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;  /* only the luma plane is used */
        chn.enDynamicRange = DYNAMIC_RANGE_SDR8;
        chn.enVideoFormat = VIDEO_FORMAT_LINEAR;
        chn.enCompressMode = COMPRESS_MODE_NONE;
        chn.u32Depth = 3;  /* frames we can pull with GetChnFrame */
        chn.stFrameRate.s32SrcFrameRate = -1;
        chn.stFrameRate.s32DstFrameRate = -1;
        if (CALL(GK_API_VI_SetChnAttr(VI_PIPE_ID, VI_CHN_ID, &chn))) goto out_pipe2;
    }
    if (CALL(GK_API_VI_EnableChn(VI_PIPE_ID, VI_CHN_ID))) goto out_pipe2;

    printf("[vpss]\n");
    {
        VPSS_GRP_ATTR_S grp;
        memset(&grp, 0, sizeof(grp));
        grp.u32MaxW = cfg.fieldW;
        grp.u32MaxH = cfg.fieldH;
        grp.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_422;
        grp.enDynamicRange = DYNAMIC_RANGE_SDR8;
        grp.stFrameRate.s32SrcFrameRate = -1;
        grp.stFrameRate.s32DstFrameRate = -1;
        if (CALL(GK_API_VPSS_CreateGrp(VPSS_GRP_ID, &grp))) goto out_vichn;
    }
    if (CALL(GK_API_VPSS_StartGrp(VPSS_GRP_ID))) goto out_grp;
    {
        VPSS_CHN_ATTR_S chn;
        memset(&chn, 0, sizeof(chn));
        chn.enChnMode = VPSS_CHN_MODE_USER;
        chn.u32Width = cfg.outW;
        chn.u32Height = cfg.outH;
        chn.enVideoFormat = VIDEO_FORMAT_LINEAR;
        chn.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        chn.enDynamicRange = DYNAMIC_RANGE_SDR8;
        chn.enCompressMode = COMPRESS_MODE_NONE;
        chn.stFrameRate.s32SrcFrameRate = -1;
        chn.stFrameRate.s32DstFrameRate = -1;
        chn.stAspectRatio.enMode = ASPECT_RATIO_NONE;
        if (CALL(GK_API_VPSS_SetChnAttr(VPSS_GRP_ID, VPSS_CHN_ID, &chn))) goto out_grp2;
    }
    if (CALL(GK_API_VPSS_EnableChn(VPSS_GRP_ID, VPSS_CHN_ID))) goto out_grp2;

    printf("[venc]\n");
    {
        VENC_CHN_ATTR_S chn;
        VENC_RECV_PIC_PARAM_S recv = { .s32RecvPicNum = -1 };
        memset(&chn, 0, sizeof(chn));
        chn.stVencAttr.enType = PT_H264;
        chn.stVencAttr.u32MaxPicWidth = cfg.outW;
        chn.stVencAttr.u32MaxPicHeight = cfg.outH;
        chn.stVencAttr.u32BufSize = cfg.outW * cfg.outH * 2;
        chn.stVencAttr.u32Profile = 0;  /* baseline, as the stock recorder used */
        chn.stVencAttr.bByFrame = GK_TRUE;
        chn.stVencAttr.u32PicWidth = cfg.outW;
        chn.stVencAttr.u32PicHeight = cfg.outH;
        chn.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        chn.stRcAttr.stH264Cbr.u32Gop = cfg.fps * 2;
        chn.stRcAttr.stH264Cbr.u32StatTime = 1;
        chn.stRcAttr.stH264Cbr.u32SrcFrameRate = cfg.fps;
        chn.stRcAttr.stH264Cbr.fr32DstFrameRate = cfg.fps;
        chn.stRcAttr.stH264Cbr.u32BitRate = cfg.kbps;
        chn.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
        chn.stGopAttr.stNormalP.s32IPQpDelta = 2;
        if (CALL(GK_API_VENC_CreateChn(VENC_CHN_ID, &chn))) goto out_vpsschn;
        {
            /* VUI: 4:3 display shape (sample aspect ratio) and the field rate */
            VENC_H264_VUI_S vui;
            unsigned int a = 4 * cfg.outH, b = 3 * cfg.outW, x = a, y = b;
            while (y) { unsigned int t = x % y; x = y; y = t; }  /* gcd */
            memset(&vui, 0, sizeof(vui));
            GK_API_VENC_GetH264Vui(VENC_CHN_ID, &vui);
            vui.stVuiAspectRatio.aspect_ratio_info_present_flag = 1;
            vui.stVuiAspectRatio.aspect_ratio_idc = 255;  /* Extended_SAR */
            vui.stVuiAspectRatio.sar_width = a / x;
            vui.stVuiAspectRatio.sar_height = b / x;
            vui.stVuiTimeInfo.timing_info_present_flag = 1;
            vui.stVuiTimeInfo.fixed_frame_rate_flag = 1;
            vui.stVuiTimeInfo.num_units_in_tick = 1;
            vui.stVuiTimeInfo.time_scale = cfg.fps * 2;
            printf("  SAR %u:%u, %u fps\n", a / x, b / x, cfg.fps);
            CALL(GK_API_VENC_SetH264Vui(VENC_CHN_ID, &vui));
        }
        if (CALL(GK_API_VENC_StartRecvFrame(VENC_CHN_ID, &recv))) goto out_venc;
    }
    if (CALL(GK_API_SYS_Bind(&vpssSrc, &vencDst))) goto out_venc2;

    printf("[record]\n");
    {
        /* status every 2 s in timed test runs, every 30 s when recording for real */
        unsigned long long start = mono_us(), every = cfg.secs ? 2000000ULL : 30000000ULL;
        unsigned long long next = start + every, lastFrameAt = start;
        unsigned int lastCaptured = 0;
        if (cfg.audio) {
            printf("[audio]\n");
            audioOn = !audio_start();
            if (audioOn) pthread_create(&audT, NULL, audio_thread, NULL);
            else printf("  audio input failed: recording video only\n");
        }
        pthread_create(&wrT, NULL, writer_thread, NULL);
        pthread_create(&capT, NULL, capture_thread, NULL);
        while (running && (!cfg.secs || mono_us() - start < cfg.secs * 1000000ULL)) {
            usleep(100000);
            /* no-video watchdog: camera off, unplugged or on another standard */
            if (st.captured != lastCaptured) {
                lastCaptured = st.captured;
                lastFrameAt = mono_us();
            } else if (!cfg.secs && mono_us() - lastFrameAt > 10000000ULL) {
                printf("  no video for 10 s: stopping so the launcher restarts detection\n");
                noVideo = 1;
                break;
            }
            if (mono_us() >= next) {
                next += every;
                printf("  %7.1f s: captured %u sent %u (fail %u) encoded %u, files %u, %llu bytes, convert %llu us/field"
                       ", audio %u mp3 (peak %lld, %llu us/frame)\n",
                    (mono_us() - start) / 1e6, st.captured, st.sent, st.sendFail, st.encoded, st.files,
                    st.bytes, st.captured ? st.convUs / st.captured : 0, st.mp3Frames, st.audioPeak,
                    st.mp3Frames ? st.audioUs / st.mp3Frames : 0);
                st.audioPeak = 0;
            }
        }
        running = 0;
        pthread_join(capT, NULL);
        pthread_join(wrT, NULL);
        if (audioOn) {
            pthread_join(audT, NULL);
            audio_stop();
            printf("  audio: %u blocks, %u MP3 frames (%u dropped), first at %+.3f s vs video, peak %lld, encode %llu us/frame\n",
                st.audioFrames, st.mp3Frames, st.audioDrops,
                st.firstPts ? ((double)st.firstAudioTs - (double)st.firstPts) / 1e6 : 0.0, st.audioPeak,
                st.mp3Frames ? st.audioUs / st.mp3Frames : 0);
        }
        printf("  total: captured %u, sent %u, send fails %u, getframe fails %u, block fails %u\n",
            st.captured, st.sent, st.sendFail, st.getFail, st.blkFail);
        printf("  encoded %u frames, %llu bytes in %.2f s\n", st.encoded, st.bytes, (mono_us() - start) / 1e6);
        if (st.captured > 1 && st.lastPts > st.firstPts)
            printf("  VI timestamps: %.2f fields/s\n",
                (st.captured - 1) * 1e6 / (double)(st.lastPts - st.firstPts));
        if (!st.encoded) failed = 1;
    }
    tp_dump_status();

    CALL(GK_API_SYS_UnBind(&vpssSrc, &vencDst));
out_venc2:
    CALL(GK_API_VENC_StopRecvFrame(VENC_CHN_ID));
out_venc:
    CALL(GK_API_VENC_DestroyChn(VENC_CHN_ID));
out_vpsschn:
    CALL(GK_API_VPSS_DisableChn(VPSS_GRP_ID, VPSS_CHN_ID));
out_grp2:
    CALL(GK_API_VPSS_StopGrp(VPSS_GRP_ID));
out_grp:
    CALL(GK_API_VPSS_DestroyGrp(VPSS_GRP_ID));
out_vichn:
    CALL(GK_API_VI_DisableChn(VI_PIPE_ID, VI_CHN_ID));
out_pipe2:
    CALL(GK_API_VI_StopPipe(VI_PIPE_ID));
out_pipe:
    CALL(GK_API_VI_DestroyPipe(VI_PIPE_ID));
out_dev:
    CALL(GK_API_VI_DisableDev(VI_DEV));
out_pool:
    unmap_all();
    if (fieldPool != VB_INVALID_POOLID) CALL(GK_API_VB_DestroyPool(fieldPool));
out_sys:
    CALL(GK_API_SYS_Exit());
out_vb:
    CALL(GK_API_VB_Exit());
out:
    tp_close();
    if (noVideo) {
        printf("dvrrec: no video, exit 3\n");
        return 3;
    }
    printf("dvrrec: %s\n", failed ? "FAILED" : "OK");
    return failed ? 1 : 0;
}
