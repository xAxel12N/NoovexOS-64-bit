#include "include/nvx/base.h"
#include "include/nvx/core/crash.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/perf.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/commands.h"

#define PF_MAXC 8
#define PF_RING 4096
#define PF_CS 6
#define PF_FDS 32

struct pf_sample { u64 ip, t; u32 pid, cpu; u8 user, n; u64 cs[PF_CS]; };
struct pf_fd { int used, ev, on, owner; u32 sfmt, rfmt; u64 base, acc, t_en, en_tot, id; int ctr, cpu; };

static int g_pf_init, g_pf_vendor, g_pf_nctr, g_pf_width, g_pf_ver; static u32 g_pf_mask, g_pf_veclen;
static u32 g_pf_used; static volatile int g_pf_samp, g_pf_sctr=-1, g_pf_scpu; static u64 g_pf_period;
static struct pf_sample g_pf_ring[PF_RING]; static volatile u32 g_pf_head; static struct pf_fd g_pf_fd[PF_FDS]; static u64 g_pf_nextid=1;
volatile u32 g_pf_nmis, g_pf_ticks;

static u32 pf_ctl(int i){ return g_pf_vendor==PERF_V_INTEL?0x186u+(u32)i:(g_pf_nctr==6?0xC0010200u+2u*(u32)i:0xC0010000u+(u32)i); }
static u32 pf_ctr(int i){ return g_pf_vendor==PERF_V_INTEL?0xC1u+(u32)i:(g_pf_nctr==6?0xC0010201u+2u*(u32)i:0xC0010004u+(u32)i); }
static u64 pf_mask(void){ return g_pf_width>=64?~0ULL:((1ULL<<g_pf_width)-1); }

int perf_init(void){
    if(g_pf_init)return g_pf_vendor!=PERF_V_NONE;
    g_pf_init=1;
    u32 a,b,c,d; cpu_cpuid(0,0,&a,&b,&c,&d); u32 maxl=a;
    int intel=b==0x756E6547u&&d==0x49656E69u&&c==0x6C65746Eu, amd=b==0x68747541u&&d==0x69746E65u&&c==0x444D4163u;
    if(intel&&maxl>=0xA){ cpu_cpuid(0xA,0,&a,&b,&c,&d); g_pf_ver=(int)(a&0xFF); int n=(int)((a>>8)&0xFF); g_pf_width=(int)((a>>16)&0xFF); g_pf_veclen=(a>>24)&0xFF; g_pf_mask=b;
        if(g_pf_ver>=1&&n>0&&g_pf_width>=32){ g_pf_vendor=PERF_V_INTEL; g_pf_nctr=n>PF_MAXC?PF_MAXC:n; } }
    else if(amd){ cpu_cpuid(0x80000000u,0,&a,&b,&c,&d); int ext=0; if(a>=0x80000001u){ cpu_cpuid(0x80000001u,0,&a,&b,&c,&d); ext=(c>>23)&1; } g_pf_vendor=PERF_V_AMD; g_pf_nctr=ext?6:4; g_pf_width=48; g_pf_ver=ext?2:1; g_pf_veclen=7; g_pf_mask=0; }
    if(g_pf_vendor!=PERF_V_NONE){ u64 v; if(cpu_rdmsr_safe(pf_ctl(0),&v)||cpu_wrmsr_safe(pf_ctr(0),0)||cpu_rdmsr_safe(pf_ctr(0),&v)){ g_pf_vendor=PERF_V_NONE; g_pf_nctr=0; } }
    klog(g_pf_vendor==PERF_V_INTEL?"perf: Intel architectural perfmon":g_pf_vendor==PERF_V_AMD?"perf: AMD core performance counters":"perf: no PMU (emulator or disabled), timer-tick sampler only");
    return g_pf_vendor!=PERF_V_NONE; }

int perf_hw(void){ perf_init(); return g_pf_vendor!=PERF_V_NONE; }
static int pf_ev_ok(int ev){ if(!perf_hw()||ev<0||ev>=PERF_EV_N)return 0; if(g_pf_vendor==PERF_V_INTEL)return perf_intel_avail(g_pf_mask,g_pf_veclen,ev); return 1; }

