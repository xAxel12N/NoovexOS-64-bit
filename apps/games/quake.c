#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sound.h"
#include "include/drivers.h"

extern volatile unsigned char kdown[128]; extern void toast(const char*);
extern void QG_Create(int argc,char** argv); extern void qsh_tick_us(unsigned long us);
extern void qsh_set_output(void (*fn)(const char*,int)); extern void qsh_set_file(const char* name,unsigned char* buf,long len);
extern void qsh_set_audio(int rate); extern int qsh_audio_pull(short* out,int frames);
extern void qsh_set_exit(void (*fn)(int)); extern void qsh_set_heap(void* p,unsigned long n); extern void qsh_set_clock(unsigned long long (*fn)(void));
typedef long qjmp[8];
extern int doom_setjmp(qjmp); extern void doom_longjmp(qjmp,int) __attribute__((noreturn));
extern long xzk_decode_into(const u8* in,long n,u8* out,long cap);
extern const u8 _binary_lq_pak0_xz_start[], _binary_lq_pak0_xz_end[], _binary_lq_pak1_xz_start[], _binary_lq_pak1_xz_end[];
#define QW 640
#define QH 480
int g_quake_state=0, g_quake_mcap=0, quake_mdx=0, quake_mdy=0, quake_mbtn=0; static int qm_prev=0;
static qjmp qjb; static u32 qpal[256]; static u8 qprev[128]; static u64 qlast=0; static char qmsg[96]=""; static const char* q_src="";
static int qx=0, qy=0, qdw=QW, qdh=QH; static char qline[160]; static int qln=0;
static void q_out(const char* s,int n){ for(int i=0;i<n;i++){ char c=s[i]; if(c=='\n'||qln>=158){ qline[qln]=0; if(qln){ char b[176],*p=b; p=scopy(p,"quake: "); p=scopy(p,qline); *p=0; klog(b); } qln=0; if(c!='\n')qline[qln++]=c; } else if(c>=32&&c<127)qline[qln++]=c; } }
static void q_exit(int code){ g_quake_state=code==0?4:3; if(!qmsg[0])scopy(qmsg,"Quake stopped - see the log (logs) for the reason"); doom_longjmp(qjb,1); }
static unsigned long long q_clock(void){ return hr_ns(); }
void QG_Init(void){}
void QG_Quit(void){ g_quake_state=4; doom_longjmp(qjb,2); }
void QG_SetPalette(unsigned char p[768]){ for(int i=0;i<256;i++)qpal[i]=((u32)p[i*3]<<16)|((u32)p[i*3+1]<<8)|(u32)p[i*3+2]; }
void QG_DrawFrame(void* pixels){
    const u8* s=(const u8*)pixels; u32* sc=SCENE;
    if(qdw==QW&&qdh==QH){ for(int y=0;y<QH;y++){ int dy=qy+y; if((u32)dy>=H)continue; u32* d=sc+(u64)dy*SW+qx; const u8* r=s+y*QW; int xm=QW; if(qx+xm>(int)W)xm=(int)W-qx; for(int x=0;x<xm;x++)d[x]=qpal[r[x]]; } return; }
    for(int y=0;y<qdh;y++){ int dy=qy+y; if((u32)dy>=H)continue; u32* d=sc+(u64)dy*SW+qx; const u8* r=s+(y*QH/qdh)*QW; for(int x=0;x<qdw;x++){ int dx=qx+x; if((u32)dx>=W)break; d[x]=qpal[r[x*QW/qdw]]; } } }
