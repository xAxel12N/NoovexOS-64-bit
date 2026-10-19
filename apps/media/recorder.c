#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"

extern int mx, my; extern void toast(const char*);
#define RC_RATE 48000
#define RC_BLK 1024
static short* rc_buf=0; static u32 rc_cap=0, rc_len=0, rc_play=0; static u8* rc_pk=0; static u32 rc_npk=0, rc_pkcap=0; static int rc_pkacc=0, rc_pkn=0;
static int rc_rec=0, rc_playing=0, rc_saved=0, rc_n=0; static char rc_last[40]=""; static u64 rc_draw_ns=0;
static int rbx[10], rby[10], rbw[10], rbh[10], rid[10], rn=0;
static int rc_gx=0, rc_gw=1;
static void rhit(int x,int y,int w,int h,int id){ if(rn<10){ rbx[rn]=x; rby[rn]=y; rbw[rn]=w; rbh[rn]=h; rid[rn]=id; rn++; } }
static int rc_alloc(void){
    if(rc_buf)return 1; u32 secs[3]={600,180,60};
    for(int k=0;k<3;k++){ u32 n=secs[k]*RC_RATE; rc_buf=(short*)kmalloc((unsigned long)n*2u); if(rc_buf){ rc_cap=n; break; } }
    if(!rc_buf)return 0; rc_pkcap=rc_cap/RC_BLK+4; rc_pk=(u8*)kmalloc(rc_pkcap); if(!rc_pk){ kfree(rc_buf); rc_buf=0; return 0; } return 1; }
static void rc_pump(void){
    if(!g_cap_on)return; u8 t[8192];
    for(int g=0;g<32;g++){ int n=snd_cap_read(t,(int)sizeof(t)); if(n<=0)break; if(!rc_rec)continue;
        for(int i=0;i+3<n;i+=4){ if(rc_len>=rc_cap){ rc_rec=0; toast("Recording is full"); break; }
            int a=(short)(t[i]|(t[i+1]<<8)), b=(short)(t[i+2]|(t[i+3]<<8)); int m=(a+b)/2; rc_buf[rc_len++]=(short)m;
            int am=m<0?-m:m; if(am>rc_pkacc)rc_pkacc=am; if(++rc_pkn>=RC_BLK){ if(rc_npk<rc_pkcap)rc_pk[rc_npk++]=(u8)(rc_pkacc*255/32768); rc_pkacc=0; rc_pkn=0; } } } }
static void rc_feed(void){
    if(!rc_playing)return; if(!snd_init()){ rc_playing=0; return; } g_snd.start(); u8 t[4096];
    for(int g=0;g<16&&rc_play<rc_len;g++){ int fr=g_snd.free_bytes()/4; if(fr<=0)break; if(fr>1024)fr=1024; u32 left=rc_len-rc_play; if((u32)fr>left)fr=(int)left;
        for(int i=0;i<fr;i++){ short s=rc_buf[rc_play+(u32)i]; t[i*4]=(u8)s; t[i*4+1]=(u8)(s>>8); t[i*4+2]=(u8)s; t[i*4+3]=(u8)(s>>8); }
        int w=g_snd.write(t,fr*4); if(w<=0)break; rc_play+=(u32)(w/4); }
    if(rc_play>=rc_len)rc_playing=0; }
int recorder_tick(void){
    if(!WN[W_RECORDER].open)return 0;
    rc_pump(); rc_feed();
    u64 now=hr_ns(); if((g_cap_on||rc_playing)&&now-rc_draw_ns>=40000000ULL){ rc_draw_ns=now; return 1; } return 0; }
static void rc_save(void){
    if(!rc_len){ toast("Nothing recorded yet"); return; }
    u32 db=rc_len*2u; u8* o=(u8*)kmalloc(44+db); if(!o){ toast("Not enough memory to save"); return; }
    u8 h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    u32 rs=36+db, br=RC_RATE*2u; h[4]=(u8)rs; h[5]=(u8)(rs>>8); h[6]=(u8)(rs>>16); h[7]=(u8)(rs>>24);
    h[24]=(u8)RC_RATE; h[25]=(u8)(RC_RATE>>8); h[26]=(u8)(RC_RATE>>16); h[27]=0; h[28]=(u8)br; h[29]=(u8)(br>>8); h[30]=(u8)(br>>16); h[31]=(u8)(br>>24);
    h[40]=(u8)db; h[41]=(u8)(db>>8); h[42]=(u8)(db>>16); h[43]=(u8)(db>>24);
    for(int i=0;i<44;i++)o[i]=h[i]; for(u32 i=0;i<rc_len;i++){ o[44+i*2]=(u8)rc_buf[i]; o[45+i*2]=(u8)(rc_buf[i]>>8); }
    char nm[40],*q=nm; q=scopy(q,"recording-"); q=udec(q,(u32)++rc_n); q=scopy(q,".wav"); *q=0;
    int r=fs_save_bin(nm,o,(int)(44+db),0); kfree(o);
    if(r>=0){ rc_saved=1; int i=0; for(;nm[i]&&i<39;i++)rc_last[i]=nm[i]; rc_last[i]=0; }
    char t[64]; q=t; q=scopy(q,r>=0?"Saved ":"Could not save "); q=scopy(q,nm); *q=0; toast(t); }
