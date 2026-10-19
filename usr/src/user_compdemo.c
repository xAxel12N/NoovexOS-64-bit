#include "user.h"
#define WM_CREATE 1
struct wmsg { int type,w,h,key,client,x,y,btn; };
static void app(char id){
    int ww=180, hh=110, key=100+(id-'0');
    int shmid=sys_shm(key, ww*hh*4);
    unsigned* buf=(unsigned*)sys_shmmap(shmid);
    for(int y=0;y<hh;y++)for(int x=0;x<ww;x++){ unsigned r=(unsigned)(x*255/ww),g=(unsigned)(y*255/hh);
        buf[y*ww+x]=(id=='1')?((r<<16)|(g<<8)|150u):((150u<<16)|(r<<8)|(unsigned)g); }
    for(int y=30;y<70;y++)for(int x=40;x<140;x++)buf[y*ww+x]=(id=='1')?0xF0F0FFu:0x203040u;
    struct wmsg m; m.type=WM_CREATE; m.w=ww; m.h=hh; m.key=key; m.client=id-'0';
    m.x=(id=='1')?60:360; m.y=(id=='1')?140:240; m.btn=0;
    sys_msgsend(0,&m,sizeof(m));
    sys_write(id=='1'?"[app1] window created, drew into shared memory\n":"[app2] window created, drew into shared memory\n");
    unsigned long start=ticks(); while(ticks()-start < 500){ for(volatile long d=0;d<2000000;d++){ } }
}
static void compositor(void){
    struct { int key,x,y,w,h; } win[6]; int nwin=0;
    sys_write("[compositor] userland compositor running - reading windows, compositing\n");
    unsigned long start=ticks();
    while(ticks()-start < 500){
        struct wmsg m;
        while(sys_wm_poll(&m)>0){ if(m.type==WM_CREATE && nwin<6){ win[nwin].key=m.key; win[nwin].x=m.x; win[nwin].y=m.y; win[nwin].w=m.w; win[nwin].h=m.h; nwin++; } }
        sys_scr_clear(0x141c28u);
        sys_scr_text(40,20,"NoovexOS userland compositor (nova) -- rung 5",0xC0D0F0u);
        sys_scr_text(40,40,"windows below are separate processes drawing via shared memory",0x8090B0u);
        for(int i=0;i<nwin;i++){
            int wx=win[i].x, wy=win[i].y, w=win[i].w, h=win[i].h;
            sys_scr_rect(wx-2,wy-2,w+4,h+22+4,0xFFFFFFu);
            sys_scr_rect(wx,wy,w,20,0x2860C8u);
            sys_scr_text(wx+10,wy+6,"App window",0xFFFFFFu);
            sys_scr_blit(win[i].key, wx, wy+22, w, h);
        }
        sys_scr_present();
        for(volatile long d=0;d<1200000;d++){ }
    }
}
int main(void){
    char id=*(volatile char*)0x40000050000ULL;
    if(id=='C') compositor(); else app(id);
    return 0;
}
