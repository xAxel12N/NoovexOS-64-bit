#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DOOM_HOST 1
#include "arch/x86_64/user/doom_jmp.c"
#include "apps/games/doom.c"

u32 W=1280, H=720, SW=1280; static u32 scene[1280*720]; u32* g_scene=scene;
u32 g_scene_acc, g_scene_wr; volatile int g_gq_inb, g_gfx_run, g_gq_capon; int g_scene_quiet=1, g_rt=0;
void (*g_gq_mark_hook)(int,int,int,int,int)=0;
void gfx_sync(void){}
Win WN[NWIN]; int focus=W_DOOM, ztop=0, drag=-1, dox=0, doy=0;
volatile u64 g_ticks=1000;
unsigned int* DG_ScreenBuffer=0; static unsigned int shot[DOOM_W*DOOM_H];
static int n_create=0, n_tick=0, tick_exit_code=-100, create_exit_code=-100;
static void (*exit_hook)(int)=0; static int n_setfile=0; static const char* last_text[8]; static int n_text=0;
static int fills=0; static int fx,fy,fw,fh;

void* kmalloc(unsigned long n){ return calloc(1,n); }
void kfree(void* p){ free(p); }
int fat_read_file(const char* name,u8* out,int cap){ (void)name; (void)out; (void)cap; return 0; }
u8 _binary_doom1_wad_xz_start[4], _binary_doom1_wad_xz_end[1];
long xzk_decode_into(const u8* in,long n,u8* out,long cap){ (void)in; (void)n; (void)out; (void)cap; return 2000000; }
void term_putc(char c){ (void)c; }
void dsh_set_output(void(*f)(const char*,int)){ (void)f; }
void dsh_set_file(const char* nm,unsigned char* b,long n){ (void)nm; (void)b; (void)n; n_setfile++; }
void dsh_set_heap(void* p){ (void)p; }
void dsh_set_exit(void(*f)(int)){ exit_hook=f; }
void doomgeneric_Create(int argc,char** argv){ (void)argc; (void)argv; n_create++; DG_ScreenBuffer=shot; if(create_exit_code!=-100){ int c=create_exit_code; create_exit_code=-100; exit_hook(c); } }
void doomgeneric_Tick(void){ n_tick++; { int p; unsigned char k; while(DG_GetKey(&p,&k)){} } if(tick_exit_code!=-100){ int c=tick_exit_code; tick_exit_code=-100; exit_hook(c); } DG_DrawFrame(); }
void fill_rect(int x,int y,int w,int h,u32 c){ fills++; fx=x; fy=y; fw=w; fh=h; for(int j=0;j<h;j++)for(int i=0;i<w;i++){ if((u32)(x+i)<W&&(u32)(y+j)<H)scene[(y+j)*SW+x+i]=c; } }
void dstr_ctr(int cx,int y,const char* s,u32 c,int sc){ (void)cx; (void)y; (void)c; (void)sc; if(n_text<8)last_text[n_text++]=s; }
void win_frame(int i,const char* t){ (void)i; (void)t; }

static int kq[64][2]; static int kqh=0, kqt=0;
static void tkey(int sc,int down){ kq[kqt][0]=sc; kq[kqt][1]=down; kqt=(kqt+1)%64; }
int gkey_pop(int who,int* down){ (void)who; if(kqh==kqt)return -1; *down=kq[kqh][1]; int sc=kq[kqh][0]; kqh=(kqh+1)%64; return sc; }

static int fail=0;
#define CHECK(name,cond) do{ int ok_=(cond); printf("%s %s\n",name,ok_?"ok":"FAIL"); if(!ok_)fail++; }while(0)
static int saw(const char* s){ for(int i=0;i<n_text;i++)if(last_text[i]&&strstr(last_text[i],s))return 1; return 0; }
static void frame(void){ n_text=0; fills=0; g_ticks+=2; doom_draw(W_DOOM); }