static void rc_start_rec(void){
    if(!snd_rec_avail()){ toast("No microphone found"); return; }
    if(!rc_alloc()){ toast("Not enough memory to record"); return; }
    if(!snd_rec_start()){ toast("The microphone did not start"); return; }
    rc_playing=0; rc_len=0; rc_npk=0; rc_pkacc=0; rc_pkn=0; rc_saved=0; rc_rec=1; }
static void rc_stop_rec(void){ if(!rc_rec)return; rc_rec=0; rc_save(); }
static char* rc_time(char* p,u32 samples){ u32 s=samples/RC_RATE; u32 m=s/60; s%=60; p=udec(p,m); *p++=':'; if(s<10)*p++='0'; p=udec(p,s); *p++='.'; p=udec(p,(samples%RC_RATE)/(RC_RATE/10)); *p=0; return p; }
static void rc_button(int x,int y,int w,int h,const char* t,u32 bg,u32 fg,int id){ int hov=mx>=x&&mx<x+w&&my>=y&&my<y+h; fill_round(x,y,w,h,h/2,15,hov?mix(bg,rgb(0,0,0),20):bg); dstr_ctr(x+w/2,y+h/2-6,t,fg,1); rhit(x,y,w,h,id); }
void recorder_draw(int i){
    win_frame(i,"Voice Recorder"); rn=0;
    if(snd_rec_avail()&&!g_cap_on)snd_rec_start();
    int wx=WN[i].x+1, wy=WN[i].y+TITLEH, ww=WN[i].w-2, wh=WN[i].h-TITLEH-1;
    fill_rect(wx,wy,ww,wh,rgb(243,244,248));
    char b[80],*p;
    int cx=wx+ww/2;
    p=b; p=rc_time(p,rc_rec?rc_len:rc_playing?rc_play:rc_len); dstr_ctr(cx,wy+22,b,rgb(26,30,42),3);
    dstr_ctr(cx,wy+70,!snd_rec_avail()?"No microphone found":rc_rec?"Recording...":rc_playing?"Playing":rc_len?"Ready - press Play to listen":"Press the red button to record",rc_rec?rgb(210,50,50):rgb(110,118,138),1);
    int gx=wx+24, gy=wy+96, gw=ww-48, gh=120; fill_round(gx,gy,gw,gh,10,15,rgb(255,255,255)); fill_rect(gx+10,gy+gh/2,gw-20,1,rgb(230,232,238));
    rc_gx=gx+10; rc_gw=gw-20;
    if(rc_npk){ u32 n=rc_npk; int cols=gw-20; for(int c=0;c<cols;c++){ u32 k0, k1; if(n<=(u32)cols){ k0=(u32)c*n/(u32)cols; k1=k0+1; } else { u32 start=rc_rec&&n>(u32)cols*4u?n-(u32)cols*4u:0; u32 span=n-start; k0=start+(u32)c*span/(u32)cols; k1=start+(u32)(c+1)*span/(u32)cols; }
            if(k0>=n)continue; if(k1>n)k1=n; if(k1<=k0)k1=k0+1; int pk=0; for(u32 k=k0;k<k1&&k<n;k++) if(rc_pk[k]>pk)pk=rc_pk[k];
            int hh=pk*(gh/2-8)/255; if(hh<1)hh=1; u32 col=rc_rec?rgb(220,70,70):ACC;
            if(rc_playing&&rc_len){ u32 at=(u32)((u64)rc_play*(u32)cols/rc_len); if((u32)c>at)col=rgb(190,198,214); }
            fill_rect(gx+10+c,gy+gh/2-hh,1,hh*2,col); } }
    else dstr_ctr(cx,gy+gh/2-6,"The recording will appear here",rgb(170,176,190),1);
    if(!rc_rec&&rc_len)rhit(gx,gy,gw,gh,9);
    int by=gy+gh+24, bs=64;
    if(rc_rec){ fill_circle(cx,by+bs/2,bs/2,rgb(220,60,60)); fill_rect(cx-11,by+bs/2-11,22,22,rgb(255,255,255)); }
    else { fill_circle(cx,by+bs/2,bs/2,snd_rec_avail()?rgb(220,60,60):rgb(200,204,214)); fill_circle(cx,by+bs/2,bs/2-6,rgb(255,255,255)); fill_circle(cx,by+bs/2,bs/2-12,snd_rec_avail()?rgb(220,60,60):rgb(200,204,214)); }
    rhit(cx-bs/2,by,bs,bs,1);
    rc_button(cx-bs/2-140,by+16,110,32,rc_playing?"Pause":"Play",rc_len&&!rc_rec?ACC:rgb(214,218,228),rgb(255,255,255),2);
    rc_button(cx+bs/2+30,by+16,110,32,"Save again",rc_len&&!rc_rec?rgb(226,230,240):rgb(236,238,244),rgb(30,34,46),3);
    int my2=by+bs+28; fill_round(wx+24,my2,ww-48,96,10,15,rgb(255,255,255));
    dstr(wx+40,my2+14,"Microphone",rgb(26,30,42),1); dstr(wx+140,my2+14,snd_rec_name(),rgb(110,118,138),1);
    int lw=ww-48-200, lx=wx+180; dstr(wx+40,my2+40,"Input level",rgb(110,118,138),1);
    fill_round(lx,my2+42,lw,10,5,15,rgb(230,232,238)); { int lv=g_cap_peak; if(lv>1000)lv=1000; int fw=lv*lw/1000; if(fw>4)fill_round(lx,my2+42,fw,10,5,15,lv>850?rgb(220,70,60):lv>500?rgb(230,170,40):rgb(46,170,90)); }
    dstr(wx+40,my2+66,"Mic volume",rgb(110,118,138),1);
    { int fw=g_mic_gain*lw/100; fill_round(lx,my2+70,lw,8,4,15,rgb(226,229,236)); if(fw>0)fill_round(lx,my2+70,fw,8,4,15,ACC); fill_circle(lx+fw,my2+74,9,rgb(255,255,255)); fill_circle(lx+fw,my2+74,4,ACC); rhit(lx-10,my2+60,lw+20,28,4); rc_gx=lx; rc_gw=lw; }
    p=b; p=udec(p,(u32)g_mic_gain); *p++='%'; *p=0; dstr(lx+lw+14,my2+66,b,rgb(70,78,98),1);
    if(rc_saved&&rc_last[0]){ p=b; p=scopy(p,"Saved as "); p=scopy(p,rc_last); p=scopy(p," in Files"); *p=0; dstr_ctr(cx,my2+112,b,rgb(110,118,138),1); }
    if(g_cap_over){ p=b; p=scopy(p,"(dropped "); p=udec(p,g_cap_over/4); p=scopy(p," samples - the system was busy)"); *p=0; dstr_ctr(cx,my2+130,b,rgb(200,120,60),1); } }
