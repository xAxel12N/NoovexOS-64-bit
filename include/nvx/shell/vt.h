#ifndef NVX_SHELL_VT_H
#define NVX_SHELL_VT_H

#include "include/nvx/base.h"
#include "include/nvx/shell/terminal.h"

#define VT_BGSET (1u<<24)
#define TIO_ISIG 0x1u
#define TIO_ICANON 0x2u
#define TIO_ECHO 0x8u
#define TIO_ECHOE 0x10u
#define TIO_ECHOK 0x20u
#define TIO_ECHONL 0x40u
#define TIO_IEXTEN 0x8000u
#define TIO_OPOST 0x1u
#define TIO_ONLCR 0x4u
#define TIO_ICRNL 0x100u
#define TIO_INLCR 0x40u
#define TIO_IGNCR 0x80u
#define VT_VINTR 0
#define VT_VQUIT 1
#define VT_VERASE 2
#define VT_VKILL 3
#define VT_VEOF 4
#define VT_VTIME 5
#define VT_VMIN 6
#define VT_VSUSP 10

struct vt_tios { u32 iflag, oflag, cflag, lflag; u8 line; u8 cc[19]; };

extern struct vt_tios g_tios;
extern u32 TBG[TRMAX][TCMAX];
extern int g_vt_curhide, g_vt_appcur, g_vt_alt;

void vt_out(const char* s,int n);
void vt_capture(const char* s,int n);
void vt_reset(void);
void vt_tios_default(void);
int vt_raw(void);
void vt_reply(const char* s);
void vt_key(int c);

#endif
