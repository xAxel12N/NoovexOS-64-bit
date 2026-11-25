#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <asm-generic/termios.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#include "drivers/usb/class/usbser_proto.h"
static int fails, checks;
#define CK(c,m) do{ checks++; if(!(c)){ printf("FAIL %s (line %d)\n",m,__LINE__); fails++; } }while(0)
static int put(u8* b,int o,const u8* d,int n){ memcpy(b+o,d,n); return o+n; }
static int cfghead(u8* b,int o,int nif){ b[0]=9; b[1]=2; b[2]=(u8)o; b[3]=(u8)(o>>8); b[4]=(u8)nif; b[5]=1; b[6]=0; b[7]=0x80; b[8]=50; return o; }
static int ep(u8* b,int o,u8 a,u8 ty,u16 mp){ u8 e[7]={7,5,a,ty,(u8)mp,(u8)(mp>>8),ty==3?16:0}; return put(b,o,e,7); }
static int ifc(u8* b,int o,u8 n,u8 alt,u8 ne,u8 cls,u8 sub,u8 pr){ u8 d[9]={9,4,n,alt,ne,cls,sub,pr,0}; return put(b,o,d,9); }
static int mk_pico(u8* b){
    int o=9; u8 iad[8]={8,0x0B,0,2,2,2,0,0}; o=put(b,o,iad,8);
    o=ifc(b,o,0,0,1,2,2,0); u8 h[5]={5,0x24,0,0x20,1}; o=put(b,o,h,5); u8 cm[5]={5,0x24,1,0,1}; o=put(b,o,cm,5); u8 un[5]={5,0x24,6,0,1}; o=put(b,o,un,5); o=ep(b,o,0x81,3,8);
    o=ifc(b,o,1,0,2,0x0A,0,0); o=ep(b,o,0x02,2,64); o=ep(b,o,0x82,2,64);
    o=ifc(b,o,2,0,0,0xFF,0,1);
    return cfghead(b,o,3); }
static int mk_esp(u8* b){
    int o=9; u8 iad[8]={8,0x0B,0,2,2,2,0,0}; o=put(b,o,iad,8);
    o=ifc(b,o,0,0,1,2,2,0); o=ep(b,o,0x82,3,64);
    o=ifc(b,o,1,0,2,0x0A,2,0); o=ep(b,o,0x01,2,64); o=ep(b,o,0x81,2,64);
    o=ifc(b,o,2,0,2,0xFF,0xFF,1); o=ep(b,o,0x02,2,64); o=ep(b,o,0x83,2,64);
    return cfghead(b,o,3); }
static int mk_ft2232(u8* b){
    int o=9; o=ifc(b,o,0,0,2,0xFF,0xFF,0xFF); o=ep(b,o,0x81,2,512); o=ep(b,o,0x02,2,512);
    o=ifc(b,o,1,0,2,0xFF,0xFF,0xFF); o=ep(b,o,0x83,2,512); o=ep(b,o,0x04,2,512);
    return cfghead(b,o,2); }
