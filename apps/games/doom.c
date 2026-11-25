#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/apps/games/doom.h"
#include "include/nvx/arch/x86_64/user/doom_jmp.h"
extern unsigned int* DG_ScreenBuffer;
extern void doomgeneric_Create(int argc, char** argv);
extern void doomgeneric_Tick(void);
extern void dsh_set_output(void(*)(const char*,int));
extern void dsh_set_file(const char*, unsigned char*, long);
extern void dsh_set_heap(void*);
extern void dsh_set_exit(void(*)(int));
static void* g_doom_heap;
#define DOOM_W 640
#define DOOM_H 400
#define DOOM_HEAP_MB 24
#define DOOM_RUN_MB 8
#define DOOM_MAX_RUNS (DOOM_HEAP_MB/DOOM_RUN_MB)
u8* g_doom_wad;
#define DOOM_WAD_CAP (32*1024*1024)
int g_doomx=0, g_doomy=0, g_doom_state=0;
int g_doom_dw=DOOM_W, g_doom_dh=DOOM_H;
static int doom_runs=0;
static long doom_wadlen=0; const char* g_doom_src=0;
static const char* doom_regname=0; static long doom_reglen=0;
static char doom_msg[96]="";
static doom_jmpbuf doom_jb;
static void doom_out(const char* s,int n){ for(int i=0;i<n;i++)term_putc(s[i]); }
static void doom_setmsg(const char* s){ int i=0; for(;s[i]&&i<(int)sizeof(doom_msg)-1;i++)doom_msg[i]=s[i]; doom_msg[i]=0; }
int doom_can_restart(void){ return doom_runs<DOOM_MAX_RUNS; }
static void doom_exit_cb(int code){
    g_doom_mcap=0;
    if(g_doom_state==2){ g_doom_state=code==0?4:5; if(code!=0)doom_setmsg("DOOM stopped with an error. See the log in Terminal."); }
    else if(doom_runs>1){ g_doom_state=5; doom_setmsg("DOOM could not start again."); doom_runs=DOOM_MAX_RUNS; }
    else g_doom_state=3;
    doom_longjmp(doom_jb,1); }
static u8 doom_kstate[128]; static u64 doom_klast=0;
static int sc_to_doomkey(int sc){ switch(sc){
    case 0x48: case 0x11: return 0xad;
    case 0x50: case 0x1F: return 0xaf;
    case 0x4B: return 0xac;
    case 0x4D: return 0xae;
    case 0x1E: return 0xa0;
    case 0x20: return 0xa1;
    case 0x12: return 0xa2;
    case 0x1D: return 0xa3;
    case 0x39: return 0xa2;
    case 0x2A: case 0x36: return 0xb6;
    case 0x38: return 0xb8;
    case 0x1C: return 13;
    case 0x01: return 27;
    case 0x0F: return 9;
    case 0x0E: return 0x7f;
    case 0x33: return 0xa0;
    case 0x34: return 0xa1;
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3'; case 0x05: return '4';
    case 0x06: return '5'; case 0x07: return '6'; case 0x08: return '7';
    case 0x15: return 'y'; case 0x31: return 'n'; case 0x21: return 'f';
} return 0; }
int g_doom_mcap=0;
int doom_mdx=0, doom_mdy=0, doom_mbtn=0, doom_mbtn_prev=0;
static int doom_mswallow=0;
int DG_GetMouse(int* dx,int* dy,int* buttons){
    if(!g_doom_mcap){ *dx=0;*dy=0;*buttons=0; doom_mdx=doom_mdy=0; doom_mbtn_prev=0; doom_mswallow=0; return 0; }
    int btn=doom_mbtn; if(doom_mswallow){ if(btn)btn=0; else doom_mswallow=0; }
    int any=(doom_mdx||doom_mdy||btn!=doom_mbtn_prev);
    *dx=doom_mdx; *dy=doom_mdy; *buttons=btn; doom_mdx=0; doom_mdy=0; doom_mbtn_prev=btn;
    return any;
}
void DG_Init(void){}
void DG_SetWindowTitle(const char* t){ (void)t; }
unsigned int DG_GetTicksMs(void){ return (unsigned int)(g_ticks*10ULL); }
void DG_SleepMs(unsigned int ms){ u64 tgt=g_ticks+(ms/10)+1; while(g_ticks<tgt) cpu_halt(); }
static int doom_focused(void){ return WN[W_DOOM].open && !WN[W_DOOM].min && focus==W_DOOM; }
static void doom_keys_drain(void){ int d; while(gkey_pop(0,&d)>=0){} }
static int doom_key_release_pending(int* pressed,unsigned char* key){
    for(int sc=1;sc<128;sc++) if(doom_kstate[sc]){ doom_kstate[sc]=0; int dk=sc_to_doomkey(sc); if(dk){ *pressed=0; *key=(unsigned char)dk; return 1; } }
    return 0; }
