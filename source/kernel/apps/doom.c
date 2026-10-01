/* ============================ DOOM (doomgeneric + FreeDoom) ============================ */
extern unsigned int* DG_ScreenBuffer;
extern void doomgeneric_Create(int argc, char** argv);
extern void doomgeneric_Tick(void);
extern void dsh_set_output(void(*)(const char*,int));
extern void dsh_set_file(const char*, unsigned char*, long);
extern void dsh_set_exit(void(*)(int));
#define DOOM_W 640
#define DOOM_H 400
#define DOOM_WAD     ((u8*)0x5800000ULL)     /* WAD lives at 88 MiB (above .bss): GRUB module copied here, or FAT read here */
#define DOOM_WAD_CAP (32*1024*1024)
static int g_doomx=0, g_doomy=0, g_doom_state=0; /* 0 init 2 running 3 failed */
static long doom_wadlen=0;
/* minimal setjmp/longjmp so I_Error/exit can unwind back to the desktop */
typedef long djmp[8];
__asm__(".globl doom_setjmp\ndoom_setjmp:\n mov %rbx,0(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n mov %r15,40(%rdi)\n lea 8(%rsp),%rax\n mov %rax,48(%rdi)\n mov (%rsp),%rax\n mov %rax,56(%rdi)\n xor %eax,%eax\n ret\n"
        ".globl doom_longjmp\ndoom_longjmp:\n mov 0(%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n mov 40(%rdi),%r15\n mov 48(%rdi),%rsp\n mov %esi,%eax\n test %eax,%eax\n jnz 1f\n mov $1,%eax\n1: jmp *56(%rdi)\n");
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
static int g_doom_mcap=0;                       /* mouse captured by DOOM (ESC releases) */
static int doom_mdx=0, doom_mdy=0, doom_mbtn=0, doom_mbtn_prev=0;
int DG_GetMouse(int* dx,int* dy,int* buttons){
    if(!g_doom_mcap){ *dx=0;*dy=0;*buttons=0; doom_mdx=doom_mdy=0; return 0; }
    int any=(doom_mdx||doom_mdy||doom_mbtn!=doom_mbtn_prev);
    *dx=doom_mdx; *dy=doom_mdy; *buttons=doom_mbtn; doom_mdx=0; doom_mdy=0; doom_mbtn_prev=doom_mbtn;
    return any;
}
void DG_Init(void){}
void DG_SetWindowTitle(const char* t){ (void)t; }
unsigned int DG_GetTicksMs(void){ return (unsigned int)(g_ticks*10ULL); }
void DG_SleepMs(unsigned int ms){ u64 tgt=g_ticks+(ms/10)+1; while(g_ticks<tgt) __asm__ volatile("hlt"); }
int DG_GetKey(int* pressed, unsigned char* key){
    for(int sc=1;sc<128;sc++){ if(kdown[sc]!=doom_prevkd[sc]){ int dk=sc_to_doomkey(sc); u8 st=kdown[sc]; doom_prevkd[sc]=st; if(dk){ *pressed=st?1:0; *key=(unsigned char)dk; return 1; } } }
    return 0; }
void DG_DrawFrame(void){ if(!DG_ScreenBuffer) return;
    for(int y=0;y<DOOM_H;y++){ int dy=g_doomy+y; if((u32)dy>=H)continue; u32* d=&SCENE[(u64)dy*SW+g_doomx]; unsigned int* s=&DG_ScreenBuffer[y*DOOM_W];
        int xm=DOOM_W; if(g_doomx+xm>(int)W)xm=(int)W-g_doomx; for(int x=0;x<xm;x++)d[x]=s[x]; } }
static void doom_init(void){
    dsh_set_output(doom_out); dsh_set_exit(doom_exit_cb);
    u32 modaddr=*(volatile u32*)0x5010ULL, modsize=*(volatile u32*)0x5018ULL;
    doom_wadlen=0;
    if(modaddr && modsize>1000000u){ doom_wadlen=(long)modsize; }        /* WAD from GRUB module (already at DOOM_WAD) */
    else { const char* cand[5]={"DOOM1.WAD","FREEDOOM.WAD","DOOM.WAD","DOOM2.WAD","FREEDOO1.WAD"};
        for(int i=0;i<5;i++){ int n=fat_read_file(cand[i],DOOM_WAD,DOOM_WAD_CAP); if(n>0){ doom_wadlen=n; break; } } }
    if(doom_wadlen<=0){ g_doom_state=3; return; }
    dsh_set_file("freedoom1.wad",DOOM_WAD,doom_wadlen);
    static char* argv[]={(char*)"doom",(char*)"-iwad",(char*)"freedoom1.wad",0};
    doomgeneric_Create(3,argv);
    g_doom_state=2;
}
static void doom_draw(int i){
    win_frame(i,"DOOM");
    g_doomx=WN[i].x+4; g_doomy=WN[i].y+28;
    if(g_doom_state==3){ fill_rect(g_doomx,g_doomy,DOOM_W,DOOM_H,rgb(12,10,14));
        dstr_ctr(WN[i].x+WN[i].w/2, g_doomy+150, "Could not load the WAD.", rgb(230,120,110), 2);
        dstr_ctr(WN[i].x+WN[i].w/2, g_doomy+186, "Put freedoom1.wad (renamed DOOM1.WAD) on an IDE FAT disk.", rgb(200,200,210), 1);
        dstr_ctr(WN[i].x+WN[i].w/2, g_doomy+206, "Check the log in Terminal for details.", rgb(150,155,170), 1); return; }
    if(doom_setjmp(doom_jb)==0){ if(g_doom_state==0){ fill_rect(g_doomx,g_doomy,DOOM_W,DOOM_H,rgb(10,10,14)); dstr_ctr(WN[i].x+WN[i].w/2,g_doomy+190,"Loading FreeDoom...",rgb(220,60,40),2); doom_init(); }
        if(g_doom_state==2){ doomgeneric_Tick(); } }
    else { g_doom_state=3; }
}
