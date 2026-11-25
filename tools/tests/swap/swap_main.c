#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#define PTE_ADDR 0x000FFFFFFFFFF000ULL
static long g_frames, g_fail_at = -1;
static u64 pframe_alloc(void){ if(g_fail_at>=0&&g_frames>=g_fail_at)return 0; void* p=aligned_alloc(4096,4096); if(!p)return 0; memset(p,0,4096); g_frames++; return (u64)p; }
static void pframe_free(u64 pa){ free((void*)pa); g_frames--; }
static void* kmalloc(unsigned long n){ return malloc(n); }
static u32 pf_n=100000; static u32 pframe_hi_count(void){ return 0; } static u32 pframe_free_count(void){ return g_fail_at>=0?(u32)(g_fail_at>g_frames?g_fail_at-g_frames:0):100000u; }
/*SWAPCODE*/
static unsigned rs=1; static unsigned rnd(void){ rs=rs*1103515245u+12345u; return rs>>8; }
static void fill(u8* p,int i){ int k=i%5; for(int j=0;j<4096;j++){ if(k==0)p[j]=(u8)rnd(); else if(k==1)p[j]=(u8)((j/7)&0xFF); else if(k==2)p[j]=0; else if(k==3)p[j]="swap test page "[j%15]; else p[j]=(u8)((rnd()%4)?p[j?j-1:0]:rnd()); } ((u32*)p)[0]=(u32)i*(k!=2); }
int main(void){
    static u8 p[4096], o[4096]; int ok=1; zs_setup(); g_swap_total=1<<20;
    for(int it=0;it<100000;it++){ for(int j=0;j<4096;j++)p[j]=(u8)((it%3==0)?rnd():(it%3==1)?(j/16+it):"abc"[j%3]); int n=lz_comp4k(p,o,2048); if(n>0){ static u8 d[4096]; if(lz_dec4k(o,n,d)||memcmp(d,p,4096)){ printf("FAIL codec round trip %d\n",it); ok=0; break; } for(int k=0;k<4;k++){ u8 c[2048]; memcpy(c,o,n); c[rnd()%n]^=(u8)(1u<<(rnd()%8)); lz_dec4k(c,n,d); lz_dec4k(c,(int)(rnd()%n),d); } } }
    if(ok)printf("codec ok\n");
    enum{ N=20000 }; static u64 ent[N]; static int have[N]; int stored=0, rej=0;
    for(int i=0;i<N;i++){ fill(p,i); u32 id; if(zs_put_page(p,&id)>0){ ent[i]=(u64)id<<12; have[i]=1; stored++; } else rej++; }
    int bad=0; for(int i=0;i<N;i++){ if(!have[i])continue; fill(p,i); u64 f=pframe_alloc(); if(swap_read(ent[i],f)||memcmp((void*)f,p,4096))bad++; pframe_free(f); }
    printf("stored %d rejected %d bad %d store pages %llu used %llu\n",stored,rej,bad,g_swap_store,g_swap_used); if(bad||stored<N/3)ok=0;
    for(int i=0;i<N;i+=2) if(have[i]){ if(swap_dup(ent[i]))ok=0; }
    for(int i=0;i<N;i++) if(have[i]&&(i%3==0)){ u64 f=pframe_alloc(); fill(p,i); if(swap_in(ent[i],f)||memcmp((void*)f,p,4096))ok=0; pframe_free(f); if(i%2){ have[i]=0; } }
    for(int i=0;i<N;i+=2) if(have[i]){ swap_put(ent[i]); if(i%3==0)have[i]=0; }
    for(int i=0;i<N;i++) if(have[i]){ fill(p,i); u64 f=pframe_alloc(); if(swap_read(ent[i],f)||memcmp((void*)f,p,4096)){ printf("FAIL after dup/put page %d\n",i); ok=0; } pframe_free(f); swap_put(ent[i]); have[i]=0; }
    printf("after free: used %llu store pages %llu bytes %llu frames %ld\n",g_swap_used,g_swap_store,g_swap_bytes,g_frames);
    if(g_swap_used||g_swap_store||g_swap_bytes||g_frames)ok=0;
    g_fail_at=g_frames+3; int got=0; for(int i=0;i<200;i++){ fill(p,3); u32 id; if(zs_put_page(p,&id)>0){ got++; swap_put((u64)id<<12); } } g_fail_at=-1;
    printf("with no frames: %d stored\n",got);
    u64 bogus=(u64)999999<<12; u64 f=pframe_alloc(); if(swap_read(bogus,f)!=-1||swap_dup(bogus)!=-1)ok=0; swap_put(bogus); pframe_free(f);
    printf(ok?"swap ok\n":"swap FAILED\n"); return ok?0:1; }
