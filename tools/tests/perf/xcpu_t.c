#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "include/nvx/core/perf_codec.h"
typedef unsigned long long u64; typedef unsigned int u32; typedef unsigned char u8;
#define NCPU 16
#define PF_RING 16
#define PF_CS 6
#define PF_FDS 32
static __thread int t_cpu;
static int cpu_id(void){ return t_cpu; }
static void cpu_pause(void){ sched_yield(); }
static u64 g_msr[NCPU][8], g_glob[NCPU]; static volatile int g_kicked[NCPU], g_alive[NCPU];
static int mi(u32 a){ return a>=0x186&&a<0x18E?(int)(a-0x186):a>=0xC1&&a<0xC9?4+(int)((a-0xC1)&3):-1; }
static int cpu_rdmsr_safe(u32 a,u64* v){ if(a==0x38F){ *v=g_glob[t_cpu]; return 0; } int k=mi(a); if(k<0)return 1; *v=g_msr[t_cpu][k]; return 0; }
static int cpu_wrmsr_safe(u32 a,u64 v){ if(a==0x38F){ g_glob[t_cpu]=v; return 0; } int k=mi(a); if(k<0)return 1; g_msr[t_cpu][k]=v; return 0; }
static void smp_kick(int c){ __atomic_store_n(&g_kicked[c],1,__ATOMIC_RELEASE); }
#include "xcpu_ext.c"
static int fails, checks;
#define CK(c,msg) do{ checks++; if(c)printf("ok %s\n",msg); else { fails++; printf("FAIL %s (line %d)\n",msg,__LINE__); } }while(0)
static void* cpu1(void* a){ (void)a; t_cpu=1; while(__atomic_load_n(&g_alive[1],__ATOMIC_ACQUIRE)){ if(__atomic_exchange_n(&g_kicked[1],0,__ATOMIC_ACQ_REL))perf_cpu_drain(); sched_yield(); } return 0; }
static int open_on(int cpu,int ev){ int sv=t_cpu; t_cpu=cpu; int k=-1; for(int i=0;i<PF_FDS;i++) if(!g_pf_fd[i].used){ k=i; break; } struct pf_fd* f=&g_pf_fd[k]; memset(f,0,sizeof *f); f->ctr=pf_alloc(); if(f->ctr<0){ t_cpu=sv; return -1; } f->cpu=cpu; pf_start(f->ctr,ev,0,0); f->used=1; t_cpu=sv; return k; }
int main(void){
    g_pf_vendor=PERF_V_INTEL; g_pf_ver=2; g_pf_nctr=4; g_pf_width=48; t_cpu=0;
    int a=open_on(0,PERF_EV_CYCLES), b=open_on(1,PERF_EV_INSTR);
    int ca=g_pf_fd[a].ctr, cb=g_pf_fd[b].ctr;
    CK(a>=0&&b>=0&&ca!=cb,"two counters allocated");
    CK((g_glob[0]>>ca&1)&&(g_glob[1]>>cb&1)&&g_msr[1][cb]!=0,"each counter enabled on its own cpu");
    g_alive[1]=1; pthread_t th; pthread_create(&th,0,cpu1,0);
    perf_ev_close(b);
    CK(!g_pf_fd[b].used,"fd released");
    CK(g_msr[1][cb]==0&&!(g_glob[1]>>cb&1),"close from cpu0 stops the counter on owner cpu1");
    CK(g_msr[0][ca]!=0&&(g_glob[0]>>ca&1),"cpu0 counter untouched by remote close");
    CK(!(__atomic_load_n(&g_pf_used,__ATOMIC_ACQUIRE)>>cb&1),"counter slot freed after remote stop");
    CK(!perf_defer_pending(1),"no pending stop left");
    g_alive[1]=0; pthread_join(th,0);
    int c=open_on(1,PERF_EV_BRANCH); int cc=g_pf_fd[c].ctr;
    perf_ev_close(c);
    CK(!g_pf_fd[c].used,"close returns when owner cpu does not answer (bounded wait)");
    CK((__atomic_load_n(&g_pf_used,__ATOMIC_ACQUIRE)>>cc&1)&&perf_defer_pending(1),"slot stays reserved until the owner stops it (fail closed)");
    int d=open_on(0,PERF_EV_CYCLES); CK(d>=0&&g_pf_fd[d].ctr!=cc&&g_pf_fd[d].ctr!=ca,"reserved slot not handed out again");
    t_cpu=1; perf_cpu_drain(); t_cpu=0;
    CK(g_msr[1][cc]==0&&!(g_glob[1]>>cc&1)&&!(g_pf_used>>cc&1),"owner drains the deferred stop on its next tick");
    perf_ev_close(a); perf_ev_close(d);
    CK(g_msr[0][ca]==0&&!(g_glob[0]>>ca&1),"local close stops immediately");
    CK(g_pf_used==0,"all slots free");
    printf("%d/%d %s\n",checks-fails,checks,fails?"PERF XCPU FAIL":"PERF XCPU all ok"); return fails!=0; }