void recorder_click(int px,int py){
    for(int k=rn-1;k>=0;k--) if(px>=rbx[k]&&px<rbx[k]+rbw[k]&&py>=rby[k]&&py<rby[k]+rbh[k]){ int id=rid[k];
        if(id==1){ if(rc_rec)rc_stop_rec(); else rc_start_rec(); }
        else if(id==2){ if(rc_rec||!rc_len)return; if(rc_playing)rc_playing=0; else { if(rc_play>=rc_len)rc_play=0; rc_playing=1; } }
        else if(id==3){ if(!rc_rec&&rc_len)rc_save(); }
        else if(id==4){ int pc=(px-rc_gx)*100/(rc_gw>0?rc_gw:1); snd_rec_gain(pc); }
        else if(id==9){ int x0=rbx[k]+10, w0=rbw[k]-20; int pc=(px-x0)*1000/(w0>0?w0:1); if(pc<0)pc=0; if(pc>1000)pc=1000; rc_play=(u32)((u64)rc_len*(u32)pc/1000u); rc_playing=1; }
        return; } }
void recorder_key(int c){ if(c==' '){ if(rc_rec)rc_stop_rec(); else rc_start_rec(); } else if(c=='p'||c=='P'){ if(!rc_rec&&rc_len){ if(rc_playing)rc_playing=0; else { if(rc_play>=rc_len)rc_play=0; rc_playing=1; } } } }
void recorder_closed(void){ if(rc_rec)rc_stop_rec(); rc_playing=0; snd_rec_stop(); }