int DG_GetKey(int* pressed, unsigned char* key){
    u64 now=g_ticks; int stale=now-doom_klast>50; doom_klast=now;
    if(!doom_focused()||stale){ doom_keys_drain(); return doom_key_release_pending(pressed,key); }
    int down, sc;
    while((sc=gkey_pop(0,&down))>=0){
        if(sc<=0||sc>=128)continue; u8 st=down?1:0; if(doom_kstate[sc]==st)continue; doom_kstate[sc]=st;
        int dk=sc_to_doomkey(sc); if(dk){ *pressed=st; *key=(unsigned char)dk; return 1; } }
    return 0; }
void doom_fit(int cw,int ch,int* dw,int* dh){
    int w=cw, h=cw*DOOM_H/DOOM_W; if(h>ch){ h=ch; w=ch*DOOM_W/DOOM_H; }
    if(w<16)w=16; if(h<10)h=10;
    if(w>DOOM_W-8&&w<DOOM_W+8&&h>DOOM_H-8&&h<DOOM_H+8){ w=DOOM_W; h=DOOM_H; }
    *dw=w; *dh=h; }
void DG_DrawFrame(void){ if(!DG_ScreenBuffer) return;
    int dw=g_doom_dw, dh=g_doom_dh;
    if(dw==DOOM_W&&dh==DOOM_H){
        for(int y=0;y<DOOM_H;y++){ int dy=g_doomy+y; if((u32)dy>=H)continue; u32* d=&SCENE[(u64)dy*SW+g_doomx]; unsigned int* s=&DG_ScreenBuffer[y*DOOM_W];
            int xm=DOOM_W; if(g_doomx+xm>(int)W)xm=(int)W-g_doomx; for(int x=0;x<xm;x++)d[x]=s[x]; }
        return; }
    for(int y=0;y<dh;y++){ int dy=g_doomy+y; if((u32)dy>=H)continue; u32* d=&SCENE[(u64)dy*SW+g_doomx]; unsigned int* s=&DG_ScreenBuffer[(y*DOOM_H/dh)*DOOM_W];
        for(int x=0;x<dw;x++){ int dx=g_doomx+x; if((u32)dx>=W)break; d[x]=s[x*DOOM_W/dw]; } } }
static void doom_init(void){
    dsh_set_output(doom_out); dsh_set_exit(doom_exit_cb);
    if(!g_doom_heap){ g_doom_heap=kmalloc((unsigned long)DOOM_HEAP_MB*1024u*1024u); if(!g_doom_heap){ g_doom_state=3; return; } dsh_set_heap(g_doom_heap); }
    extern u8 _binary_doom1_wad_xz_start[], _binary_doom1_wad_xz_end[];
    u32 modaddr=0, modsize=0;
#ifndef DOOM_HOST
    modaddr=*(volatile u32*)0x5010ULL; modsize=*(volatile u32*)0x5018ULL;
#endif
    static const char* cand[5]={"DOOM2.WAD","DOOM.WAD","FREEDOOM.WAD","FREEDOO1.WAD","DOOM1.WAD"};
    static const char* inm[5]={"doom2.wad","doom.wad","freedoom2.wad","freedoom1.wad","doom1.wad"};
    const char* nm="doom1.wad"; doom_wadlen=0; g_doom_src=0;
    if(!g_doom_wad) g_doom_wad=(u8*)kmalloc(DOOM_WAD_CAP);
    if(g_doom_wad) for(int i=0;i<4;i++){ int n=fat_read_file(cand[i],g_doom_wad,DOOM_WAD_CAP); if(n>1000000){ doom_wadlen=n; nm=inm[i]; g_doom_src=cand[i]; break; } }
    if(doom_wadlen<=0 && modaddr && modsize>1000000u){ if(g_doom_wad)kfree(g_doom_wad); g_doom_wad=(u8*)(u64)modaddr; doom_wadlen=(long)modsize; g_doom_src="boot disk"; }
    if(doom_wadlen<=0 && g_doom_wad){ extern long xzk_decode_into(const u8*,long,u8*,long); long el=xzk_decode_into(_binary_doom1_wad_xz_start,(long)(_binary_doom1_wad_xz_end-_binary_doom1_wad_xz_start),g_doom_wad,DOOM_WAD_CAP); if(el>1000000){ doom_wadlen=el; g_doom_src="built in"; } }
    if(doom_wadlen<=0){ g_doom_state=3; return; }
    if(doom_regname!=nm||doom_reglen!=doom_wadlen){ dsh_set_file(nm,g_doom_wad,doom_wadlen); doom_regname=nm; doom_reglen=doom_wadlen; }
    static char* argv[]={(char*)"doom",(char*)"-iwad",(char*)"doom1.wad",0}; argv[2]=(char*)nm;
    doom_runs++;
    doomgeneric_Create(3,argv);
    g_doom_state=2;
}
static void doom_restart(void){
    if(!doom_can_restart())return;
    doom_keys_drain(); for(int i=0;i<128;i++)doom_kstate[i]=0; doom_klast=g_ticks;
    doom_mdx=doom_mdy=0; doom_mbtn_prev=0; doom_mswallow=0; g_doom_mcap=0;
    g_doom_state=0; }
