#ifndef NVX_DRIVERS_USB_USBREG_H
#define NVX_DRIVERS_USB_USBREG_H

#include "include/nvx/base.h"

#define USBREG_MAX 32
#define USBREG_CFG 256
typedef struct { u8 used, ctl, root, depth, speed, devnum, cls, sub, proto, nif, cfgval, ifcls[8]; u16 vid, pid, bcd, bcdusb, ma, cfgn; u32 route; u64 t; u8 dd[18], cfg[USBREG_CFG]; char mfr[32], prod[40], ser[32]; } usbreg_t;
extern usbreg_t g_usbreg[USBREG_MAX]; extern u32 g_usbreg_gen;

usbreg_t* usbreg_add(int ctl,int root,u32 route,int depth,int speed,const u8* dd,const u8* cfg,int cfgn);
usbreg_t* usbreg_find(int ctl,int root,u32 route,int depth);
void usbreg_strings(usbreg_t* r,const char* mfr,const char* prod,const char* ser);
void usbreg_gone(int ctl,int root,u32 R,int dR);
int usbreg_parent(int i);
int usbreg_tier_port(const usbreg_t* r,int tier);
int usbreg_count(void);
int usbreg_bus_name(const usbreg_t* r,char* out,int cap);
const char* usbreg_speed_str(int speed);
int usbreg_speed_mbps(int speed);
const char* usbreg_class_name(int cls);
const char* usbreg_driver(const usbreg_t* r);
int usbreg_list(char* out,int cap);
int usbreg_tree(char* out,int cap);
int usbreg_line(const usbreg_t* r,char* out,int cap);
int usbreg_hc_ports(int c);
const char* usbreg_drv_at(int ctl,int root,u32 route,int depth);
int usbreg_hc_count(void);
const char* usbreg_kind(const usbreg_t* r);
int usbreg_card(int i,char* out,int cap);
int usbreg_order(int* idx,int max);

#endif
