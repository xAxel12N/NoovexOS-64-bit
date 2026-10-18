#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/arch/x86_64/interrupts.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/arch/x86_64/hrtimer.h"
#include "include/nvx/core/sound.h"
struct nvx_snd g_snd; int g_snd_state=0, g_snd_vol=80;
int snd_init(void){
    if(g_snd_state)return g_snd_state==1;
    g_snd_state=2;
    if(hda_probe(&g_snd)||ac97_probe(&g_snd)){ g_snd_state=1; g_snd.volume(g_snd_vol); return 1; }
    return 0;
}
#define CAPN (1u<<20)
static u8 cap_buf[CAPN]; static u64 cap_w=0, cap_r=0; int g_cap_on=0, g_cap_level=0, g_cap_peak=0; u32 g_cap_over=0; int g_mic_gain=70; static u64 cap_seen=0; extern volatile u64 g_ticks;
int snd_rec_avail(void){ if(g_snd_state==0)snd_init(); return g_snd_state==1&&g_snd.rec_start!=0; }
const char* snd_rec_name(void){ return snd_rec_avail()&&g_snd.rec_name?g_snd.rec_name:"No microphone"; }
static void cap_pump(void){
    if(!g_cap_on||!g_snd.rec_read)return; u8 t[4096]; int lv=0;
    for(int g=0;g<64;g++){ int n=g_snd.rec_read(t,(int)sizeof(t)); if(n<=0)break;
        for(int i=0;i+3<n;i+=4){ short a=(short)(t[i]|(t[i+1]<<8)); short b=(short)(t[i+2]|(t[i+3]<<8)); int m=a<0?-a:a; int m2=b<0?-b:b; if(m2>m)m=m2; if(m>lv)lv=m; }
        for(int i=0;i<n;i++){ if(cap_w-cap_r>=CAPN){ cap_r++; g_cap_over++; } cap_buf[cap_w%CAPN]=t[i]; cap_w++; } }
    g_cap_level=lv*1000/32768; if(g_cap_level>g_cap_peak)g_cap_peak=g_cap_level; else g_cap_peak-=g_cap_peak/24+1; if(g_cap_peak<0)g_cap_peak=0; cap_seen=g_ticks; }
int snd_rec_start(void){ if(!snd_init()||!g_snd.rec_start)return 0; if(g_cap_on)return 1; if(g_snd.rec_gain)g_snd.rec_gain(g_mic_gain); if(!g_snd.rec_start())return 0; cap_w=cap_r=0; g_cap_on=1; g_cap_over=0; cap_seen=g_ticks; return 1; }
void snd_rec_stop(void){ if(!g_cap_on)return; if(g_snd.rec_stop)g_snd.rec_stop(); g_cap_on=0; g_cap_level=0; g_cap_peak=0; }
void snd_rec_gain(int pct){ if(pct<0)pct=0; if(pct>100)pct=100; g_mic_gain=pct; if(g_snd_state==1&&g_snd.rec_gain)g_snd.rec_gain(pct); }
int snd_cap_avail(void){ cap_pump(); return (int)(cap_w-cap_r); }
int snd_cap_read(u8* d,int len){ cap_pump(); u64 av=cap_w-cap_r; if((u64)len>av)len=(int)av; len&=~3; for(int i=0;i<len;i++)d[i]=cap_buf[(cap_r+(u64)i)%CAPN]; cap_r+=(u64)len; return len; }
void snd_poll(void){ if(g_snd_state==1&&g_snd.poll)g_snd.poll(); if(g_cap_on)cap_pump(); }
int snd_dev_read(char* buf,int len){
    if(!snd_rec_start())return -1;
    int n=snd_cap_read((u8*)buf,len&~3); if(n>0)return n;
    if(len<4)return 0;
    if(g_js_running)return JS_BLOCK;
    for(int t=0;t<1000&&n<=0;t++){ hr_delay_us(1000); n=snd_cap_read((u8*)buf,len&~3); }
    return n>0?n:0; }
int snd_dev_write(const char* buf,int len){
    if(!snd_init())return -1;
    g_snd.start();
    int n=g_snd.write((const u8*)buf,len&~3);
    if(n>0)return n;
    if(len<4)return len;
    if(g_js_running)return JS_BLOCK;
    for(int t=0;t<2000&&n<=0;t++){ hr_delay_us(1000); n=g_snd.write((const u8*)buf,len&~3); }
    return n>0?n:0;
}
static void snd_tone(int hz,int ms){
    if(!snd_init())return; g_snd.start();
    int rate=g_snd.rate>0?g_snd.rate:48000; long total=(long)rate*ms/1000; long done=0; long phase=0; long per=rate/(hz>0?hz:440);
    static u8 blk[4096];
    u64 t0=hr_ns();
    while(done<total&&hr_ns()-t0<(u64)(ms+2000)*1000000ULL){
        int fr=0; for(;fr<1024&&done+fr<total;fr++){ long p=(phase+fr)%per; long v=(p<per/2)?(p*4*6000/per-6000):(18000-p*4*6000/per); short s=(short)v; blk[fr*4]=(u8)s; blk[fr*4+1]=(u8)(s>>8); blk[fr*4+2]=(u8)s; blk[fr*4+3]=(u8)(s>>8); }
        int off=0; while(off<fr*4&&hr_ns()-t0<(u64)(ms+2000)*1000000ULL){ int n=g_snd.write(blk+off,fr*4-off); if(n>0)off+=n; else hr_delay_us(2000); }
        done+=fr; phase+=fr; }
}
void audio_cmd(const char* a){
    while(*a==' ')a++;
    if(!snd_init()){ tcol(rgb(240,120,120)); term_puts("audio: no HD Audio or AC97 device found\n"); tcol(WHT); return; }
    if(a[0]=='v'&&a[1]=='o'&&a[2]=='l'){ const char* p=a+3; while(*p==' ')p++; int v=0; while(*p>='0'&&*p<='9')v=v*10+(*p++-'0'); if(v>100)v=100; g_snd_vol=v; g_snd.volume(v); term_puts("volume set\n"); return; }
    if(a[0]=='m'&&a[1]=='i'&&a[2]=='c'){ const char* p=a+3; while(*p==' ')p++; int v=0; while(*p>='0'&&*p<='9')v=v*10+(*p++-'0'); snd_rec_gain(v); term_puts(snd_rec_avail()?"microphone level set\n":"no microphone input found\n"); return; }
    if(a[0]=='t'&&a[1]=='o'&&a[2]=='n'&&a[3]=='e'){ const char* p=a+4; while(*p==' ')p++; int hz=0; while(*p>='0'&&*p<='9')hz=hz*10+(*p++-'0'); if(hz<50||hz>8000)hz=440; term_puts("playing a 1 second tone\n"); snd_tone(hz,1000); return; }
    term_puts("audio: "); term_puts(g_snd.name);
    if(g_snd.name[0]=='I'){ term_puts("\n  "); term_puts(hda_info()); }
    term_puts("\n  input: "); term_puts(snd_rec_name());
    term_puts("\n  48 kHz, 16-bit stereo on /dev/dsp (play and record); 'audio tone [hz]', 'audio vol <0-100>', 'audio mic <0-100>', 'recorder'\n");
}
