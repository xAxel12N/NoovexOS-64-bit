#ifndef NVX_MM_DMAWIN_H
#define NVX_MM_DMAWIN_H

#include "include/nvx/base.h"

#define DMW_FIXED 0x09A00000u
#define DMW_SIZE  0x00200000u
extern u32 g_devbuf;
#define DMW(off) (g_devbuf+(u32)(off))
void dmw_init(void);
const char* dmw_where(void);

#endif
