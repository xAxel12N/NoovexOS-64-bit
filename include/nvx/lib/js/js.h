#ifndef NVX_LIB_JS_H
#define NVX_LIB_JS_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"

typedef struct { unsigned char t; int num; const char* s; int slen; int p; } JTok;

extern int js_err;

JTok* cur(void);
void js_run(const char* src,int len,void (*out_cb)(const char*,int));

#endif
