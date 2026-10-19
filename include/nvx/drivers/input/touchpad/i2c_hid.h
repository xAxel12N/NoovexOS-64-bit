#ifndef NVX_DRIVERS_INPUT_I2C_HID_H
#define NVX_DRIVERS_INPUT_I2C_HID_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/i2c/dw_i2c.h"
#include "include/nvx/drivers/input/touchpad/tp_hid.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"

typedef struct { u16 len, bcd, rdlen, rdreg, inreg, maxin, outreg, maxout, cmdreg, datareg, vid, pid, ver; } ihdesc;
typedef struct {
    int ok, mode, stage; dwi2c bus; u16 addr, descreg; ihdesc d; u8* rdesc; tplay lay; tpeng eng;
    int gpio_ok; u64 gpio_base; u16 pin; u8 active_low;
    char path[72], hid[12], ctrl[72]; u64 ctrl_base; int from_acpi;
    u8 buf[512], last[512]; int lastn; u32 reads, frames, empty, dup;
    int ncont; tpcontact cont[TPF_MAX]; int btn;
    int pend_n, pend_total; tpcontact pend[TPF_MAX]; int pend_need, pend_got, pend_btn, pend_palm; u32 frames_split, frames_lost, rate, rate_cnt; u64 rate_t0;
    u64 last_tick; int gest_last;
    int proto, el_pattern, el_clickpad, el_maxx, el_maxy, el_replen, el_lastbtn; u16 el_product, el_ic; u8 el_fw; u32 el_tp_pkts;
} tpdev;

extern tpdev g_tp;

void ih_delay_ms(int ms);
int ih_get_desc(tpdev* t,u16 reg);
int ih_read_input(tpdev* t,u8* out,int n);
int ih_setup(tpdev* t);
void ih_frame(tpdev* t,const tpcontact* c,int n,int btn);
void tp_poll(void);

#endif
