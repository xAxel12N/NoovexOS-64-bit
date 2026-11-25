#ifndef NVX_DRIVERS_VIDEO_AMD_DCN_FLIP_H
#define NVX_DRIVERS_VIDEO_AMD_DCN_FLIP_H
#include "include/nvx/base.h"

#define DCN_FLIP_PIPES 4

typedef struct { int pipe, otg, otg_on, blank; u32 fmt, bpp, sw, pitch, vpx, vpy, vpw, vph, flipctl, frame, vcount; u64 addr; } dcn_scan_t;

int dcn_flip_ok(void);
int dcn_scan_read(int pipe,dcn_scan_t* s);
int dcn_scan_active(dcn_scan_t* s);
int dcn_wait_frames(int otg,int n);
int dcn_flip(int pipe,u64 addr,u32 pitch_px,int immediate);
int dcn_flip_test(void);
int amd_flip_test(void);
void dcn_flip_shell(int argc,char** argv);

#endif
