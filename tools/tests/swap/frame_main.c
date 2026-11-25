#include <stdio.h>
typedef unsigned int u32; typedef unsigned long long u64;
static long g_free, g_calls, g_clear_calls, g_cycle_len=3, g_steal; static u64 g_cyc;
static u64 pframe_alloc(void){ if(g_free>0){ g_free--; return 0x1000; } return 0; }
static u32 mem_reclaim(u32 want){ g_calls++; if(g_calls%g_cycle_len==0)g_cyc++; if(g_calls<=g_clear_calls)return 0; if(g_clear_calls<0)return 0; if(g_steal)return want; g_free+=want; return want; }
static u64 swap_cycles(void){ return g_cyc; }
int g_sr_nolock[64]; static int cpu_id(void){ return 0; } static u32 pframe_free_count(void){ return (u32)(g_free>0?g_free:0); }
/*FRAMECODE*/
static int run(const char* what,long clear,long cyc,long steal,int want_ok,u32 maxr){
    g_free=0; g_calls=0; g_clear_calls=clear; g_cycle_len=cyc; g_steal=steal; g_cyc=0; u64 f=vm_frame(); int ok=(!!f==want_ok)&&g_vm_frame_rounds<=maxr;
    printf("%s %s: frame %s after %u rounds\n",ok?"ok":"FAIL",what,f?"found":"none",g_vm_frame_rounds); return ok; }
int main(void){
    int ok=1;
    ok&=run("reclaim works at once",0,3,0,1,1);
    ok&=run("second-chance pass gives nothing for 10 calls, then pages come back",10,6,0,1,11);
    ok&=run("long first scan cycle: 20 empty calls, cycle of 12 calls",20,12,0,1,21);
    ok&=run("nothing reclaimable: gives up after two empty cycles",-1,3,0,0,64);
    ok&=run("nothing reclaimable and no cycle progress: bounded",-1,1000000,0,0,64);
    ok&=run("reclaim reports progress but frames are taken: bounded",0,3,1,0,512);
    { g_sr_nolock[0]=1; g_free=10; g_calls=0; g_clear_calls=0; g_cycle_len=3; g_steal=0; u64 f=vm_frame(); int k=f&&g_calls==1&&g_free==73&&g_vm_kres_fills==1;
      printf("%s user fault below the kernel reserve reclaims first: %ld calls, %ld free left\n",k?"ok":"FAIL",g_calls,g_free); ok&=k;
      g_free=10; g_calls=0; g_clear_calls=-1; long n=0; for(int i=0;i<10;i++)n+=!!vm_frame(); k=n==10&&g_calls==1;
      printf("%s nothing reclaimable: the reserve top-up backs off (%ld reclaim calls for 10 faults)\n",k?"ok":"FAIL",g_calls); ok&=k;
      g_sr_nolock[0]=0; g_free=10; g_calls=0; g_clear_calls=0; f=vm_frame(); k=f&&g_calls==0; printf("%s kernel-mode fault uses the reserve without reclaim\n",k?"ok":"FAIL"); ok&=k; }
    printf(ok?"frame ok\n":"frame FAILED\n"); return ok?0:1; }
