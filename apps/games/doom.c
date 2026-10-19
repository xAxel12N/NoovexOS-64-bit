#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/apps/games/doom.h"
/* ============================ DOOM (doomgeneric + FreeDoom) ============================ */
extern unsigned int* DG_ScreenBuffer;
extern void doomgeneric_Create(int argc, char** argv);
extern void doomgeneric_Tick(void);
extern void dsh_set_output(void(*)(const char*,int));
extern void dsh_set_file(const char*, unsigned char*, long);
extern void dsh_set_heap(void*);
static void* g_doom_heap;
extern void dsh_set_exit(void(*)(int));
#define DOOM_W 640
#define DOOM_H 400
u8* g_doom_wad;
#define DOOM_WAD     (g_doom_wad?g_doom_wad:(g_doom_wad=(u8*)kmalloc(DOOM_WAD_CAP)))
#define DOOM_WAD_CAP (32*1024*1024)
int g_doomx=0, g_doomy=0, g_doom_state=0; /* 0 init 2 running 3 failed */
static long doom_wadlen=0; const char* g_doom_src=0;
/* minimal setjmp/longjmp so I_Error/exit can unwind back to the desktop */
typedef long djmp[8];
extern int doom_setjmp(djmp);
extern void doom_longjmp(djmp,int) __attribute__((noreturn));
static djmp doom_jb;
static void doom_out(const char* s,int n){ for(int i=0;i<n;i++)term_putc(s[i]); }   /* Doom log -> Terminal */
static void doom_exit_cb(int code){ (void)code; g_doom_state=3; doom_longjmp(doom_jb,1); }
static u8 doom_prevkd[128];
static int sc_to_doomkey(int sc){ switch(sc){
    case 0x48: case 0x11: return 0xad;   /* Up / W    -> UPARROW   */
    case 0x50: case 0x1F: return 0xaf;   /* Down / S  -> DOWNARROW */
    case 0x4B: return 0xac;              /* Left      -> LEFTARROW (turn) */
    case 0x4D: return 0xae;              /* Right     -> RIGHTARROW(turn) */
    case 0x1E: return 0xa0;              /* A -> strafe left  (WASD)     */
    case 0x20: return 0xa1;              /* D -> strafe right (WASD)     */
    case 0x12: return 0xa2;              /* E      -> USE    */
    case 0x1D: return 0xa3;              /* LCtrl  -> FIRE   */
    case 0x39: return 0xa2;              /* Space  -> USE    */
    case 0x2A: case 0x36: return 0xb6;   /* Shift  -> run    */
    case 0x38: return 0xb8;              /* Alt    -> strafe */
    case 0x1C: return 13;                /* Enter  */
    case 0x01: return 27;                /* Esc    */
    case 0x0F: return 9;                 /* Tab    */
    case 0x0E: return 0x7f;              /* Backspace */
    case 0x33: return 0xa0;              /* , -> strafe left  */
    case 0x34: return 0xa1;              /* . -> strafe right */
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3'; case 0x05: return '4';
    case 0x06: return '5'; case 0x07: return '6'; case 0x08: return '7';
    case 0x15: return 'y'; case 0x31: return 'n'; case 0x21: return 'f';
} return 0; }
/* --- platform hooks the Doom blob calls --- */
int g_doom_mcap=0;                       /* mouse captured by DOOM (ESC releases) */
int doom_mdx=0, doom_mdy=0, doom_mbtn=0, doom_mbtn_prev=0;
int DG_GetMouse(int* dx,int* dy,int* buttons){
    if(!g_doom_mcap){ *dx=0;*dy=0;*buttons=0; doom_mdx=doom_mdy=0; return 0; }
    int any=(doom_mdx||doom_mdy||doom_mbtn!=doom_mbtn_prev);
    *dx=doom_mdx; *dy=doom_mdy; *buttons=doom_mbtn; doom_mdx=0; doom_mdy=0; doom_mbtn_prev=doom_mbtn;
    return any;
}
void DG_Init(void){}
void DG_SetWindowTitle(const char* t){ (void)t; }
unsigned int DG_GetTicksMs(void){ return (unsigned int)(g_ticks*10ULL); }
void DG_SleepMs(unsigned int ms){ u64 tgt=g_ticks+(ms/10)+1; while(g_ticks<tgt) cpu_halt(); }
int DG_GetKey(int* pressed, unsigned char* key){
    for(int sc=1;sc<128;sc++){ if(kdown[sc]!=doom_prevkd[sc]){ int dk=sc_to_doomkey(sc); u8 st=kdown[sc]; doom_prevkd[sc]=st; if(dk){ *pressed=st?1:0; *key=(unsigned char)dk; return 1; } } }
    return 0; }
