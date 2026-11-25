#ifndef NVX_DRIVERS_INPUT_EVDEV_H
#define NVX_DRIVERS_INPUT_EVDEV_H

#include "include/nvx/base.h"

#define EVDEV_DEV 43
#define EVDEV_RING 512
extern volatile int g_ev_open;
void evdev_mouse_btn(int idx,int down);
void evdev_rel2(int dx,int dy);
void evdev_wheel(int v120,int h120,int vn,int hn);
void evdev_syn(void);
int evdev_btn_code(int idx);
int evdev_read(char* buf,int len);
int evdev_ready(void);
int evdev_ioctl(u64 req,u8* out,int cap);
void evdev_opened(void);

#endif
