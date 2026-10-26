/*
 * dvrcap: capture experiment for the open DVR recorder.
 *
 * Builds a fully bound pipeline and records a few seconds of raw H.264:
 *   TP2850 -> MIPI rx (YUV422 8-bit) -> VI (MIPI YUV422, ISP bypassed) -> VPSS (scale)
 *   -> VENC (H.264)
 * Struct definitions come from OpenIPC Divinus (MIT, third_party/divinus); every MPP
 * call is logged with its return code so a failing step is obvious.
 *
 *   dvrcap [-s pal|ntsc] [-m field|frame] [-t seconds] [-o file.h264] [-b kbps]
 *          [-W out_w] [-H out_h] [-q seq] [-c mask1] [-C mask0] [-x bits] [-M mode]
 *          [-P fmt] [-R prec] [-I intf] [-n] [-k]
 *     field: one field per frame, 288/240 lines at 50/60 fps (default)
 *     frame: both fields stacked, 576/480 lines at 25/30 fps (stock firmware layout)
 *     -q: VI data sequence, 0=VUVU 1=UVUV (default 1; the board rejects UYVY etc. here)
 *     -c / -C: VI component masks 1 / 0, hex (default 00FF0000 / FF000000)
 *     -x: data-rate bits, 1 = VI device X2, 2 = MIPI rx X2 (default 0; both rejected)
 *     -R: MIPI rx data type, Divinus v4_common_prec index (default 7 = YUV422 8-bit;
 *         4 = RAW16 reads each YUV pixel as one 16-bit word: luma high byte, chroma low)
 *     -I: VI interface, Divinus v4_vi_intf index (default 9 = MIPI_YUV422, 6 = MIPI)
 *     -M: VI/VPSS mode, 0=VI offline+VPSS offline 1=offline+online 2=online+offline
 *         3=online+online (default 2; VI offline cannot output semi-planar YUV)
 *     -P: VI pipe pixel format: sp422 (default), sp420, uyvy
 *     -n: leave the TP2850 as it is      -k: skip the sensor clock/reset ioctls
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <unistd.h>

#include "v4_isp.h"
#include "v4_snr.h"
#include "v4_sys.h"
#include "v4_vb.h"
#include "v4_venc.h"
#include "v4_vi.h"
#include "v4_vpss.h"

#include "compat.h"
#include "tp2850.h"

int HI_MPI_SYS_Exit(void);
int HI_MPI_SYS_Init(void);
int HI_MPI_SYS_GetVIVPSSMode(v4_sys_oper *mode);
int HI_MPI_SYS_SetVIVPSSMode(v4_sys_oper *mode);
int HI_MPI_SYS_Bind(v4_sys_bind *source, v4_sys_bind *dest);
int HI_MPI_SYS_UnBind(v4_sys_bind *source, v4_sys_bind *dest);
int HI_MPI_VB_SetConfig(v4_vb_pool *config);
int HI_MPI_VB_Init(void);
int HI_MPI_VB_Exit(void);
int HI_MPI_VI_SetDevAttr(int device, v4_vi_dev *config);
int HI_MPI_VI_EnableDev(int device);
int HI_MPI_VI_DisableDev(int device);
int HI_MPI_VI_SetDevBindPipe(int device, v4_vi_bind *config);
int HI_MPI_VI_CreatePipe(int pipe, v4_vi_pipe *config);
int HI_MPI_VI_StartPipe(int pipe);
int HI_MPI_VI_StopPipe(int pipe);
int HI_MPI_VI_DestroyPipe(int pipe);
int HI_MPI_VI_SetChnAttr(int pipe, int channel, v4_vi_chn *config);
int HI_MPI_VI_EnableChn(int pipe, int channel);
int HI_MPI_VI_DisableChn(int pipe, int channel);
int HI_MPI_VPSS_CreateGrp(int group, v4_vpss_grp *config);
int HI_MPI_VPSS_StartGrp(int group);
int HI_MPI_VPSS_StopGrp(int group);
int HI_MPI_VPSS_DestroyGrp(int group);
int HI_MPI_VPSS_SetChnAttr(int group, int channel, v4_vpss_chn *config);
int HI_MPI_VPSS_EnableChn(int group, int channel);
int HI_MPI_VPSS_DisableChn(int group, int channel);
int HI_MPI_VENC_CreateChn(int channel, v4_venc_chn *config);
int HI_MPI_VENC_StartRecvFrame(int channel, int *count);
int HI_MPI_VENC_StopRecvFrame(int channel);
int HI_MPI_VENC_DestroyChn(int channel);
int HI_MPI_VENC_GetFd(int channel);
int HI_MPI_VENC_QueryStatus(int channel, v4_venc_stat *stat);
int HI_MPI_VENC_GetStream(int channel, v4_venc_strm *stream, unsigned int timeout);
int HI_MPI_VENC_ReleaseStream(int channel, v4_venc_strm *stream);

/* Logged call: prints the call and its return code, records failure. */
static int failed;
#define CALL(expr) ({ int _r = (expr); \
    printf("  %-58s -> %#x\n", #expr, _r); if (_r) failed = 1; _r; })

static double now_s(void) {
    return mono_us() / 1e6;
}

static int mipi_config(unsigned int width, unsigned int height, int sensorCtl, int rate2x,
                       int prec) {
    v4_snr_dev cfg;
    unsigned int dev = 0;
    int laneMode = 0, fd, ret;

    memset(&cfg, 0, sizeof(cfg));
    cfg.device = 0;
    cfg.input = V4_SNR_INPUT_MIPI;
    cfg.dataRate2X = rate2x;
    cfg.rect.width = width;
    cfg.rect.height = height;
    cfg.mipi.prec = prec;
    cfg.mipi.mode = V4_SNR_MWDR_NONE;
    /* lane order as the stock firmware configured it (/proc/umap/mipi_rx) */
    cfg.mipi.laneId[0] = 2;
    cfg.mipi.laneId[1] = 0;
    cfg.mipi.laneId[2] = -1;
    cfg.mipi.laneId[3] = -1;

    fd = open("/dev/mipi", O_RDWR);
    printf("  open /dev/mipi -> %d (struct %u bytes)\n", fd, (unsigned)sizeof(cfg));
    if (fd < 0) return -1;
    /* same order as OpenIPC Divinus; the sensor clock/reset lines may also reach the TP2850 */
    printf("  ioctl HSMODE     -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CONF_HSMODE, int), &laneMode));
    printf("  ioctl CLKON_MIPI -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_MIPI, unsigned int), &dev));
    printf("  ioctl RST_MIPI   -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_MIPI, unsigned int), &dev));
    if (sensorCtl) {
        printf("  ioctl CLKON_SENS -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_SENS, unsigned int), &dev));
        printf("  ioctl RST_SENS   -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_SENS, unsigned int), &dev));
    }
    ret = ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CONF_DEV, v4_snr_dev), &cfg);
    printf("  ioctl CONF_DEV   -> %d\n", ret);
    printf("  ioctl UNRST_MIPI -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_UNRST_MIPI, unsigned int), &dev));
    if (sensorCtl)
        printf("  ioctl UNRST_SENS -> %d\n", ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_UNRST_SENS, unsigned int), &dev));
    close(fd);
    if (ret) failed = 1;
    return ret;
}

/* VB/SYS "exists" (0x04) or "busy" (0x12): set up earlier by another process; use it */
#define ALREADY(r) (((r) & 0xff) == 0x04 || ((r) & 0xff) == 0x12)

int main(int argc, char **argv) {
    unsigned int inW, inH, fps, outW, outH, secs, kbps = 8000;
    int seq = V4_VI_SEQ_UVUV;
    const char *path;
    v4_sys_bind viSrc = { .module = V4_SYS_MOD_VIU, .device = 0, .channel = 0 };
    v4_sys_bind vpssDst = { .module = V4_SYS_MOD_VPSS, .device = 0, .channel = 0 };
    v4_sys_bind vpssSrc = { .module = V4_SYS_MOD_VPSS, .device = 0, .channel = 0 };
    v4_sys_bind vencDst = { .module = V4_SYS_MOD_VENC, .device = 0, .channel = 0 };

    enum tp_std std = TP_PAL;
    int fieldMode = 1, skipTp = 0, sensorCtl = 1, opt;
    unsigned int mask1 = 0x00FF0000, mask0 = 0xFF000000;
    int rateBits = 0, mipiPrec = V4_PREC_YUV422_8BIT, viIntf = V4_VI_INTF_MIPI_YUV422;
    int viMode = V4_SYS_OPER_VION_VPSSOFF;
    v4_common_pixfmt pipeFmt = V4_PIXFMT_YVU422SP;
    secs = 10; kbps = 8000; path = "/home/probe/cap.h264"; outW = 960; outH = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    while ((opt = getopt(argc, argv, "s:m:t:o:b:W:H:q:c:C:x:M:P:R:I:nk")) != -1) {
        switch (opt) {
            case 's': std = !strcmp(optarg, "ntsc") ? TP_NTSC : TP_PAL; break;
            case 'm': fieldMode = strcmp(optarg, "frame") != 0; break;
            case 't': secs = atoi(optarg); break;
            case 'o': path = optarg; break;
            case 'b': kbps = atoi(optarg); break;
            case 'W': outW = atoi(optarg); break;
            case 'H': outH = atoi(optarg); break;
            case 'q': seq = atoi(optarg); break;
            case 'c': mask1 = strtoul(optarg, NULL, 16); break;
            case 'C': mask0 = strtoul(optarg, NULL, 16); break;
            case 'x': rateBits = atoi(optarg); break;
            case 'R': mipiPrec = atoi(optarg); break;
            case 'I': viIntf = atoi(optarg); break;
            case 'M': viMode = atoi(optarg); break;
            case 'P':
                pipeFmt = !strcmp(optarg, "sp420") ? V4_PIXFMT_YVU420SP :
                          !strcmp(optarg, "uyvy") ? V4_PIXFMT_YUV422_UYVY : V4_PIXFMT_YVU422SP;
                break;
            case 'n': skipTp = 1; break;
            case 'k': sensorCtl = 0; break;
            default: fprintf(stderr, "see the header of dvrcap.c for options\n"); return 2;
        }
    }
    inW = 960;
    inH = std == TP_PAL ? 576 : 480;
    fps = std == TP_PAL ? 25 : 30;
    if (!outH) outH = inH;
    if (fieldMode) { inH /= 2; fps *= 2; }
    printf("dvrcap: %s %s, in %ux%u @%u -> out %ux%u, %us, %u kbps, seq %d, masks %08x/%08x, rate %d, mipi prec %d, vi intf %d, mode %d, pipe fmt %d, file %s\n",
        std == TP_PAL ? "PAL" : "NTSC", fieldMode ? "field" : "frame",
        inW, inH, fps, outW, outH, secs, kbps, seq, mask0, mask1, rateBits, mipiPrec, viIntf, viMode, pipeFmt, path);

    printf("[system]\n");
    printf("  HI_MPI_SYS_Exit (cleanup) -> %#x\n", HI_MPI_SYS_Exit());
    printf("  HI_MPI_VB_Exit (cleanup)  -> %#x\n", HI_MPI_VB_Exit());
    {
        v4_vb_pool pool;
        unsigned int a = v4_buffer_calculate_venc(inW, inH, V4_PIXFMT_YVU420SP, 16);
        unsigned int b = v4_buffer_calculate_venc(outW, outH, V4_PIXFMT_YVU420SP, 16);
        memset(&pool, 0, sizeof(pool));
        pool.count = 1;
        pool.comm[0].blockSize = a > b ? a : b;
        pool.comm[0].blockCnt = 10;
        printf("  pool: %u blocks x %llu bytes\n", pool.comm[0].blockCnt, pool.comm[0].blockSize);
        int r = HI_MPI_VB_SetConfig(&pool);
        printf("  HI_MPI_VB_SetConfig -> %#x%s\n", r, r && ALREADY(r) ? " (already set up, using it)" : "");
        if (r && !ALREADY(r)) { failed = 1; goto out; }
    }
    {
        int r = HI_MPI_VB_Init();
        printf("  HI_MPI_VB_Init -> %#x%s\n", r, r && ALREADY(r) ? " (already set up, using it)" : "");
        if (r && !ALREADY(r)) { failed = 1; goto out; }
        r = HI_MPI_SYS_Init();
        printf("  HI_MPI_SYS_Init -> %#x%s\n", r, r && ALREADY(r) ? " (already set up, using it)" : "");
        if (r && !ALREADY(r)) { failed = 1; goto out_vb; }
    }
    {
        v4_sys_oper mode[8];
        memset(mode, 0, sizeof(mode));
        CALL(HI_MPI_SYS_GetVIVPSSMode(mode));
        /* only pipe 0 may run VI online; the other pipes keep their (offline) mode */
        mode[0] = viMode;
        if (CALL(HI_MPI_SYS_SetVIVPSSMode(mode))) goto out_sys;
    }

    printf("[mipi]\n");
    if (mipi_config(inW, inH, sensorCtl, (rateBits & 2) ? 1 : 0, mipiPrec)) goto out_sys;

    printf("[tp2850]\n");
    if (tp_open()) {
        printf("  cannot open /dev/i2c-0\n");
    } else {
        usleep(20000);
        tp_dump_status();
        if (!skipTp) {
            int e = tp_init(std, fieldMode);
            printf("  tp_init(%s, %s) -> %d write errors\n",
                std == TP_PAL ? "PAL" : "NTSC", fieldMode ? "field" : "frame", e);
            usleep(500000);
        }
        tp_dump_status();
    }

    printf("[vi]\n");
    {
        v4_vi_dev dev;
        memset(&dev, 0, sizeof(dev));
        dev.intf = viIntf;
        dev.work = V4_VI_WORK_1MULTIPLEX;
        dev.cmpntMask[0] = mask0;
        dev.dataRate2X = (rateBits & 1) ? 1 : 0;
        dev.cmpntMask[1] = mask1;
        dev.progressiveOn = 1;
        for (int i = 0; i < 4; i++) dev.adChn[i] = -1;
        dev.seq = seq;
        dev.rgbModeOn = 0;
        dev.size.width = inW;
        dev.size.height = inH;
        dev.bayerSize.width = inW;
        dev.bayerSize.height = inH;
        dev.wdr = V4_WDR_NONE;
        dev.wdrCacheLine = inH;
        if (CALL(HI_MPI_VI_SetDevAttr(0, &dev))) goto out_sys;
    }
    if (CALL(HI_MPI_VI_EnableDev(0))) goto out_sys;
    {
        v4_vi_bind bind = { .num = 1, .pipeId = { 0, 0 } };
        if (CALL(HI_MPI_VI_SetDevBindPipe(0, &bind))) goto out_dev;
    }
    {
        v4_vi_pipe pipe;
        memset(&pipe, 0, sizeof(pipe));
        pipe.ispBypassOn = 1;
        pipe.maxSize.width = inW;
        pipe.maxSize.height = inH;
        pipe.pixFmt = pipeFmt;
        pipe.compress = V4_COMPR_NONE;
        pipe.prec = V4_PREC_8BPP;
        pipe.srcFps = -1;
        pipe.dstFps = -1;
        if (CALL(HI_MPI_VI_CreatePipe(0, &pipe))) goto out_dev;
    }
    if (CALL(HI_MPI_VI_StartPipe(0))) goto out_pipe;
    {
        v4_vi_chn chn;
        memset(&chn, 0, sizeof(chn));
        chn.size.width = inW;
        chn.size.height = inH;
        chn.pixFmt = V4_PIXFMT_YVU420SP;
        chn.dynRange = V4_HDR_SDR8;
        chn.compress = V4_COMPR_NONE;
        chn.srcFps = -1;
        chn.dstFps = -1;
        if (CALL(HI_MPI_VI_SetChnAttr(0, 0, &chn))) goto out_pipe2;
    }
    if (CALL(HI_MPI_VI_EnableChn(0, 0))) goto out_pipe2;

    printf("[vpss]\n");
    {
        v4_vpss_grp grp;
        memset(&grp, 0, sizeof(grp));
        grp.dest.width = inW;
        grp.dest.height = inH;
        grp.pixFmt = V4_PIXFMT_YVU420SP;
        grp.hdr = V4_HDR_SDR8;
        grp.srcFps = -1;
        grp.dstFps = -1;
        if (CALL(HI_MPI_VPSS_CreateGrp(0, &grp))) goto out_vichn;
    }
    if (CALL(HI_MPI_VPSS_StartGrp(0))) goto out_grp;
    if (CALL(HI_MPI_SYS_Bind(&viSrc, &vpssDst))) goto out_grp2;
    {
        v4_vpss_chn chn;
        memset(&chn, 0, sizeof(chn));
        chn.dest.width = outW;
        chn.dest.height = outH;
        chn.pixFmt = V4_PIXFMT_YVU420SP;
        chn.hdr = V4_HDR_SDR8;
        chn.compress = V4_COMPR_NONE;
        chn.srcFps = -1;
        chn.dstFps = -1;
        if (CALL(HI_MPI_VPSS_SetChnAttr(0, 0, &chn))) goto out_bind1;
    }
    if (CALL(HI_MPI_VPSS_EnableChn(0, 0))) goto out_bind1;

    printf("[venc]\n");
    {
        v4_venc_chn chn;
        int count = -1;
        memset(&chn, 0, sizeof(chn));
        chn.attrib.codec = V4_VENC_CODEC_H264;
        chn.attrib.maxPic.width = outW;
        chn.attrib.maxPic.height = outH;
        chn.attrib.bufSize = ALIGN_UP(outW * outH * 2, 64);
        chn.attrib.profile = 0;  /* baseline, as the stock recorder used */
        chn.attrib.byFrame = 1;
        chn.attrib.pic.width = outW;
        chn.attrib.pic.height = outH;
        chn.rate.mode = V4_VENC_RATEMODE_H264CBR;
        chn.rate.h264Cbr.gop = fps * 2;
        chn.rate.h264Cbr.statTime = 1;
        chn.rate.h264Cbr.srcFps = fps;
        chn.rate.h264Cbr.dstFps = fps;
        chn.rate.h264Cbr.maxBitrate = kbps;
        chn.gop.mode = V4_VENC_GOPMODE_NORMALP;
        chn.gop.normalP.ipQualDelta = 2;
        if (CALL(HI_MPI_VENC_CreateChn(0, &chn))) goto out_vpsschn;
        if (CALL(HI_MPI_VENC_StartRecvFrame(0, &count))) goto out_venc;
    }
    if (CALL(HI_MPI_SYS_Bind(&vpssSrc, &vencDst))) goto out_venc2;

    printf("[record]\n");
    {
        FILE *f = fopen(path, "wb");
        int fd = HI_MPI_VENC_GetFd(0);
        unsigned int frames = 0, timeouts = 0;
        unsigned long long bytes = 0, firstTs = 0, lastTs = 0;
        double start = now_s();

        printf("  file %s -> %p, venc fd %d\n", path, (void *)f, fd);
        while (f && fd >= 0 && now_s() - start < secs) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            v4_venc_stat stat;
            v4_venc_strm strm;
            v4_venc_pack packs[8];
            int r;

            r = poll(&pfd, 1, 2000);
            if (r <= 0) {
                if (++timeouts <= 3) printf("  poll -> %d (no frame within 2 s)\n", r);
                if (timeouts >= 3 && !frames) break;
                continue;
            }
            if (HI_MPI_VENC_QueryStatus(0, &stat) || !stat.curPacks) continue;
            if (stat.curPacks > 8) stat.curPacks = 8;
            memset(&strm, 0, sizeof(strm));
            strm.packet = packs;
            strm.count = stat.curPacks;
            if ((r = HI_MPI_VENC_GetStream(0, &strm, 40))) {
                printf("  GetStream -> %#x\n", r);
                continue;
            }
            for (unsigned int i = 0; i < strm.count; i++) {
                v4_venc_pack *p = &strm.packet[i];
                fwrite(p->data + p->offset, 1, p->length - p->offset, f);
                bytes += p->length - p->offset;
                if (!firstTs) firstTs = p->timestamp;
                lastTs = p->timestamp;
            }
            frames++;
            if (frames == 1 || frames % 100 == 0)
                printf("  frame %u at %.2f s, %llu bytes so far\n", frames, now_s() - start, bytes);
            HI_MPI_VENC_ReleaseStream(0, &strm);
        }
        if (f) fclose(f);
        printf("  frames %u, bytes %llu, wall %.2f s\n", frames, bytes, now_s() - start);
        if (frames > 1 && lastTs > firstTs)
            printf("  encoder timestamps: %.3f s span -> %.2f fps\n",
                (lastTs - firstTs) / 1e6, (frames - 1) * 1e6 / (double)(lastTs - firstTs));
        if (!frames) failed = 1;
    }

    tp_dump_status();
    CALL(HI_MPI_SYS_UnBind(&vpssSrc, &vencDst));
out_venc2:
    CALL(HI_MPI_VENC_StopRecvFrame(0));
out_venc:
    CALL(HI_MPI_VENC_DestroyChn(0));
out_vpsschn:
    CALL(HI_MPI_VPSS_DisableChn(0, 0));
out_bind1:
    CALL(HI_MPI_SYS_UnBind(&viSrc, &vpssDst));
out_grp2:
    CALL(HI_MPI_VPSS_StopGrp(0));
out_grp:
    CALL(HI_MPI_VPSS_DestroyGrp(0));
out_vichn:
    CALL(HI_MPI_VI_DisableChn(0, 0));
out_pipe2:
    CALL(HI_MPI_VI_StopPipe(0));
out_pipe:
    CALL(HI_MPI_VI_DestroyPipe(0));
out_dev:
    CALL(HI_MPI_VI_DisableDev(0));
out_sys:
    CALL(HI_MPI_SYS_Exit());
out_vb:
    CALL(HI_MPI_VB_Exit());
out:
    tp_close();
    printf("dvrcap: %s\n", failed ? "FAILED" : "OK");
    return failed ? 1 : 0;
}