static int q_key(int sc){ static const char LET[]="qwertyuiop"; static const char L2[]="asdfghjkl"; static const char L3[]="zxcvbnm";
    if(sc>=0x10&&sc<=0x19)return LET[sc-0x10]; if(sc>=0x1E&&sc<=0x26)return L2[sc-0x1E]; if(sc>=0x2C&&sc<=0x32)return L3[sc-0x2C];
    if(sc>=0x02&&sc<=0x0A)return '1'+sc-0x02; if(sc==0x0B)return '0';
    switch(sc){ case 0x01: return 27; case 0x0F: return 9; case 0x1C: return 13; case 0x39: return ' '; case 0x0E: return 127;
        case 0x48: return 128; case 0x50: return 129; case 0x4B: return 130; case 0x4D: return 131; case 0x38: return 132; case 0x1D: return 133; case 0x2A: case 0x36: return 134;
        case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: return 135+sc-0x3B; case 0x57: return 145; case 0x58: return 146;
        case 0x52: return 147; case 0x53: return 148; case 0x51: return 149; case 0x49: return 150; case 0x47: return 151; case 0x4F: return 152;
        case 0x0C: return '-'; case 0x0D: return '='; case 0x1A: return '['; case 0x1B: return ']'; case 0x27: return ';'; case 0x28: return '\''; case 0x29: return '`'; case 0x2B: return '\\'; case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/'; }
    return 0; }
int QG_GetKey(int* down,int* key){
    for(int sc=1;sc<128;sc++){ if(kdown[sc]!=qprev[sc]){ u8 st=kdown[sc]; qprev[sc]=st; int k=q_key(sc); if(k){ *down=st?1:0; *key=k; return 1; } } }
    for(int b=0;b<3;b++){ int m=1<<b; if((quake_mbtn&m)!=(qm_prev&m)){ qm_prev^=m; *down=(quake_mbtn&m)?1:0; *key=200+b; return 1; } }
    return 0; }
void QG_GetMouseMove(int* x,int* y){ if(!g_quake_mcap){ *x=0; *y=0; quake_mdx=quake_mdy=0; return; } *x=quake_mdx*2; *y=quake_mdy*2; quake_mdx=0; quake_mdy=0; }
void QG_GetJoyAxes(float* a){ for(int i=0;i<6;i++)a[i]=0; }
static int fs_find(const char* nm){ for(int k=0;k<FS_N;k++){ if(!FS[k].used)continue; const char* a=FS[k].name; int i=0; for(;a[i]&&nm[i];i++){ char c=a[i]; if(c>='A'&&c<='Z')c+=32; if(c!=nm[i])break; } if(!a[i]&&!nm[i])return k; } return -1; }
static int q_snd_cap=0, q_snd_rate=0; static short q_pcm[4096*2];
static void q_audio(void){
    if(!q_snd_rate||g_snd_state!=1)return;
    int fb=g_snd.free_bytes(); if(fb<0)return; if(fb>q_snd_cap)q_snd_cap=fb;
    int want=q_snd_rate*4*70/1000-(q_snd_cap-fb); if(want>fb)want=fb; if(want>(int)sizeof(q_pcm))want=(int)sizeof(q_pcm); want&=~3; if(want<=0)return;
    int fr=qsh_audio_pull(q_pcm,want/4); if(fr>0)g_snd.write((const u8*)q_pcm,fr*4); }
static u8* unxz(const u8* s,const u8* e,long* out){ long cap=64L*1024*1024; u8* b=(u8*)kmalloc((unsigned long)cap); if(!b)return 0; long n=xzk_decode_into(s,(long)(e-s),b,cap); if(n<=0){ kfree(b); return 0; } *out=n; return b; }
static const char QCFG[]="bind w +forward\nbind s +back\nbind a +moveleft\nbind d +moveright\nbind SPACE +jump\nbind MOUSE1 +attack\nbind CTRL +attack\nbind SHIFT +speed\nbind e +jump\nbind MOUSE2 +forward\n+mlook\nsensitivity 5\nviewsize 100\ncrosshair 1\n";
static void quake_init(void){
    static u8* heap=0; if(!heap){ heap=(u8*)kmalloc(96UL*1024*1024); if(!heap){ scopy(qmsg,"Not enough memory for Quake (it needs 96 MB for itself and up to 128 MB for the game data)"); g_quake_state=3; return; } qsh_set_heap(heap,96UL*1024*1024); }
    qsh_set_output(q_out); qsh_set_exit(q_exit); qsh_set_clock(q_clock);
    int k0=fs_find("pak0.pak"), k1=fs_find("pak1.pak");
    if(k0>=0){ extern char* fsd(int); qsh_set_file("pak0.pak",(unsigned char*)fsd(k0),FS[k0].len); if(k1>=0)qsh_set_file("pak1.pak",(unsigned char*)fsd(k1),FS[k1].len); q_src="your pak0.pak from Files"; }
    else { long n0=0,n1=0; u8* p0=unxz(_binary_lq_pak0_xz_start,_binary_lq_pak0_xz_end,&n0); u8* p1=unxz(_binary_lq_pak1_xz_start,_binary_lq_pak1_xz_end,&n1);
        if(!p0){ scopy(qmsg,"Could not unpack the built-in LibreQuake data"); g_quake_state=3; return; }
        qsh_set_file("pak0.pak",p0,n0); if(p1)qsh_set_file("pak1.pak",p1,n1); q_src="built-in LibreQuake (BSD licence)"; }
    qsh_set_file("autoexec.cfg",(unsigned char*)QCFG,(long)sizeof(QCFG)-1);
    q_snd_rate=0; if(snd_init()){ g_snd.start(); q_snd_rate=g_snd.rate>0?g_snd.rate:48000; } qsh_set_audio(q_snd_rate); klog(q_snd_rate?"quake: sound on":"quake: no sound device, playing silent");
    for(int i=0;i<128;i++)qprev[i]=kdown[i];
    static char* argv[]={(char*)"quake",(char*)"-basedir",(char*)"/q",(char*)"+map",(char*)"start",0};
    klog("quake: starting"); QG_Create(5,argv); g_quake_state=2; qlast=hr_ns(); }
void quake_draw(int i){
    win_frame(i,g_quake_mcap?"Quake - Esc frees the mouse":"Quake - click to play: WASD move, mouse aim, click fire, Space jump");
    int cx=WN[i].x+2, cy=WN[i].y+TITLEH, cw=WN[i].w-4, ch=WN[i].h-TITLEH-2;
    int dw=cw, dh=cw*3/4; if(dh>ch){ dh=ch; dw=ch*4/3; } if(dw>QW-8&&dw<QW+8&&dh>QH-8&&dh<QH+8){ dw=QW; dh=QH; }
    qdw=dw; qdh=dh; qx=cx+(cw-dw)/2; qy=cy+(ch-dh)/2;
    if(qx>cx||qy>cy){ fill_rect(cx,cy,cw,ch,rgb(0,0,0)); }
    if(g_quake_state>=3){ fill_rect(cx,cy,cw,ch,rgb(14,12,10)); dstr_ctr(cx+cw/2,cy+ch/2-30,g_quake_state==4?"Quake has quit.":"Quake stopped.",rgb(230,170,110),2); dstr_ctr(cx+cw/2,cy+ch/2+6,g_quake_state==4?"Restart NoovexOS to play again.":qmsg,rgb(200,200,210),1); return; }
    if(doom_setjmp(qjb)==0){
        if(g_quake_state==0){ fill_rect(cx,cy,cw,ch,rgb(10,8,6)); dstr_ctr(cx+cw/2,cy+ch/2-10,"Loading Quake...",rgb(200,120,60),2); g_quake_state=1; return; }
        if(g_quake_state==1){ quake_init(); return; }
        if(g_quake_state==2){ u64 now=hr_ns(); u64 us=(now-qlast)/1000ULL; qlast=now; if(us>100000)us=100000; if(us<1000)us=1000; qsh_tick_us((unsigned long)us); q_audio(); } }
    else { if(g_quake_state==2)g_quake_state=3; } }
void quake_click(void){ if(g_quake_state==2)g_quake_mcap=1; }
void quake_closed(void){ g_quake_mcap=0; }
const char* quake_source(void){ return q_src; }