int main(void){
    WN[W_DOOM].open=1; WN[W_DOOM].x=100; WN[W_DOOM].y=80; WN[W_DOOM].w=648; WN[W_DOOM].h=430;
    int dw,dh;
    doom_fit(644,404,&dw,&dh); CHECK("fit: a window near 640x400 uses the native size", dw==640&&dh==400);
    doom_fit(1268,660,&dw,&dh); CHECK("fit: maximised keeps 16:10 with side bars", dh==660&&dw==1056);
    doom_fit(1000,400,&dw,&dh); CHECK("fit: wide window limits by height", dh==400&&dw==640);
    doom_fit(400,600,&dw,&dh); CHECK("fit: narrow window limits by width", dw==400&&dh==250);

    frame(); CHECK("first draw loads DOOM and starts it", g_doom_state==2&&n_create==1&&n_setfile==1);
    frame(); CHECK("running frames tick the engine", n_tick==2);
    CHECK("640x400 window draws the frame unscaled", g_doom_dw==640&&g_doom_dh==400&&g_doomx==WN[W_DOOM].x+4);

    for(int i=0;i<DOOM_W*DOOM_H;i++)shot[i]=0x00112233u; shot[DOOM_W-1]=0x00ff0000u; shot[(DOOM_H-1)*DOOM_W]=0x0000ff00u;
    WN[W_DOOM].x=6; WN[W_DOOM].y=32; WN[W_DOOM].w=1268; WN[W_DOOM].h=602; memset(scene,0x77,sizeof scene);
    frame();
    { int cx=WN[W_DOOM].x+2, cy=WN[W_DOOM].y+TITLEH, cw=WN[W_DOOM].w-4, ch=WN[W_DOOM].h-TITLEH-2;
      doom_fit(cw,ch,&dw,&dh);
      int ok=g_doom_dw==dw&&g_doom_dh==dh&&g_doomx==cx+(cw-dw)/2&&g_doomy==cy+(ch-dh)/2;
      CHECK("maximised: frame is scaled and centred", ok);
      CHECK("maximised: bars at the sides are black", scene[(cy+ch/2)*SW+cx]==0&&scene[(cy+ch/2)*SW+cx+cw-1]==0);
      CHECK("maximised: scaled frame keeps its corners", scene[g_doomy*SW+g_doomx+dw-1]==0x00ff0000u&&scene[(g_doomy+dh-1)*SW+g_doomx]==0x0000ff00u&&scene[(g_doomy+dh/2)*SW+g_doomx+dw/2]==0x00112233u); }
    WN[W_DOOM].x=100; WN[W_DOOM].y=80; WN[W_DOOM].w=648; WN[W_DOOM].h=430;

    int pressed=0; unsigned char key=0;
    tkey(0x11,1); tkey(0x11,0);
    int r1=DG_GetKey(&pressed,&key); int p1=pressed, k1=key; int r2=DG_GetKey(&pressed,&key); int p2=pressed, k2=key; int r3=DG_GetKey(&pressed,&key);
    CHECK("a tap between two frames gives press and release", r1&&p1==1&&k1==0xad&&r2&&p2==0&&k2==0xad&&!r3);
    tkey(0x1D,1); tkey(0x1D,1); tkey(0x1D,0);
    r1=DG_GetKey(&pressed,&key); r2=DG_GetKey(&pressed,&key); p2=pressed; r3=DG_GetKey(&pressed,&key);
    CHECK("typematic repeats are not reported twice", r1&&r2&&p2==0&&!r3);
    tkey(0x1F,1); DG_GetKey(&pressed,&key); focus=W_TERM; tkey(0x11,1); tkey(0x11,0);
    r1=DG_GetKey(&pressed,&key); p1=pressed; k1=key; r2=DG_GetKey(&pressed,&key);
    CHECK("losing focus releases held keys and drops the queued ones", r1&&p1==0&&k1==0xaf&&!r2&&gkey_pop(0,&pressed)<0);
    focus=W_DOOM; DG_GetKey(&pressed,&key);

    int dx,dy,b;
    doom_click(); doom_mbtn=1;
    CHECK("the capturing click grabs the mouse", g_doom_mcap==1);
    DG_GetMouse(&dx,&dy,&b); int b1=b; DG_GetMouse(&dx,&dy,&b); int b2=b;
    doom_mbtn=0; DG_GetMouse(&dx,&dy,&b); int b3=b; doom_mbtn=1; DG_GetMouse(&dx,&dy,&b); int b4=b;
    CHECK("the capturing click does not fire, the next one does", b1==0&&b2==0&&b3==0&&b4==1);
    doom_mdx=5; doom_mbtn=0; DG_GetMouse(&dx,&dy,&b); CHECK("mouse motion still reaches the game", dx==5&&b==0);
    doom_click(); CHECK("a click while captured changes nothing", g_doom_mcap==1&&doom_mswallow==0);

    tick_exit_code=0; frame();
    CHECK("quitting from the menu ends in the quit state, not the WAD error", g_doom_state==4&&g_doom_mcap==0);
    frame(); CHECK("the quit screen says DOOM has quit and offers a restart", saw("DOOM has quit")&&saw("play again")&&!saw("Could not load")&&n_tick==4);
    tkey(0x1C,1); tkey(0x1C,0); frame();
    CHECK("Enter on the quit screen starts DOOM again", g_doom_state==0);
    frame(); CHECK("the second run re-creates the engine without re-registering the WAD", g_doom_state==2&&n_create==2&&n_setfile==1);
    frame(); CHECK("the second run ticks", n_tick==6);

    tick_exit_code=3; frame();
    CHECK("an error exit shows the stopped state with a message", g_doom_state==5&&saw("DOOM stopped")==0);
    frame(); CHECK("the stopped screen names the error and offers a restart", saw("DOOM stopped")&&saw("error")&&saw("play again"));
    doom_click(); CHECK("a click on the stopped screen restarts", g_doom_state==0);
    frame(); CHECK("third run starts", g_doom_state==2&&n_create==3&&doom_can_restart()==0);
    tick_exit_code=0; frame(); frame();
    CHECK("after the last run the heap budget is used up and a reboot is advised", g_doom_state==4&&saw("Restart NoovexOS")&&!saw("Press Enter"));
    doom_click(); tkey(0x1C,1); tkey(0x1C,0); frame();
    CHECK("neither click nor Enter restarts past the budget", g_doom_state==4&&n_create==3);

    doom_runs=0; g_doom_state=4; doom_click(); frame(); CHECK("restart ready for the failure case", g_doom_state==2&&n_create==4);
    tick_exit_code=0; frame(); doom_click(); create_exit_code=-1; frame();
    CHECK("a failed restart is reported as such and blocks further restarts", g_doom_state==5&&!doom_can_restart());
    frame(); CHECK("failed restart text", saw("could not start again")&&saw("Restart NoovexOS"));

    doom_runs=0; g_doom_state=0; doom_wadlen=0; create_exit_code=-1; frame();
    CHECK("an error during the first start still shows the WAD message", g_doom_state==3);
    frame(); CHECK("WAD message", saw("Could not load the WAD"));

    printf(fail?"doom: %d FAILED\n":"ALL PASS\n",fail); return fail?1:0; }
