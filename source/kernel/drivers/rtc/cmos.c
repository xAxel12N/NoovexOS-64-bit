#include "include/nvx/base.h"
#include "include/nvx/drivers/rtc/cmos.h"
u8 cmos(u8 r){ outb(0x70,r); return inb(0x71); }
int bcd(u8 v){ return (v&0x0F)+((v>>4)*10); }
void read_clock(int*hh,int*mm,int*ss,int*yr,int*mo,int*dy){
    for(int _g=0;_g<100000&&(cmos(0x0A)&0x80)&&cmos(0x0A)!=0xFF;_g++){}
    u8 s=cmos(0),mi=cmos(2),h=cmos(4),d=cmos(7),mon=cmos(8),y=cmos(9),rb=cmos(0x0B);
    int pm=(!(rb&0x02))&&(h&0x80); h&=0x7F;
    if(!(rb&0x04)){s=(u8)bcd(s);mi=(u8)bcd(mi);h=(u8)bcd(h);d=(u8)bcd(d);mon=(u8)bcd(mon);y=(u8)bcd(y);}
    if(!(rb&0x02)){ if(pm&&h!=12)h+=12; if(!pm&&h==12)h=0; }
    *ss=s;*mm=mi;*hh=h;*dy=d;*mo=mon;*yr=2000+y;
}
