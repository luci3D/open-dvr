#!/bin/bash
# Compile-tests the OpenIPC openhisilicon headers with our toolchain and prints the sizes
# of the structs the recorder passes to the MPP library.
set -e
cd "$(dirname "$0")/.."
T=$HOME/toolchains/armv5-eabi--uclibc--stable-2025.08-1
CC=$T/bin/arm-linux-gcc
cat > /tmp/hdrtest.c <<'EOF'
#include "gk_api_sys.h"
#include "gk_api_vb.h"
#include "gk_api_vi.h"
#include "gk_api_vpss.h"
#include "gk_api_venc.h"
#include "mipi.h"
int sizes[] = {
    sizeof(VIDEO_FRAME_INFO_S), sizeof(VI_DEV_ATTR_S), sizeof(VI_PIPE_ATTR_S), sizeof(VI_CHN_ATTR_S),
    sizeof(VPSS_GRP_ATTR_S), sizeof(VPSS_CHN_ATTR_S), sizeof(VENC_CHN_ATTR_S), sizeof(VENC_STREAM_S),
    sizeof(VENC_PACK_S), sizeof(VB_CONFIG_S), sizeof(VB_POOL_CONFIG_S), sizeof(combo_dev_attr_t),
};
EOF
"$CC" -march=armv7-a -mfloat-abi=softfp -Ithird_party/openhisilicon/include -c /tmp/hdrtest.c -o /tmp/hdrtest.o 2>&1 | head -30
"$T/bin/arm-linux-objdump" -s -j .data /tmp/hdrtest.o | tail -4
echo "(frame_info vi_dev vi_pipe vi_chn vpss_grp vpss_chn venc_chn venc_stream venc_pack vb_config vb_pool combo_dev)"

# same structs as defined by OpenIPC Divinus (already proven on the board), for comparison
cat > /tmp/divtest.c <<'EOT'
#include "v4_isp.h"
#include "v4_snr.h"
#include "v4_sys.h"
#include "v4_vb.h"
#include "v4_venc.h"
#include "v4_vi.h"
#include "v4_vpss.h"
int sizes[] = { 0, sizeof(v4_vi_dev), sizeof(v4_vi_pipe), sizeof(v4_vi_chn), sizeof(v4_vpss_grp),
    sizeof(v4_vpss_chn), sizeof(v4_venc_chn), sizeof(v4_venc_strm), sizeof(v4_venc_pack),
    sizeof(v4_vb_pool), 0, sizeof(v4_snr_dev) };
EOT
"$CC" -march=armv7-a -mfloat-abi=softfp -Ithird_party/divinus/hal/hisi -w -c /tmp/divtest.c -o /tmp/divtest.o
"$T/bin/arm-linux-objdump" -s -j .data /tmp/divtest.o | tail -3
