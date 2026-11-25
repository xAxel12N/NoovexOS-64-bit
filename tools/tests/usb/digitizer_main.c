#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/nvx/base.h"
#include "include/nvx/core/sysfeat.h"
volatile u64 g_ticks; u32 W=1920, H=1080;
static int nfail, npass, absn, absx, absy, absxr, btn, presses, rpresses, scv, sch, gest;
void klog(const char* s){ (void)s; }
void fg_boot(struct featguard* g){ (void)g; }
int fg_begin(struct featguard* g){ return g->on&&!g->dead; }
void fg_ok(struct featguard* g){ (void)g; }
void fg_fail(struct featguard* g,const char* w){ (void)w; g->dead=1; }
void fg_enable(struct featguard* g,int on){ g->on=on; }
void in_abs(int x,int xr,int y,int yr){ absn++; absx=x; absy=y; absxr=xr; (void)yr; }
void in_btn_raw(int l,int r,int m){ int nb=(l?1:0)|(r?2:0)|(m?4:0); if((nb&1)&&!(btn&1))presses++; if((nb&2)&&!(btn&2))rpresses++; btn=nb; }
void in_scroll(int v,int h){ scv+=v; sch+=h; }
void in_gesture(int g){ gest=g; }
#include "drivers/usb/class/hid_digitizer.c"
static void chk(const char* n,int ok){ if(ok)npass++; else nfail++; printf("%s %s\n",n,ok?"ok":"FAIL"); }
static const u8 pen_rd[]={0x05,0x0D,0x09,0x02,0xA1,0x01,0x85,0x02,0x09,0x20,0xA1,0x00,
    0x09,0x42,0x09,0x44,0x09,0x3C,0x09,0x45,0x09,0x32,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x05,0x81,0x02,0x95,0x03,0x81,0x03,
    0x05,0x01,0x09,0x30,0x26,0xFF,0x7F,0x75,0x10,0x95,0x01,0x81,0x02,0x09,0x31,0x81,0x02,
    0x05,0x0D,0x09,0x30,0x26,0xFF,0x0F,0x75,0x10,0x81,0x02,
    0x09,0x3D,0x15,0xC4,0x25,0x3C,0x75,0x08,0x81,0x02,0x09,0x3E,0x81,0x02,0xC0,0xC0};
static const u8 wac_rd[]={0x06,0x0D,0xFF,0x09,0x02,0xA1,0x01,0x85,0x10,0x09,0x42,0x09,0x32,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x02,0x81,0x02,0x95,0x06,0x81,0x03,
    0x0A,0x30,0x01,0x26,0xFF,0x7F,0x75,0x10,0x95,0x01,0x81,0x02,0x0A,0x31,0x01,0x81,0x02,0x09,0x30,0x26,0xFF,0x0F,0x81,0x02,0xC0};
#define FINGER 0x09,0x22,0xA1,0x02,0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,0x09,0x47,0x81,0x02,0x95,0x06,0x81,0x03, \
    0x09,0x51,0x75,0x08,0x95,0x01,0x25,0x0A,0x81,0x02,0x05,0x01,0x09,0x30,0x26,0xFF,0x0F,0x75,0x10,0x95,0x01,0x81,0x02,0x09,0x31,0x81,0x02,0x05,0x0D,0xC0
static const u8 ts_rd[]={0x05,0x0D,0x09,0x04,0xA1,0x01,0x85,0x01,FINGER,FINGER,0x09,0x54,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x02,
    0x85,0x02,0x09,0x55,0x25,0x0A,0x75,0x08,0x95,0x01,0xB1,0x02,0xC0,
    0x09,0x0E,0xA1,0x01,0x85,0x03,0x09,0x23,0xA1,0x02,0x09,0x52,0x09,0x53,0x25,0x0A,0x75,0x08,0x95,0x02,0xB1,0x02,0xC0,0xC0};
