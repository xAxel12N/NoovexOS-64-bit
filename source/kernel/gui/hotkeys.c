#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/video/gfx.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/core/sound.h"

extern volatile u8 g_hk_q[32]; extern volatile int g_hk_w, g_hk_r;
extern char toast_msg[80]; extern u64 toast_t; extern volatile u64 g_ticks;
extern int g_gfx, g_gfx_fx, g_airplane, g_lap_tp_off;
extern void bl_step(int dir); extern void snd_apply_volume(int v);
int g_osd_kind=0, g_osd_val=0; static char g_osd_txt[40]; static int hk_prev_vol=60, hk_prev_mic=70, hk_micmute=0;
void osd_show(int kind,int val,const char* txt){
    g_osd_kind=kind; g_osd_val=val; int i=0; for(;txt&&txt[i]&&i<39;i++)g_osd_txt[i]=txt[i]; g_osd_txt[i]=0;
    toast_msg[0]=0; toast_t=g_ticks; }
void osd_clear(void){ g_osd_kind=0; }
static void ic_speaker(int x,int y,u32 c,int muted,int lvl){
    fill_rect(x,y+8,6,10,c); for(int k=0;k<10;k++)fill_rect(x+6+k/2,y+8-k/2,1,10+k,c);
    if(muted){ for(int k=0;k<9;k++){ fill_rect(x+15+k,y+9+k,2,2,c); fill_rect(x+23-k,y+9+k,2,2,c); } return; }
    for(int a=0;a<3&&a<lvl;a++){ int r=6+a*5; for(int t=-r;t<=r;t++){ int yy=t, xx=0; while((xx+1)*(xx+1)+yy*yy<=r*r)xx++; if(xx>2)fill_rect(x+12+xx,y+13+yy,2,1,c); } } }
static void ic_sun(int x,int y,u32 c){ fill_circle(x+13,y+13,6,c); for(int k=0;k<8;k++){ static const int dx[8]={0,8,11,8,0,-8,-11,-8}, dy[8]={-11,-8,0,8,11,8,0,-8}; fill_circle(x+13+dx[k],y+13+dy[k],2,c); } }
static void ic_mic(int x,int y,u32 c,int off){ fill_round(x+9,y+2,9,14,4,15,c); fill_rect(x+6,y+12,2,5,c); fill_rect(x+19,y+12,2,5,c); fill_rect(x+6,y+17,15,2,c); fill_rect(x+12,y+19,3,4,c); fill_rect(x+8,y+23,11,2,c); if(off) for(int k=0;k<22;k++)fill_rect(x+3+k,y+2+k,2,2,rgb(240,90,80)); }
static void ic_plane(int x,int y,u32 c){ fill_rect(x+12,y+2,3,22,c); for(int k=0;k<10;k++)fill_rect(x+13-k,y+11+k/2,2*k+1,1,c); fill_rect(x+8,y+22,11,2,c); }
static void ic_cam(int x,int y,u32 c){ fill_round(x+2,y+7,22,15,3,15,c); fill_circle(x+13,y+14,4,rgb(30,34,46)); fill_round(x+8,y+4,9,4,1,15,c); }
static void ic_screen(int x,int y,u32 c){ fill_round(x+1,y+3,24,16,2,15,c); fill_rect(x+4,y+6,18,10,rgb(30,34,46)); fill_rect(x+11,y+19,4,4,c); fill_rect(x+7,y+23,12,2,c); }
static void ic_pad(int x,int y,u32 c,int off){ fill_round(x+1,y+3,24,18,3,15,c); fill_rect(x+4,y+6,18,12,rgb(30,34,46)); fill_rect(x+12,y+18,2,3,rgb(30,34,46)); if(off) for(int k=0;k<22;k++)fill_rect(x+2+k,y+1+k,2,2,rgb(240,90,80)); }
static void ic_play(int x,int y,u32 c){ for(int k=0;k<10;k++)fill_rect(x+6,y+3+k,k+1,1,c); for(int k=0;k<10;k++)fill_rect(x+6,y+13+k,10-k,1,c); fill_rect(x+18,y+4,3,18,c); fill_rect(x+23,y+4,3,18,c); }
int osd_draw(void){
    if(!g_osd_kind)return 0; if(!toast_t){ g_osd_kind=0; return 1; }
    u64 age=(u64)(g_ticks-toast_t); if(age>=200){ toast_t=0; g_osd_kind=0; return 1; }
    int a=255; if(age>160)a=255-(int)(age-160)*255/40;
    int bar=g_osd_val>=0; int w=bar?340:300, h=56, x=((int)W-w)/2, y=(int)H-150; u32 bg=rgb(28,32,44);
    if(g_gfx) gfx_glass(x,y,w,h,16,bg,a*236/255,g_gfx_fx?2:0); else fill_round(x,y,w,h,16,15,bg);
    u32 fg=mix(bg,rgb(240,244,252),a), ac=mix(bg,ACC,a), dim=mix(bg,rgb(90,98,118),a);
    int ix=x+18, iy=y+15; int k=g_osd_kind;
    if(k==1) ic_speaker(ix,iy,fg,g_osd_val==0,g_osd_val==0?0:g_osd_val<34?1:g_osd_val<67?2:3);
    else if(k==2) ic_sun(ix,iy,fg); else if(k==3) ic_mic(ix,iy,fg,g_osd_val==0); else if(k==4) ic_plane(ix,iy,fg);
    else if(k==5) ic_cam(ix,iy,fg); else if(k==6) ic_screen(ix,iy,fg); else if(k==7) ic_pad(ix,iy,fg,g_osd_val==0); else ic_play(ix,iy,fg);
    if(bar){ int bx=x+62, bw=w-130, by=y+h/2-3; fill_round(bx,by,bw,6,3,15,dim); int fw=g_osd_val*bw/100; if(fw>3)fill_round(bx,by,fw,6,3,15,ac);
        char t[8],*p=t; p=udec(p,(u32)g_osd_val); *p=0; dstr_ctr(x+w-34,y+h/2-6,t,fg,1); if(g_osd_txt[0]) dstr(bx,y+8,g_osd_txt,dim,1); }
    else dstr(x+62,y+h/2-6,g_osd_txt,fg,1);
    return 1; }
