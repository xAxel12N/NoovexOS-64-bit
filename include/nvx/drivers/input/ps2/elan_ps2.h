#ifndef NVX_DRIVERS_INPUT_ELAN_PS2_H
#define NVX_DRIVERS_INPUT_ELAN_PS2_H

#include "include/nvx/base.h"

extern volatile int g_etp_hw;
extern int g_etp_crc, g_etp_maxx, g_etp_maxy, g_etp_xres , g_etp_yres , g_etp_traces;
extern u32 g_etp_fw;
extern u32 g_etp_bad, g_etp_pkts, g_etp_drop;
extern int g_etp_ready;
extern u32 g_etp_frames;

int etp_detect(void);
void etp_irq_byte(u8 v);
void etp_poll(void);

#endif