static volatile u32 g_pf_defer[NCPU];
static int pf_alloc(void){ for(int i=0;i<g_pf_nctr;i++){ u32 o=__atomic_load_n(&g_pf_used,__ATOMIC_ACQUIRE); if(!(o&(1u<<i))&&__atomic_compare_exchange_n(&g_pf_used,&o,o|(1u<<i),0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return i; } return -1; }
static void pf_global(int i,int on){ if(g_pf_vendor!=PERF_V_INTEL||g_pf_ver<2)return; u64 g=0; if(cpu_rdmsr_safe(0x38F,&g))return; if(on)g|=1ULL<<i; else g&=~(1ULL<<i); cpu_wrmsr_safe(0x38F,g); }
static void pf_start(int i,int ev,u64 init,int pmi){ cpu_wrmsr_safe(pf_ctl(i),0); cpu_wrmsr_safe(pf_ctr(i),init&pf_mask()); cpu_wrmsr_safe(pf_ctl(i),perf_ctl_bits(perf_evsel(g_pf_vendor,ev),pmi)); pf_global(i,1); }
static void pf_stop(int i){ if(i<0||i>=g_pf_nctr)return; cpu_wrmsr_safe(pf_ctl(i),0); pf_global(i,0); __atomic_fetch_and(&g_pf_used,~(1u<<i),__ATOMIC_RELEASE); }
static u64 pf_rd(int i){ u64 v=0; cpu_rdmsr_safe(pf_ctr(i),&v); return v&pf_mask(); }

static void pf_record(u64 ip,int user,u64 rbp){
    u32 h=__atomic_fetch_add(&g_pf_head,1,__ATOMIC_RELAXED); struct pf_sample* s=&g_pf_ring[h%PF_RING];
    s->ip=ip; s->t=hr_ns(); s->user=(u8)user; s->cpu=(u32)cpu_id(); int j=g_jcur; s->pid=(j>=0&&j<JMAX&&g_jp[j].state)?(u32)g_jp[j].pid:0;
    s->n=user?0:(u8)crash_bt(rbp,s->cs,PF_CS); }

int perf_nmi(regs_t* r){
    if(!g_pf_samp||g_pf_sctr<0||g_pf_vendor==PERF_V_NONE||cpu_id()!=g_pf_scpu)return 0;
    int i=g_pf_sctr, hit=0;
    if(g_pf_vendor==PERF_V_INTEL&&g_pf_ver>=2){ u64 st=0; if(cpu_rdmsr_safe(0x38E,&st))return 0; hit=(st>>i)&1; if(hit)cpu_wrmsr_safe(0x390,1ULL<<i); }
    else hit=!((pf_rd(i)>>(g_pf_width-1))&1);
    if(!hit)return 0;
    g_pf_nmis++; pf_record(r->rip,(r->cs&3)!=0,r->rbp);
    cpu_wrmsr_safe(pf_ctr(i),perf_reload(g_pf_period,g_pf_width)); lapic_wr(0x340,0x400);
    return 1; }

void perf_tick(regs_t* r){ if(!g_pf_samp||g_pf_sctr>=0)return; g_pf_ticks++; pf_record(r->rip,(r->cs&3)!=0,r->rbp); }

static int pf_samp_on(void){
    g_pf_head=0; g_pf_nmis=0; g_pf_ticks=0; g_pf_sctr=-1; g_pf_scpu=cpu_id();
    if(pf_ev_ok(PERF_EV_CYCLES)&&g_lapic_base|g_lapic_x2){ int i=pf_alloc(); if(i>=0){ u64 hz=g_hr.tsc_hz?g_hr.tsc_hz:1000000000ULL; g_pf_period=hz/1000; g_pf_sctr=i; lapic_wr(0x340,0x400); pf_start(i,PERF_EV_CYCLES,perf_reload(g_pf_period,g_pf_width),1); } }
    __atomic_store_n(&g_pf_samp,1,__ATOMIC_RELEASE); return g_pf_sctr>=0; }
static void pf_samp_off(void){ __atomic_store_n(&g_pf_samp,0,__ATOMIC_RELEASE); if(g_pf_sctr>=0){ pf_stop(g_pf_sctr); lapic_wr(0x340,0x10000); g_pf_sctr=-1; } }

long perf_ev_open(u32 type,u64 cfg,u32 sfmt,u32 rfmt,int disabled,int pid,int cpu,int owner){
    int ev=perf_map_attr(type,cfg); if(ev<0)return -2;
    if(pid==-1&&cpu==-1)return -22;
    if(g_uid!=0&&ev<PERF_EV_N)return -13;
    if(cpu>=0&&cpu!=cpu_id())return -22;
    if(pid==-1&&cpu>=0&&g_uid!=0)return -13;
    if(ev<PERF_EV_N&&!pf_ev_ok(ev))return -2;
    int k=-1; for(int i=0;i<PF_FDS;i++) if(!g_pf_fd[i].used){ k=i; break; } if(k<0)return -24;
    struct pf_fd* f=&g_pf_fd[k]; for(u32 q=0;q<sizeof(*f);q++)((u8*)f)[q]=0;
    f->ev=ev; f->sfmt=sfmt; f->rfmt=rfmt; f->owner=owner; f->ctr=-1; f->id=g_pf_nextid++; f->cpu=cpu_id();
    if(ev<PERF_EV_N){ f->ctr=pf_alloc(); if(f->ctr<0)return -16; pf_start(f->ctr,ev,0,0); }
    f->used=1; if(!disabled){ f->on=1; f->t_en=hr_ns(); f->base=f->ctr>=0?pf_rd(f->ctr):f->t_en; }
    return k; }

static u64 pf_val(struct pf_fd* f){ u64 v=f->acc; if(f->on){ u64 now=f->ctr>=0?pf_rd(f->ctr):hr_ns(); v+=f->ctr>=0?((now-f->base)&pf_mask()):now-f->base; } return v; }
int perf_ev_read(int i,u64* out,int cap){ if(i<0||i>=PF_FDS||!g_pf_fd[i].used)return -9; struct pf_fd* f=&g_pf_fd[i]; if(f->ctr>=0&&f->cpu!=cpu_id())return -5; u64 en=f->en_tot+(f->on?hr_ns()-f->t_en:0); return perf_read_fmt(f->rfmt,pf_val(f),en,en,f->id,(unsigned long long*)out,cap); }
int perf_ev_ctl(int i,u32 cmd,u64* io){
    if(i<0||i>=PF_FDS||!g_pf_fd[i].used)return -9;
    struct pf_fd* f=&g_pf_fd[i]; if(f->ctr>=0&&f->cpu!=cpu_id())return -5;
    if(cmd==0x2400){ if(!f->on){ f->on=1; f->t_en=hr_ns(); f->base=f->ctr>=0?pf_rd(f->ctr):f->t_en; } return 0; }
    if(cmd==0x2401){ if(f->on){ f->acc=pf_val(f); f->en_tot+=hr_ns()-f->t_en; f->on=0; } return 0; }
    if(cmd==0x2403){ f->acc=0; f->en_tot=0; if(f->on){ f->t_en=hr_ns(); f->base=f->ctr>=0?pf_rd(f->ctr):f->t_en; } return 0; }
    if(cmd==0x80082407u){ if(io)*io=f->id; return 0; }
    return -25; }
void perf_cpu_drain(void){ int c=cpu_id(); if(c<0||c>=NCPU)return; u32 m=__atomic_load_n(&g_pf_defer[c],__ATOMIC_ACQUIRE); for(int i=0;m&&i<32;i++) if(m&(1u<<i)){ pf_stop(i); __atomic_fetch_and(&g_pf_defer[c],~(1u<<i),__ATOMIC_RELEASE); } }
int perf_defer_pending(int c){ return c>=0&&c<NCPU?(int)__atomic_load_n(&g_pf_defer[c],__ATOMIC_ACQUIRE):0; }
void perf_ev_close(int i){ if(i<0||i>=PF_FDS||!g_pf_fd[i].used)return; struct pf_fd* f=&g_pf_fd[i]; int c=f->cpu;
    if(f->ctr>=0){ if(c==cpu_id()||c<0||c>=NCPU)pf_stop(f->ctr); else { __atomic_fetch_or(&g_pf_defer[c],1u<<f->ctr,__ATOMIC_ACQ_REL); smp_kick(c); for(int s=0;s<2000000&&(perf_defer_pending(c)&(1u<<f->ctr));s++)cpu_pause(); } }
    f->ctr=-1; f->used=0; }

static char* pf_u(char* p,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
static char* pf_s(char* p,const char* s){ while(*s)*p++=*s++; return p; }
static char* pf_pad(char* p,char* start,int w){ while(p-start<w)*p++=' '; return p; }

static const char* g_pf_names[PERF_EV_N]={"cycles","instructions","cache-references","cache-misses","branches","branch-misses"};

static void pf_stat(char* cmdline){
    perf_init(); int ctr[PERF_EV_N]; u64 v0[PERF_EV_N];
    for(int e=0;e<PERF_EV_N;e++){ ctr[e]=-1; if(pf_ev_ok(e)){ ctr[e]=pf_alloc(); if(ctr[e]>=0){ pf_start(ctr[e],e,0,0); v0[e]=pf_rd(ctr[e]); } } }
    int cpu=cpu_id(); u64 t0=hr_ns(); run_cmd(cmdline); u64 dt=hr_ns()-t0; u64 v[PERF_EV_N];
    for(int e=0;e<PERF_EV_N;e++) if(ctr[e]>=0){ v[e]=(pf_rd(ctr[e])-v0[e])&pf_mask(); pf_stop(ctr[e]); }
    char b[160],*p;
    p=pf_s(b,"\n Performance counter stats for '"); p=pf_s(p,cmdline); p=pf_s(p,"' (CPU "); p=pf_u(p,(u64)cpu); p=pf_s(p,", whole CPU, not per task):\n\n"); *p=0; term_puts(b);
    for(int e=0;e<PERF_EV_N;e++){ p=b; char* s=p; if(ctr[e]>=0)p=pf_u(p,v[e]); else p=pf_s(p,"<not supported>"); p=pf_pad(p,s,20); p=pf_s(p,"  "); p=pf_s(p,g_pf_names[e]);
        if(e==PERF_EV_INSTR&&ctr[e]>=0&&ctr[0]>=0&&v[0]){ u64 x=v[1]*100/v[0]; p=pf_s(p,"   # "); p=pf_u(p,x/100); *p++='.'; *p++=(char)('0'+x/10%10); *p++=(char)('0'+x%10); p=pf_s(p," insn per cycle"); }
        if((e==PERF_EV_CMISS||e==PERF_EV_BMISS)&&ctr[e]>=0&&ctr[e-1]>=0&&v[e-1]){ u64 x=v[e]*10000/v[e-1]; p=pf_s(p,"   # "); p=pf_u(p,x/100); *p++='.'; *p++=(char)('0'+x/10%10); *p++=(char)('0'+x%10); p=pf_s(p," %"); }
        *p++='\n'; *p=0; term_puts(b); }
    p=pf_s(b,"\n "); p=pf_u(p,dt/1000000000ULL); *p++='.'; { u64 f=dt%1000000000ULL/1000; char t[7]; for(int q=5;q>=0;q--){ t[q]=(char)('0'+f%10); f/=10; } t[6]=0; p=pf_s(p,t); } p=pf_s(p," seconds time elapsed\n"); *p=0; term_puts(b);
    if(!perf_hw())term_puts(" no hardware PMU here (QEMU TCG or PMU hidden by the hypervisor): only the time is measured\n"); }

struct pf_hent { u64 key; const char* name; u32 n, pid; u8 user; };

static void pf_top(int secs){
    if(secs<1)secs=5;
    if(secs>60)secs=60;
    int hw=pf_samp_on(); term_puts(hw?"perf top: sampling cycles via PMU overflow NMI at ~1 kHz on this CPU ...\n":"perf top: no PMU, sampling CPU 0 from the timer tick instead ...\n");
    extern void ui_pump(void); u64 end=hr_ns()+(u64)secs*1000000000ULL; while(hr_ns()<end){ ui_pump(); hr_delay_ms(10); }
    pf_samp_off();
    static struct pf_hent h[256]; int nh=0; u32 n=g_pf_head; if(n>PF_RING)n=PF_RING; u32 tot=0, other=0;
    for(u32 i=0;i<n;i++){ struct pf_sample* s=&g_pf_ring[i]; const char* nm=0; u64 off=0, key;
        if(s->user)key=0x8000000000000000ULL|s->pid; else if(kname_find(s->ip,&nm,&off)==0||!nm)key=s->ip&~0xFFFULL; else key=s->ip-off;
        tot++; int k=-1; for(int j=0;j<nh;j++) if(h[j].key==key){ k=j; break; }
        if(k<0){ if(nh>=256){ other++; continue; } k=nh++; h[k].key=key; h[k].name=s->user?0:nm; h[k].n=0; h[k].pid=s->pid; h[k].user=s->user; }
        h[k].n++; }
    for(int i=0;i<nh;i++) for(int j=i+1;j<nh;j++) if(h[j].n>h[i].n){ struct pf_hent t=h[i]; h[i]=h[j]; h[j]=t; }
    char b[200],*p; p=pf_s(b,"Samples: "); p=pf_u(p,tot); if(g_pf_head>PF_RING){ p=pf_s(p," (ring wrapped, "); p=pf_u(p,g_pf_head-PF_RING); p=pf_s(p," dropped)"); } p=pf_s(p,hw?"  event: cycles (PMI)\n":"  event: cpu-clock (timer tick)\n"); *p=0; term_puts(b);
    if(!tot){ term_puts("  no samples\n"); return; }
    for(int i=0;i<nh&&i<20;i++){ p=b; u64 x=(u64)h[i].n*10000/tot; char* s=p; p=pf_u(p,x/100); *p++='.'; *p++=(char)('0'+x/10%10); *p++=(char)('0'+x%10); *p++='%'; p=pf_pad(p,s,9);
        if(h[i].user){ p=pf_s(p,"[user] pid "); p=pf_u(p,h[i].pid); int jj=-1; for(int q=0;q<JMAX;q++) if(g_jp[q].state&&(u32)g_jp[q].pid==h[i].pid){ jj=q; break; } if(jj>=0){ *p++=' '; int c=0; while(g_jp[jj].cmd[c]&&c<40)*p++=g_jp[jj].cmd[c++]; } }
        else { p=pf_s(p,"[kernel] "); if(h[i].name){ int c=0; while(h[i].name[c]&&c<80)*p++=h[i].name[c++]; } else { p=pf_s(p,"0x"); for(int q=60;q>=0;q-=4)*p++="0123456789abcdef"[(h[i].key>>q)&15]; } }
        *p++='\n'; *p=0; term_puts(b); }
    if(other){ p=pf_s(b,"  + "); p=pf_u(p,other); p=pf_s(p," samples in other symbols\n"); *p=0; term_puts(b); } }

static void pf_info(void){
    perf_init(); char b[200],*p=b;
    p=pf_s(p,g_pf_vendor==PERF_V_INTEL?"PMU: Intel architectural perfmon v":g_pf_vendor==PERF_V_AMD?"PMU: AMD core PMCs (PerfCtrExtCore ":"PMU: none (QEMU TCG or hidden): perf top uses the timer tick, perf stat measures time only\n");
    if(g_pf_vendor!=PERF_V_NONE){ p=pf_u(p,(u64)g_pf_ver); if(g_pf_vendor==PERF_V_AMD)*p++=')'; p=pf_s(p,", "); p=pf_u(p,(u64)g_pf_nctr); p=pf_s(p," counters, "); p=pf_u(p,(u64)g_pf_width); p=pf_s(p," bits, overflow -> LAPIC LVT PMI as NMI\n"); }
    *p=0; term_puts(b);
    for(int e=0;e<PERF_EV_N&&g_pf_vendor!=PERF_V_NONE;e++){ p=pf_s(b,"  "); p=pf_s(p,g_pf_names[e]); p=pf_s(p,pf_ev_ok(e)?"\n":"  (not available)\n"); *p=0; term_puts(b); }
    p=pf_s(b,"  counters in use: "); { u64 pc=0; for(u64 v=(u64)g_pf_used; v; v&=v-1) pc++; p=pf_u(p,pc); } p=pf_s(p,", NMIs taken: "); p=pf_u(p,g_pf_nmis); *p++='\n'; *p=0; term_puts(b); }

void perf_cmd(const char* a){
    while(*a==' ')a++;
    if(a[0]=='s'&&a[1]=='t'&&a[2]=='a'&&a[3]=='t'&&(a[4]==' '||!a[4])){ a+=4; while(*a==' ')a++; if(!*a){ term_puts("usage: perf stat <command>\n"); return; } char cl[480]; int k=0; while(a[k]&&k<479){ cl[k]=a[k]; k++; } cl[k]=0; pf_stat(cl); return; }
    if(a[0]=='t'&&a[1]=='o'&&a[2]=='p'&&(a[3]==' '||!a[3])){ if(g_uid!=0){ term_puts("perf top: kernel symbols are shown to root only (use sudo)\n"); return; } a+=3; while(*a==' ')a++; int s=0; while(*a>='0'&&*a<='9'&&s<1000)s=s*10+(*a++-'0'); pf_top(s); return; }
    if(!*a||(a[0]=='i'&&a[1]=='n'&&a[2]=='f'&&a[3]=='o')){ pf_info(); return; }
    term_puts("usage: perf info | perf stat <command> | perf top [seconds]\n"); }
