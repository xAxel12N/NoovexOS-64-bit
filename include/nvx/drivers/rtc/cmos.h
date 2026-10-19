#ifndef NVX_DRIVERS_RTC_CMOS_H
#define NVX_DRIVERS_RTC_CMOS_H

#include "include/nvx/base.h"

u8 cmos(u8 r);
int bcd(u8 v);
void read_clock(int*hh,int*mm,int*ss,int*yr,int*mo,int*dy);

#endif
