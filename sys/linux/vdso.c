#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#if __has_include("build/vdso_so.h")
#include "build/vdso_so.h"
#else
static const unsigned char vdso_so[1]={0}; static const unsigned int vdso_so_len=0;
#define VDSO_VVAR_OFF 0u
#endif

#define VDSO_V 0x40000180000ULL
static u64 g_vd_fr[8]; static int g_vd_np; static u64 g_vv_fr; static int g_vd_state;
u64 lx_epoch_ns(void);
u32 g_vd_maps;
static void vdso_fill(void){
    volatile u64* v=(volatile u64*)g_vv_fr;
    v[0]++; __atomic_thread_fence(__ATOMIC_SEQ_CST);
    v[1]=g_hr.src==2?1:0; v[2]=g_hr.tsc0; v[3]=g_hr.tsc_mult; v[4]=lx_epoch_ns();
    __atomic_thread_fence(__ATOMIC_SEQ_CST); v[0]++; }
static int vdso_init(void){
    if(g_vd_state)return g_vd_state>0;
    g_vd_state=-1;
    u32 n=vdso_so_len; int np=(int)(VDSO_VVAR_OFF/4096u); if(n>VDSO_VVAR_OFF)n=VDSO_VVAR_OFF; if(np<1||np>8||(VDSO_VVAR_OFF&4095u)||!g_hr.ready)return 0;
    for(int i=0;i<np;i++){ u64 f=pframe_alloc(); if(!f)return 0; u8* d=(u8*)f; for(u32 k=0;k<4096;k++){ u32 o=(u32)i*4096u+k; d[k]=o<n?vdso_so[o]:0; } g_vd_fr[i]=f; }
    u64 f=pframe_alloc(); if(!f)return 0; for(int k=0;k<512;k++)((u64*)f)[k]=0; g_vv_fr=f;
    g_vd_np=np; vdso_fill(); g_vd_state=1; return 1; }
void vdso_resync(void){ if(g_vd_state>0)vdso_fill(); }
u64 vdso_map(u64 cr3){
    if(!vdso_init())return 0;
    for(int i=0;i<g_vd_np;i++){ pframe_ref(g_vd_fr[i]); if(vm_map(cr3,VDSO_V+((u64)i<<12),g_vd_fr[i],0x5ULL|PTE_COW)){ pframe_free(g_vd_fr[i]); return 0; } }
    pframe_ref(g_vv_fr); if(vm_map(cr3,VDSO_V+((u64)g_vd_np<<12),g_vv_fr,vm_pflags(0x5ULL|PTE_COW,PTE_NX))){ pframe_free(g_vv_fr); return 0; }
    g_vd_maps++; return VDSO_V; }
u64 vdso_start(void){ return g_vd_state>0?VDSO_V:0; }
u64 vdso_end(void){ return g_vd_state>0?VDSO_V+((u64)(g_vd_np+1)<<12):0; }