static const u8 kb_rd[]={0x05,0x01,0x09,0x06,0xA1,0x01,0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0xC0};
static void pen_rep(u8* r,int bits,int x,int y,int p,int tx,int ty){ r[0]=2; r[1]=(u8)bits; r[2]=(u8)x; r[3]=(u8)(x>>8); r[4]=(u8)y; r[5]=(u8)(y>>8); r[6]=(u8)p; r[7]=(u8)(p>>8); r[8]=(u8)tx; r[9]=(u8)ty; }
static void fing(u8* f,int tip,int id,int x,int y){ f[0]=(u8)(tip?3:0); f[1]=(u8)id; f[2]=(u8)x; f[3]=(u8)(x>>8); f[4]=(u8)y; f[5]=(u8)(y>>8); }
static int ts_rep(int e,int cc,int n,const int* t,const int* id,const int* x,const int* y,u32 ms){ u8 r[14]; memset(r,0,14); r[0]=1; for(int i=0;i<n;i++)fing(r+1+6*i,t[i],id[i],x[i],y[i]); r[13]=(u8)cc; return dg_report(e,r,14,ms); }
static int evhas(const dgev* e,int n,int t,int c,int v){ for(int i=0;i<n;i++) if(e[i].type==t&&e[i].code==c&&e[i].value==v)return 1; return 0; }
int main(void){
    dglay L; int r=dg_parse(pen_rd,sizeof(pen_rd),&L);
    chk("pen: parsed as pen, report 2, 10 bytes",r==1&&L.pen&&L.pen_rid==2&&L.in_max==10);
    chk("pen: tip/barrel/invert/eraser/in-range bits 0..4",L.tip.off==0&&L.bar1.off==1&&L.inv.off==2&&L.eras.off==3&&L.rng.off==4);
    chk("pen: X/Y 16-bit 0..32767, pressure 0..4095, tilt -60..60 signed",L.x.off==8&&L.y.off==24&&L.xmax==32767&&L.pres.off==40&&L.pmax==4095&&L.tx.off==56&&L.tx.sgn&&L.tmin==-60&&L.tmax==60&&L.ty.off==64);
    u8 feat[64]; int fl, fr;
    chk("keyboard descriptor: not a digitizer",dg_probe(0,0x1234,1,kb_rd,sizeof(kb_rd),feat,&fl,&fr)==0&&fl==0);
    chk("pen: probe returns report length, no feature report",dg_probe(0,0x1234,1,pen_rd,sizeof(pen_rd),feat,&fl,&fr)==10&&fl==0);
    dg_bind(0,5); dg_bind(1,6);
    u8 pr[10]; dgev ev[64]; int ne;
    pen_rep(pr,0x10,16384,8192,0,0,0); absn=0; dg_report(5,pr,10,0);
    chk("pen hover: pointer moves to (0.5,0.25), no button",absn==1&&absxr==65535&&absx>=32760&&absx<=32770&&absy>=16380&&absy<=16390&&btn==0);
    ne=dg_ev_take(ev,64); chk("evdev hover: BTN_TOOL_PEN 1, ABS_X, ABS_Y, SYN_REPORT",evhas(ev,ne,1,0x140,1)&&evhas(ev,ne,3,0,16384)&&evhas(ev,ne,3,1,8192)&&ev[ne-1].type==0&&!evhas(ev,ne,1,0x14a,1));
    g_dg_curve=2; pen_rep(pr,0x11,16384,8192,2048,10,(u8)-20); dg_report(5,pr,10,0);
    chk("pen tip: left button, pressure 2048 -> ~0.5 with linear curve, tilt",(btn&1)&&g_dg_pen.pres>31000&&g_dg_pen.pres<34500&&g_dg_pen.tx==10&&g_dg_pen.ty==-20);
    ne=dg_ev_take(ev,64); chk("evdev tip: BTN_TOUCH 1, ABS_PRESSURE 2048, ABS_TILT_X/Y",evhas(ev,ne,1,0x14a,1)&&evhas(ev,ne,3,0x18,2048)&&evhas(ev,ne,3,0x1a,10)&&evhas(ev,ne,3,0x1b,-20));
    pen_rep(pr,0x13,16384,8192,2048,10,(u8)-20); dg_report(5,pr,10,0); ne=dg_ev_take(ev,64);
    chk("barrel button: right button + BTN_STYLUS",(btn&2)&&evhas(ev,ne,1,0x14b,1));
    pen_rep(pr,0x00,0,0,0,0,0); absn=0; dg_report(5,pr,10,0); ne=dg_ev_take(ev,64);
    chk("out of range: no jump to 0,0, buttons released, BTN_TOOL_PEN 0",absn==0&&btn==0&&evhas(ev,ne,1,0x140,0)&&evhas(ev,ne,1,0x14a,0));
    pen_rep(pr,0x1C,100,100,500,0,0); dg_report(5,pr,10,0); ne=dg_ev_take(ev,64);
    chk("eraser end: BTN_TOOL_RUBBER, eraser flag, counts as tip",evhas(ev,ne,1,0x141,1)&&g_dg_pen.eraser&&(btn&1));
    pen_rep(pr,0x00,0,0,0,0,0); dg_report(5,pr,10,0); dg_ev_take(ev,64);
    r=dg_parse(wac_rd,sizeof(wac_rd),&L);
    chk("Wacom vendor page 0xFF0D maps to digitizer/generic desktop (Intuos S CTL-4100 HID mode)",r==1&&L.pen_rid==16&&L.x.off==8&&L.xmax==32767&&L.y.off==24&&L.pmax==4095&&L.rng.off==1);
    const dgwacom* w=dg_wacom(0x056A,0x0374); chk("libwacom table: 056a:0374 CTL-4100, 056a:037a CTL-472, 056a:030e CTL-480 Bamboo protocol",w&&!strncmp(w->model,"CTL-4100",8)&&dg_wacom(0x056A,0x037a)&&dg_wacom(0x056A,0x030e)->proto==DGP_BAMBOO&&!dg_wacom(0x046d,0x0374));
    chk("Wacom CTL-480: probe sends mode feature report 2 = 2",dg_probe(1,0x056A,0x030e,kb_rd,sizeof(kb_rd),feat,&fl,&fr)==9&&fl==2&&feat[0]==2&&feat[1]==2&&fr==2);
    dg_bind(1,7); u8 bp[9]={2,0x21,0x10,0x27,0x88,0x13,0x00,0x02,0}; absn=0; dg_report(7,bp,9,0);
    chk("Wacom pen protocol: prox+tip, x=10000 y=5000 p=512",absn==1&&(btn&1)&&g_dg_pen.x==10000&&g_dg_pen.y==5000&&dg_dev[dg_of[7]-1].pen.pres==512);
    bp[1]=0; dg_report(7,bp,9,0); chk("Wacom pen protocol: out of proximity releases",btn==0);
    r=dg_parse(ts_rd,sizeof(ts_rd),&L);
    chk("touchscreen: 2 finger collections, contact count, report 1",r==2&&L.touch&&L.nf==2&&L.touch_rid==1&&L.cc.off==96&&L.f[1].x.off==64&&L.fxmax==4095);
    chk("touchscreen: input mode feature report 3, contact count max report 2",L.mode_rid==3&&L.mode.ok&&L.didx.ok&&L.ccmax_rid==2);
    chk("touchscreen probe: Set Feature input mode = 2 (multi-input), device index 0",dg_probe(2,0x2575,1,ts_rd,sizeof(ts_rd),feat,&fl,&fr)==14&&fl==3&&feat[0]==3&&feat[1]==2&&feat[2]==0&&fr==3);
    dg_bind(2,9); dgdev* D=&dg_dev[dg_of[9]-1];
    { int t[2]={1,1}, id[2]={4,7}, x[2]={100,200}, y[2]={100,200}; int t2[1]={1}, id2[1]={9}, x2[1]={300}, y2[1]={300};
      u32 f0=g_dg_frames; ts_rep(9,3,2,t,id,x,y,0); chk("hybrid mode: first report of 3 contacts holds the frame",g_dg_frames==f0);
      ts_rep(9,0,1,t2,id2,x2,y2,0); chk("hybrid mode: second report completes a 3-contact frame",g_dg_frames==f0+1&&D->nprev==3&&D->prev[2].id==9);
      ne=dg_ev_take(ev,64); chk("evdev MT: 3 slots with tracking IDs 4,7,9 and positions, BTN_TOUCH",evhas(ev,ne,3,0x39,4)&&evhas(ev,ne,3,0x39,7)&&evhas(ev,ne,3,0x39,9)&&evhas(ev,ne,3,0x35,300)&&evhas(ev,ne,1,0x14a,1));
      ts_rep(9,0,0,t,id,x,y,0); ne=dg_ev_take(ev,64); chk("cc=0 with no frame pending: all lifted, tracking IDs -1",D->nprev==0&&evhas(ev,ne,3,0x39,-1)&&evhas(ev,ne,1,0x14a,0)); }
    { int t[2]={1,0}, id[2]={1,0}, x[2]={2000,0}, y[2]={2000,0}; presses=0; absn=0; btn=0;
      ts_rep(9,1,2,t,id,x,y,1000); x[0]=2010; ts_rep(9,1,2,t,id,x,y,1100); ts_rep(9,0,0,t,id,x,y,1150);
      chk("tap: pointer moves to finger, one left click on lift",absn>=1&&presses==1&&btn==0);
      presses=0; x[0]=1000; ts_rep(9,1,2,t,id,x,y,2000); x[0]=1500; ts_rep(9,1,2,t,id,x,y,2050); chk("drag: left button held while moving",presses==1&&(btn&1));
      ts_rep(9,0,0,t,id,x,y,2100); chk("drag: released on lift",btn==0);
      rpresses=0; presses=0; ts_rep(9,1,2,t,id,x,y,3000); ts_rep(9,1,2,t,id,x,y,3800); ts_rep(9,0,0,t,id,x,y,3900); chk("long press: right click, no left click",rpresses==1&&presses==0&&btn==0); }
    { int t[2]={1,1}, id[2]={1,2}, x[2]={1000,1400}, y[2]={1000,1000}; scv=sch=0; gest=0;
      ts_rep(9,2,2,t,id,x,y,4000); for(int s=1;s<=4;s++){ y[0]=y[1]=1000+s*200; ts_rep(9,2,2,t,id,x,y,4000+s*10); }
      chk("two-finger drag down: scroll up by notches (content follows fingers), no click",scv==4&&sch==0&&gest==0&&btn==0);
      x[0]=900; x[1]=1500; ts_rep(9,2,2,t,id,x,y,4100); chk("pinch out: zoom in gesture",gest==GES_ZOOM_IN);
      x[0]=1150; x[1]=1250; ts_rep(9,2,2,t,id,x,y,4200); chk("pinch in: zoom out gesture",gest==GES_ZOOM_OUT);
      presses=0; ts_rep(9,0,0,t,id,x,y,4300); chk("two-finger lift: no click",presses==0&&btn==0); }
    { int a=dg_curve(2048,4095,0), b=dg_curve(2048,4095,2), c=dg_curve(2048,4095,4), mono=1, pv=-1;
      for(int p=0;p<=4095;p+=7){ int v=dg_curve(p,4095,1); if(v<pv)mono=0; pv=v; }
      chk("pressure curve: soft > linear > firm at half, endpoints 0/65535, monotonic",a>b&&b>c&&dg_curve(0,4095,0)==0&&dg_curve(4095,4095,4)==65535&&mono); }
    { int ox, oy; g_dg_map=DGM_FULL; g_dg_lefty=1; dg_map_xy(0,0,100,0,0,100,&ox,&oy); chk("left-handed: rotated 180",ox==65535&&oy==65535); g_dg_lefty=0;
      g_dg_map=DGM_RIGHT; dg_map_xy(0,0,100,50,0,100,&ox,&oy); chk("map to right screen half",ox==32768);
      g_dg_map=DGM_ASPECT; g_dg_W=1600; g_dg_H=1600; dg_map_xy(100,0,200,50,0,100,&ox,&oy); chk("keep proportions: 2:1 tablet on square screen uses the left square",ox==65535&&oy>=32760&&oy<=32770); g_dg_map=DGM_FULL; }
    { srand(7); u8 d[256], rp[64]; int crash=0; for(int it=0;it<100000;it++){ int n=rand()%256; for(int i=0;i<n;i++){ int k=rand()%8; d[i]=(u8)(k==0?0x05:k==1?0x0D:k==2?0x09:k==3?0xA1:k==4?0x81:k==5?0xC0:rand()); }
        int l=dg_probe(3,(u16)(rand()&1?0x056A:0x1111),(u16)(0x0300+rand()%0x100),d,n,feat,&fl,&fr); if(fl<0||fl>64)crash=1; dg_bind(3,10);
        for(int q=0;q<4&&l>0;q++){ int m=1+rand()%64; for(int i=0;i<m;i++)rp[i]=(u8)rand(); dg_report(10,rp,m,(u32)q); } dg_ev_take(ev,64); for(int s=0;s<DG_DEV;s++) if(s!=dg_of[5]-1&&s!=dg_of[7]-1&&s!=dg_of[9]-1)dg_dev[s].used=0; }
      chk("fuzz: 100000 random descriptors x 4 random reports",!crash); }
    printf("digitizer: %d ok, %d failed\n",npass,nfail); if(!nfail)printf("digitizer: ALL PASS\n"); return nfail?1:0; }
