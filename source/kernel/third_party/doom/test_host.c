extern void doomgeneric_Create(int,char**);
extern void doomgeneric_Tick(void);
extern void dsh_set_output(void(*)(const char*,int));
extern void dsh_set_file(const char*,unsigned char*,long);
extern void dsh_set_exit(void(*)(int));
extern void dsh_set_heap(void*);
static unsigned char dheap[24*1024*1024] __attribute__((aligned(16)));
static long sys(long n,long a,long b,long c){ long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory"); return r; }
static void out(const char* s,int n){ sys(1,2,(long)s,n); }
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void outs(const char*s){ out(s,slen(s)); }
static void doexit(int c){ sys(231,c,0,0); }
void DG_Init(void){} void DG_DrawFrame(void){} void DG_SleepMs(unsigned int m){(void)m;}
static unsigned int tk=0; unsigned int DG_GetTicksMs(void){ return tk+=20; }
int DG_GetKey(int* p,unsigned char* k){ (void)p;(void)k; return 0; }
void DG_SetWindowTitle(const char* t){ (void)t; }
static unsigned char wadbuf[32*1024*1024];
static int run(void){
  long fd=sys(2,(long)"/tmp/wad/freedoom1.wad",0,0);
  if(fd<0){ outs("open freedoom1.wad FAILED\n"); return 1; }
  long total=0; for(;;){ long r=sys(0,fd,(long)(wadbuf+total),1<<20); if(r<=0)break; total+=r; if(total>=32*1024*1024)break; }
  sys(3,fd,0,0);
  dsh_set_heap(dheap); dsh_set_output(out); dsh_set_exit(doexit); dsh_set_file("freedoom1.wad",wadbuf,total);
  outs(">> WAD loaded, starting DOOM init...\n\n");
  static char* argv[]={(char*)"doom",(char*)"-iwad",(char*)"freedoom1.wad",0};
  doomgeneric_Create(3,argv);
  outs("\n>> doomgeneric_Create RETURNED - init succeeded. Running 5 ticks...\n");
  for(int i=0;i<60;i++) doomgeneric_Tick();
  extern unsigned int* DG_ScreenBuffer;
  long wf=sys(2,(long)"/tmp/doomframe.raw",577,420);
  if(wf>=0){ long tot=640L*400L*4L, off=0; while(off<tot){ long w=sys(1,wf,(long)((char*)DG_ScreenBuffer+off),tot-off); if(w<=0)break; off+=w; } sys(3,wf,0,0); outs(">> frame dumped after 60 ticks. DOOM ENGINE + WAD WORK.\n"); }
  else outs(">> 60 ticks ran. DOOM ENGINE + WAD WORK.\n");
  return 0;
}
__attribute__((force_align_arg_pointer)) void _start(void){ int r=run(); doexit(r); }
