#ifndef NVX_CORE_AUTH_FACEID_IR_H
#define NVX_CORE_AUTH_FACEID_IR_H
#include "include/nvx/base.h"
int fir_available(void);
int fir_start(void);
void fir_tick(void);
void fir_stop(void);
int fir_active(void);
int fir_emitter_on(void);
const char* fir_camera_text(void);
int fir_live(const u32* rgb,int w,int h,int x,int y,int fw,int fh);
#endif