void DG_DrawFrame(void){ if(!DG_ScreenBuffer) return;
    for(int y=0;y<DOOM_H;y++){ int dy=g_doomy+y; if((u32)dy>=H)continue; u32* d=&SCENE[(u64)dy*SW+g_doomx]; unsigned int* s=&DG_ScreenBuffer[y*DOOM_W];
        int xm=DOOM_W; if(g_doomx+xm>(int)W)xm=(int)W-g_doomx; for(int x=0;x<xm;x++)d[x]=s[x]; } }
static void doom_init(void){
    dsh_set_output(doom_out); dsh_set_exit(doom_exit_cb);
    if(!g_doom_heap){ g_doom_heap=kmalloc(24u*1024u*1024u); if(!g_doom_heap){ g_doom_state=3; return; } dsh_set_heap(g_doom_heap); }
    extern u8 _binary_doom1_wad_start[], _binary_doom1_wad_end[];
    u32 modaddr=*(volatile u32*)0x5010ULL, modsize=*(volatile u32*)0x5018ULL;
    static const char* cand[5]={"DOOM2.WAD","DOOM.WAD","FREEDOOM.WAD","FREEDOO1.WAD","DOOM1.WAD"};
    static const char* inm[5]={"doom2.wad","doom.wad","freedoom2.wad","freedoom1.wad","doom1.wad"};
    const char* nm="doom1.wad"; doom_wadlen=0; g_doom_src=0;
    if(!g_doom_wad) g_doom_wad=(u8*)kmalloc(DOOM_WAD_CAP);
    if(g_doom_wad) for(int i=0;i<4;i++){ int n=fat_read_file(cand[i],g_doom_wad,DOOM_WAD_CAP); if(n>1000000){ doom_wadlen=n; nm=inm[i]; g_doom_src=cand[i]; break; } }
    if(doom_wadlen<=0 && modaddr && modsize>1000000u){ if(g_doom_wad)kfree(g_doom_wad); g_doom_wad=(u8*)(u64)modaddr; doom_wadlen=(long)modsize; g_doom_src="boot disk"; }
    if(doom_wadlen<=0){ long el=(long)(_binary_doom1_wad_end-_binary_doom1_wad_start); if(el>1000000){ if(g_doom_wad)kfree(g_doom_wad); g_doom_wad=_binary_doom1_wad_start; doom_wadlen=el; g_doom_src="built in"; } }
    if(doom_wadlen<=0){ g_doom_state=3; return; }
    dsh_set_file(nm,g_doom_wad,doom_wadlen);
    static char* argv[]={(char*)"doom",(char*)"-iwad",(char*)"doom1.wad",0}; argv[2]=(char*)nm;
    doomgeneric_Create(3,argv);
    g_doom_state=2;
}
void doom_draw(int i){
    win_frame(i,g_doom_mcap?"DOOM  -  Esc frees the mouse":"DOOM - WASD move, Ctrl fire, Space use, click aims");
    g_doomx=WN[i].x+4; g_doomy=WN[i].y+28;
    if(g_doom_state==3){ fill_rect(g_doomx,g_doomy,DOOM_W,DOOM_H,rgb(12,10,14));
        dstr_ctr(WN[i].x+WN[i].w/2, g_doomy+150, "Could not load the WAD.", rgb(230,120,110), 2);
        dstr_ctr(WN[i].x+WN[i].w/2, g_doomy+186, "The built-in DOOM1.WAD is missing. Put DOOM1.WAD or DOOM2.WAD on a FAT disk.", rgb(200,200,210), 1);
        dstr_ctr(WN[i].x+WN[i].w/2, g_doomy+206, "Check the log in Terminal for details.", rgb(150,155,170), 1); return; }
    if(doom_setjmp(doom_jb)==0){ if(g_doom_state==0){ fill_rect(g_doomx,g_doomy,DOOM_W,DOOM_H,rgb(10,10,14)); dstr_ctr(WN[i].x+WN[i].w/2,g_doomy+190,"Loading DOOM...",rgb(220,60,40),2); doom_init(); }
        if(g_doom_state==2){ doomgeneric_Tick(); } }
    else { g_doom_state=3; }
}