void doom_click(void){
    if(g_doom_state==2){ if(!g_doom_mcap){ g_doom_mcap=1; doom_mswallow=1; } }
    else if(g_doom_state>=4) doom_restart(); }
static int doom_restart_key(void){
    if(!doom_focused()){ doom_keys_drain(); return 0; }
    int down, sc, hit=0; while((sc=gkey_pop(0,&down))>=0){ if(down&&(sc==0x1C||sc==0x39))hit=1; }
    return hit; }
void doom_draw(int win){
    volatile int i=win;
    win_frame(i,g_doom_mcap?"DOOM  -  Esc frees the mouse":"DOOM - WASD move, Ctrl fire, Space use, click aims");
    int cx=WN[i].x+2, cy=WN[i].y+TITLEH, cw=WN[i].w-4, ch=WN[i].h-TITLEH-2;
    int dw, dh; doom_fit(cw,ch,&dw,&dh); g_doom_dw=dw; g_doom_dh=dh; g_doomx=cx+(cw-dw)/2; g_doomy=cy+(ch-dh)/2;
    if(g_doomx>cx||g_doomy>cy) fill_rect(cx,cy,cw,ch,rgb(0,0,0));
    if(g_doom_state==3){ fill_rect(cx,cy,cw,ch,rgb(12,10,14));
        dstr_ctr(cx+cw/2, cy+ch/2-50, "Could not load the WAD.", rgb(230,120,110), 2);
        dstr_ctr(cx+cw/2, cy+ch/2-14, "The built-in DOOM1.WAD is missing. Put DOOM1.WAD or DOOM2.WAD on a FAT disk.", rgb(200,200,210), 1);
        dstr_ctr(cx+cw/2, cy+ch/2+6, "Check the log in Terminal for details.", rgb(150,155,170), 1); return; }
    if(g_doom_state>=4){ fill_rect(cx,cy,cw,ch,rgb(14,10,12));
        dstr_ctr(cx+cw/2, cy+ch/2-40, g_doom_state==4?"DOOM has quit.":"DOOM stopped.", rgb(230,170,110), 2);
        if(g_doom_state==5) dstr_ctr(cx+cw/2, cy+ch/2-4, doom_msg, rgb(200,200,210), 1);
        dstr_ctr(cx+cw/2, cy+ch/2+16, doom_can_restart()?"Press Enter or click here to play again.":"Restart NoovexOS to play again.", rgb(150,155,170), 1);
        if(doom_restart_key()) doom_restart();
        return; }
    if(doom_setjmp(doom_jb)==0){
        if(g_doom_state==0){ fill_rect(cx,cy,cw,ch,rgb(10,10,14)); dstr_ctr(cx+cw/2,cy+ch/2-10,"Loading DOOM...",rgb(220,60,40),2); doom_init(); }
        if(g_doom_state==2){ doomgeneric_Tick(); } }
    else { if(g_doom_state==2){ g_doom_state=5; doom_setmsg("DOOM stopped unexpectedly."); } g_doom_mcap=0; }
}