static void vol_set(int v,int show){ if(v<0)v=0; if(v>100)v=100; g_snd_vol=v; if(!g_snd_state)snd_init(); snd_apply_volume(v); if(show)osd_show(1,v,v?"Volume":"Muted"); }
static int bl_now(void){ extern int bl_cur_pct(void); return bl_cur_pct(); }
void hk_do(int id){
    if(id==1){ vol_set(g_snd_vol+(g_snd_vol<10?2:6),1); }
    else if(id==2){ vol_set(g_snd_vol-(g_snd_vol<=10?2:6),1); }
    else if(id==3){ if(g_snd_vol){ hk_prev_vol=g_snd_vol; vol_set(0,1); } else vol_set(hk_prev_vol?hk_prev_vol:50,1); }
    else if(id>=4&&id<=7){ if(WN[W_VIDEO].open){ extern void video_key(int); video_key(id==4||id==7?' ':id==5?18:17); osd_show(8,-1,id==4?"Play / pause":id==5?"Forward":id==6?"Back":"Stop"); } else osd_show(8,-1,"Nothing is playing"); }
    else if(id==8){ open_win(W_CALC); }
    else if(id==9||(id>=22&&id<=27)){ open_win(W_BROWSER); }
    else if(id==10){ open_win(W_BROWSER); }
    else if(id==11){ open_win(W_FILES); }
    else if(id==12){ extern void power_sleep(void); power_sleep(); }
    else if(id==13||id==14){ bl_step(id==13?1:-1); int b=bl_now(); osd_show(2,b<0?0:b,"Brightness"); }
    else if(id==15){ osd_show(6,-1,"PC screen only (one display)"); }
    else if(id==16){ if(WN[W_CAMERA].open)close_win(W_CAMERA); else { extern void camera_open(void); camera_open(); } osd_show(5,-1,WN[W_CAMERA].open?"Camera on":"Camera off"); }
    else if(id==17){ extern void snd_rec_gain(int); extern int g_mic_gain; if(!hk_micmute){ hk_prev_mic=g_mic_gain?g_mic_gain:70; snd_rec_gain(0); hk_micmute=1; osd_show(3,0,"Microphone muted"); } else { snd_rec_gain(hk_prev_mic); hk_micmute=0; osd_show(3,hk_prev_mic,"Microphone on"); } }
    else if(id==18){ g_airplane^=1; osd_show(4,-1,g_airplane?"Airplane mode on":"Airplane mode off"); }
    else if(id==19){ g_lap_tp_off^=1; osd_show(7,g_lap_tp_off?0:-1,g_lap_tp_off?"Touchpad off":"Touchpad on"); if(!g_lap_tp_off)g_osd_val=-1; }
    else if(id==20){ g_state=ST_LOGIN; extern char login_pass[]; extern int login_pl; login_pass[0]=0; login_pl=0; }
    else if(id==21){ open_win(W_VIDEO); }
}
int hk_poll(void){ int any=0; while(g_hk_r!=g_hk_w){ int id=g_hk_q[g_hk_r]; g_hk_r=(g_hk_r+1)&31; if(g_state==ST_DESKTOP||id<=3||id==13||id==14)hk_do(id); any=1; } return any; }