static int mk_ch340(u8* b){ int o=9; o=ifc(b,o,0,0,3,0xFF,1,2); o=ep(b,o,0x82,2,32); o=ep(b,o,0x02,2,32); o=ep(b,o,0x81,3,8); return cfghead(b,o,1); }
static int near(u32 a,u32 b,u32 pct){ u64 d=a>b?a-b:b-a; return d*100<=(u64)b*pct; }
int main(void){
    u8 c[512]; struct usr_cfg cf; int n;
    CK(usr_lookup(0x0403,0x6001)==USR_FTDI&&usr_lookup(0x0403,0x6015)==USR_FTDI&&usr_lookup(0x0403,0x6010)==USR_FTDI,"FTDI ids");
    CK(usr_lookup(0x10C4,0xEA60)==USR_CP210X&&usr_lookup(0x1A86,0x7523)==USR_CH341&&usr_lookup(0x067B,0x2303)==USR_PL2303,"CP210x/CH340/PL2303 ids");
    CK(usr_lookup(0x046D,0xC52B)==USR_NONE,"unknown id");
    n=mk_pico(c); CK(usr_chip(0x2E8A,0x000A,c,n)==USR_ACM,"Pico is CDC-ACM");
    CK(usr_parse(USR_ACM,c,n,&cf)==1&&cf.p[0].ifn==1&&cf.p[0].ctl==0&&cf.p[0].ie==0x82&&cf.p[0].oe==0x02&&cf.p[0].intr==0x81&&cf.p[0].im==64,"Pico data/control interfaces");
    n=mk_esp(c); CK(usr_chip(0x303A,0x1001,c,n)==USR_ACM,"ESP32-S3 USB serial/JTAG is CDC-ACM");
    CK(usr_parse(USR_ACM,c,n,&cf)==1&&cf.p[0].ifn==1&&cf.p[0].ctl==0&&cf.p[0].ie==0x81&&cf.p[0].oe==0x01,"ESP32-S3: JTAG interface ignored");
    n=mk_ft2232(c); CK(usr_parse(USR_FTDI,c,n,&cf)==2&&cf.p[1].ifn==1&&cf.p[1].ie==0x83&&cf.p[1].oe==0x04&&cf.p[0].im==512,"FT2232H two ports");
    n=mk_ch340(c); CK(usr_chip(0x1A86,0x7523,c,n)==USR_CH341&&usr_parse(USR_CH341,c,n,&cf)==1&&cf.p[0].ie==0x82&&cf.p[0].oe==0x02&&cf.p[0].intr==0x81,"CH340 endpoints");
    CK(usr_ftdi_div(9600,0)==0x4138,"FTDI 9600 divisor"); CK(usr_ftdi_div(115200,0)==0x1A,"FTDI 115200 divisor"); CK(usr_ftdi_div(3000000,0)==0,"FTDI 3M divisor"); CK(usr_ftdi_div(2000000,0)==1,"FTDI 2M divisor");
    CK(usr_ftdi_div(460800,0)==0x4006,"FTDI 460800 divisor"); CK(usr_ftdi_div(12000000,1)==0x20000,"FT2232H 12M divisor"); CK(usr_ftdi_div(115200,1)==(0x20000u|(833>>3)|(3u<<14)),"FT2232H 115200 divisor"); CK(usr_ftdi_div(12000000,0)==0xFFFFFFFFu,"FT232R refuses 12M"); CK(usr_ftdi_div(0,0)==0xFFFFFFFFu,"FTDI refuses 0");
    static const u32 std[]={300,600,1200,2400,4800,9600,19200,38400,57600,74880,115200,230400,250000,460800,500000,921600,1000000,1500000,2000000,3000000};
    for(unsigned i=0;i<sizeof std/sizeof std[0];i++){ u32 b=std[i]; char m[64];
        snprintf(m,sizeof m,"FTDI %u within 3%%",b); CK(near(usr_ftdi_rate(usr_ftdi_div(b,0),0),b,3),m);
        snprintf(m,sizeof m,"FT-H %u within 3%%",b); CK(near(usr_ftdi_rate(usr_ftdi_div(b,1),1),b,3),m);
        if(b<=2000000){ u16 v; snprintf(m,sizeof m,"CH341 %u within 3%%",b); CK(usr_ch341_div(b,&v)&&near(usr_ch341_rate(v),b,3),m); } }
    { u16 v=0; CK(usr_ch341_div(115200,&v)&&v==0xCC83,"CH341 115200 = 0xCC83"); CK(usr_ch341_div(9600,&v)&&v==0xB282,"CH341 9600 = 0xB282"); CK(!usr_ch341_div(3000000,&v),"CH341 refuses 3M"); CK(!usr_ch341_div(10,&v),"CH341 refuses 10 baud"); CK(usr_ch341_div(921600,&v)&&(v&4)&&near(usr_ch341_rate(v),921600,1),"CH341 921600 uses the doubled clock"); }
    struct usr_req q[USR_MAXREQ]; struct usr_port P; memset(&P,0,sizeof P); P.ifn=1; P.ctl=0;
    struct usr_line L={115200,8,0,0};
    n=usr_line_reqs(USR_ACM,&P,0,&L,q); CK(n==1&&q[0].rt==0x21&&q[0].req==0x20&&q[0].idx==0&&q[0].len==7&&q[0].data[0]==0x00&&q[0].data[1]==0xC2&&q[0].data[2]==0x01&&q[0].data[6]==8&&q[0].data[4]==0&&q[0].data[5]==0,"ACM SET_LINE_CODING 115200 8N1 on the control interface");
    n=usr_mctl_req(USR_ACM,&P,0,1,0,q); CK(n==1&&q[0].req==0x22&&q[0].val==1&&q[0].idx==0,"ACM DTR on, RTS off");
    n=usr_break_req(USR_ACM,&P,0,&L,1,0,0,q); CK(n==1&&q[0].req==0x23&&q[0].val==0xFFFF,"ACM SEND_BREAK");
    P.ifn=0; n=usr_line_reqs(USR_CP210X,&P,0,&L,q); CK(n==2&&q[0].rt==0x41&&q[0].req==0x1E&&q[0].len==4&&q[0].data[0]==0x00&&q[0].data[1]==0xC2&&q[0].data[2]==0x01&&q[1].req==0x03&&q[1].val==0x0800,"CP210x baud + 8N1");
    { struct usr_line L2={9600,7,2,2}; n=usr_line_reqs(USR_CP210X,&P,0,&L2,q); CK(n==2&&q[1].val==(2|(2<<4)|(7<<8)),"CP210x 7E2"); }
    n=usr_mctl_req(USR_CP210X,&P,0,1,1,q); CK(q[0].req==7&&q[0].val==0x0303,"CP210x SET_MHS DTR+RTS");
    n=usr_init_reqs(USR_CP210X,&P,0,q); CK(n==1&&q[0].req==0&&q[0].val==1,"CP210x IFC_ENABLE");
    n=usr_line_reqs(USR_CH341,&P,0,&L,q); CK(n==2&&q[0].req==0x9A&&q[0].val==0x1312&&q[0].idx==0xCC83&&q[1].val==0x2518&&q[1].idx==0xC3,"CH341 baud + LCR 8N1");
    { struct usr_line L2={9600,7,1,2}; CK(usr_ch341_lcr(&L2)==(0xC0|2|0x08|0x04),"CH341 LCR 7O2"); L2.parity=2; CK(usr_ch341_lcr(&L2)==(0xC0|2|0x08|0x10|0x04),"CH341 LCR 7E2"); }
    n=usr_mctl_req(USR_CH341,&P,0,1,1,q); CK(q[0].req==0xA4&&q[0].val==0x9F,"CH341 modem control inverted DTR+RTS");
    n=usr_mctl_req(USR_CH341,&P,0,0,0,q); CK(q[0].val==0xFF,"CH341 modem control both off");
    n=usr_break_req(USR_CH341,&P,0,&L,1,0xFF,0xC3,q); CK(q[0].val==0x1805&&q[0].idx==((0x83<<8)|0xFE),"CH341 break on");
    n=usr_break_req(USR_CH341,&P,0,&L,0,0xFE,0x83,q); CK(q[0].idx==((0xC3<<8)|0xFF),"CH341 break off");
    n=usr_line_reqs(USR_FTDI,&P,0,&L,q); CK(n==2&&q[0].req==3&&q[0].val==0x1A&&q[0].idx==0&&q[1].req==4&&q[1].val==8&&q[1].idx==0,"FT232R 115200 8N1");
    { int f=usr_ft_flags(0x0700,2); P.ifn=1; n=usr_line_reqs(USR_FTDI,&P,f,&L,q); CK(n==2&&q[0].idx==((usr_ftdi_div(115200,1)>>16)<<8|2)&&q[1].idx==2,"FT2232H port B index"); P.ifn=0; }
    { int f=usr_ft_flags(0x0900,1); CK((f&USR_FT_HCLK)&&(f&USR_FT_HIX)&&!(f&USR_FT_MULTI),"FT232H flags"); n=usr_mctl_req(USR_FTDI,&P,f,1,0,q); CK(q[0].idx==0&&q[0].val==0x0301,"FT232H modem control index 0"); }
    CK(usr_ft_flags(0x0600,1)==0,"FT232R flags");
    { struct usr_line L2={115200,8,2,2}; CK(usr_ftdi_data(&L2,1)==(8|(2<<8)|(2<<11)|0x4000),"FTDI data word 8E2 + break"); L2.bits=5; CK(usr_line_reqs(USR_FTDI,&P,0,&L2,q)<0,"FTDI refuses 5 bits"); }
    n=usr_init_reqs(USR_PL2303,&P,0,q); CK(n==11&&q[0].rt==0xC0&&q[10].rt==0x40&&q[10].val==2&&q[10].idx==0x44,"PL2303 vendor init");
    n=usr_line_reqs(USR_PL2303,&P,0,&L,q); CK(n==2&&q[0].req==0x20&&q[0].len==7&&q[1].rt==0x40,"PL2303 line coding");
    { u8 st=0x30; CK(usr_modem_in(USR_FTDI,&st,1)==(USR_TIOCM_CTS|USR_TIOCM_DSR),"FTDI modem status"); u8 cs=0xFE; CK(usr_modem_in(USR_CH341,&cs,1)==USR_TIOCM_CTS,"CH341 status inverted"); u8 cp=0xB0; CK(usr_modem_in(USR_CP210X,&cp,1)==(USR_TIOCM_CTS|USR_TIOCM_DSR|USR_TIOCM_CAR),"CP210x status"); }
    { u8 nt[10]={0xA1,0x20,0,0,0,0,2,0,3,0}; CK(usr_acm_notify(nt,10)==(USR_TIOCM_CAR|USR_TIOCM_DSR),"ACM SERIAL_STATE"); }
    { u8 in[130], out[130]; int st=-1; memset(in,'x',130); in[0]=0x31; in[1]=0x60; in[64]=0x11; in[65]=0x60; in[128]=0x01; in[129]=0x60;
        int k=usr_ftdi_strip(in,130,64,out,&st); CK(k==124&&st==0x01&&out[0]=='x',"FTDI status bytes stripped per packet"); k=usr_ftdi_strip(in,2,64,out,&st); CK(k==0,"FTDI status-only packet"); }
    CK(USR_CBAUD==CBAUD&&USR_CBAUDEX==CBAUDEX&&USR_BOTHER==BOTHER&&USR_CSIZE==CSIZE&&USR_CSTOPB==CSTOPB&&USR_PARENB==PARENB&&USR_PARODD==PARODD&&USR_HUPCL==HUPCL&&USR_CLOCAL==CLOCAL&&USR_CMSPAR==CMSPAR&&USR_CRTSCTS==CRTSCTS&&USR_CREAD==CREAD,"cflag bits match asm/termbits.h");
    CK(USR_TIOCM_DTR==TIOCM_DTR&&USR_TIOCM_RTS==TIOCM_RTS&&USR_TIOCM_CTS==TIOCM_CTS&&USR_TIOCM_CAR==TIOCM_CAR&&USR_TIOCM_RNG==TIOCM_RNG&&USR_TIOCM_DSR==TIOCM_DSR,"TIOCM bits match");
    CK(TCGETS==0x5401&&TCSETS==0x5402&&TCSETSW==0x5403&&TCSETSF==0x5404&&TCSBRK==0x5409&&TCXONC==0x540A&&TCFLSH==0x540B&&TIOCMGET==0x5415&&TIOCMBIS==0x5416&&TIOCMBIC==0x5417&&TIOCMSET==0x5418&&FIONREAD==0x541B&&TIOCOUTQ==0x5411,"tty ioctl numbers");
    CK(TIOCSBRK==0x5427&&TIOCCBRK==0x5428&&TCSBRKP==0x5425&&TIOCEXCL==0x540C&&TIOCNXCL==0x540D&&TIOCGETD==0x5424,"break/exclusive/line discipline ioctl numbers");
    CK((u32)TCGETS2==0x802C542Au&&(u32)TCSETS2==0x402C542Bu&&(u32)TCSETSW2==0x402C542Cu&&(u32)TCSETSF2==0x402C542Du,"termios2 ioctl numbers");
    CK(sizeof(struct termios2)==44&&offsetof(struct termios2,c_cflag)==8&&offsetof(struct termios2,c_line)==16&&offsetof(struct termios2,c_cc)==17&&offsetof(struct termios2,c_ispeed)==36&&offsetof(struct termios2,c_ospeed)==40,"struct termios2 layout");
    CK(VMIN==6&&VTIME==5,"VMIN/VTIME index");
    { struct usr_line l; CK(usr_cflag_line(B115200|CS8|CREAD|CLOCAL,0,&l)&&l.baud==115200&&l.bits==8&&l.parity==0&&l.stop==0,"B115200 CS8");
      CK(usr_cflag_line(BOTHER|CS8,74880,&l)&&l.baud==74880,"BOTHER 74880");
      CK(usr_cflag_line(B9600|CS7|PARENB|PARODD|CSTOPB,0,&l)&&l.baud==9600&&l.bits==7&&l.parity==1&&l.stop==2,"7O2");
      CK(usr_cflag_line(B9600|CS8|PARENB|CMSPAR|PARODD,0,&l)&&l.parity==3,"mark parity"); CK(usr_cflag_line(B9600|CS8|PARENB|CMSPAR,0,&l)&&l.parity==4,"space parity");
      CK(!usr_cflag_line(B0|CS8,0,&l),"B0 hangs up");
      CK(usr_cflag_line(B4000000|CS8,0,&l)&&l.baud==4000000&&usr_cflag_line(B57600|CS8,0,&l)&&l.baud==57600&&usr_cflag_line(B50|CS5,0,&l)&&l.baud==50&&l.bits==5,"baud table edges");
      static const u32 cb[]={B50,B300,B1200,B9600,B19200,B38400,B57600,B115200,B230400,B460800,B500000,B921600,B1000000,B1500000,B2000000,B3000000,B4000000};
      int ok=1; for(unsigned i=0;i<sizeof cb/sizeof cb[0];i++){ u32 b=usr_cflag_baud(cb[i],0); if(usr_baud_code(b)!=cb[i])ok=0; } CK(ok,"baud codes round trip");
      CK(usr_baud_code(74880)==BOTHER,"non-standard rate uses BOTHER"); }
    srand(7); int fz=0;
    for(int it=0;it<300000;it++){ u8 r[300]; int len=rand()%300; for(int i=0;i<len;i++)r[i]=(u8)rand(); if(len>9&&(rand()&1)){ for(int i=0;i+1<len;){ int bl=2+rand()%10; r[i]=(u8)bl; r[i+1]=(u8)(rand()%2?4:5); i+=bl; } }
        int ch=1+rand()%5; struct usr_cfg o; int k=usr_parse(ch,r,len,&o); usr_has_acm(r,len); if(k<0||k>USR_MAXIF)fz++; for(int i=0;i<k;i++) if(!o.p[i].ie||!o.p[i].oe||!(o.p[i].ie&0x80)||(o.p[i].oe&0x80))fz++; }
    CK(fz==0,"descriptor fuzz: ports always have a bulk IN and OUT endpoint");
    for(u32 b=46;b<4000000;b+=b/7+1){ u16 v; struct usr_line l={b,8,0,0}; u32 d=usr_ftdi_div(b,b&1); if(d!=0xFFFFFFFFu&&!near(usr_ftdi_rate(d,b&1),b,5))fz++; if(usr_ch341_div(b,&v)&&!near(usr_ch341_rate(v),b,5))fz++; for(int ch=1;ch<=5;ch++)usr_line_reqs(ch,&P,b&7,&l,q); }
    CK(fz==0,"baud sweep: chosen divisors stay within 5%");
    printf("%d checks, %d failures\n",checks,fails);
    if(!fails)printf("usbser host test: ALL PASS\n");
    return fails?1:0; }
