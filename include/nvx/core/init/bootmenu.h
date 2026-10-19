#ifndef NVX_INIT_BOOTMENU_H
#define NVX_INIT_BOOTMENU_H

#include "include/nvx/base.h"

extern const char* BM_ITEMS[5];
extern int g_bootsel , g_bootcfg , g_bootmsg;
extern int bm_bx,bm_by[5],bm_bw,bm_bh, cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh, cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh, msg_okx,msg_oky,msg_okw,msg_okh;
extern int g_recmode;
extern int g_rec_openterm;
extern int g_recsel , g_recinfo;

void recovery_present(void);
int recovery_input(void);

#endif
