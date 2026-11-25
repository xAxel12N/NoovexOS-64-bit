#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "drivers/sound/midisynth_core.h"
void umidi_poll(void);
int apcm_playing(void);
static struct ms_state g_ms; static int g_ms_on=0, g_ms_cap=0;
static volatile int g_ms_busy;
static volatile u32 g_msyq[256]; static volatile u32 g_msyq_w=0, g_msyq_r=0;
int msynth_enabled(void){ return g_ms_on; }
int msynth_enable(int on){
    if(on){ if(!snd_init())return 0; ms_init(&g_ms,g_snd.rate>0?g_snd.rate:48000); g_msyq_r=g_msyq_w; g_ms_cap=0; g_ms_on=1; klog("midi: synth on"); return 1; }
    g_ms_on=0; ms_init(&g_ms,g_ms.rate); return 1; }
void msynth_event(u8 st,u8 d1,u8 d2){
    if(!g_ms_on)return;
    u32 k; do{ k=g_msyq_w; if(k-g_msyq_r>=255)return; }while(!__sync_bool_compare_and_swap(&g_msyq_w,k,k+1));
    g_msyq[k&255]=0x80000000u|((u32)st<<16)|((u32)d1<<8)|d2; }
static void ms_drain(void){ for(int g=0;g<256&&g_msyq_r!=g_msyq_w;g++){ u32 e=g_msyq[g_msyq_r&255]; if(!(e&0x80000000u))break; g_msyq[g_msyq_r&255]=0; g_msyq_r++; ms_event(&g_ms,(u8)(e>>16),(u8)(e>>8),(u8)e); } }
void msynth_tick(void){
    if(!g_ms_on)return;
    umidi_poll();
    if(apcm_playing()||__sync_lock_test_and_set(&g_ms_busy,1))return;
    ms_drain();
    if(!ms_active(&g_ms)||g_snd_state!=1||!g_snd.write||!g_snd.free_bytes){ __sync_lock_release(&g_ms_busy); return; }
    if(g_snd.start)g_snd.start();
    int fb=g_snd.free_bytes(); if(fb>g_ms_cap)g_ms_cap=fb;
    int lim=g_ms.rate*40/1000*4, fill=g_ms_cap-fb, can=lim-fill; if(can>fb)can=fb; can&=~3; if(fb<=0||can<=0){ __sync_lock_release(&g_ms_busy); return; }
    static short buf[2048];
    while(can>0){ int fr=can/4; if(fr>1024)fr=1024;
        ms_render(&g_ms,buf,fr);
        int w=g_snd.write((const u8*)buf,fr*4); if(w<=0)break; can-=w; if(w<fr*4)break; }
    __sync_lock_release(&g_ms_busy); }
int msynth_mix(int* acc,int fr){
    if(!g_ms_on||fr<=0||fr>1024||__sync_lock_test_and_set(&g_ms_busy,1))return 0;
    ms_drain(); int r=0;
    if(ms_active(&g_ms)){ static short mb[2048]; ms_render(&g_ms,mb,fr); for(int k=0;k<fr*2;k++)acc[k]+=mb[k]; r=1; }
    __sync_lock_release(&g_ms_busy); return r; }
void msynth_note(int note,int vel,int ms){
    int was=g_ms_on; if(!was&&!msynth_enable(1))return;
    msynth_event(0x90,(u8)note,(u8)vel);
    u64 t0=hr_ns(); int off=0;
    while(hr_ns()-t0<(u64)(ms+400)*1000000ULL){ if(!off&&hr_ns()-t0>=(u64)ms*1000000ULL){ msynth_event(0x80,(u8)note,0); off=1; } msynth_tick(); if(off&&!ms_active(&g_ms))break; }
    if(!was)msynth_enable(0); }
