#include "include/nvx/base.h"
#include "include/nvx/sys/bpf/bpf_attach.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/sys/linux/lxsock.h"

void kidle(void);

volatile int g_bpf_tp_any, g_bpf_sock_any;
static struct bpf_prog* volatile g_bpf_tp[BPF_TP_N][BPF_TP_SLOTS];
static volatile int g_bpf_tp_busy[BPF_TP_N];
static volatile u8 g_bpf_inrun[NCPU];
static volatile int g_bpfa_lk;

static int bpfa_lock(void){ for(u32 i=0;i<100000000u;i++){ if(!__atomic_exchange_n(&g_bpfa_lk,1,__ATOMIC_ACQUIRE))return 1; __builtin_ia32_pause(); } return 0; }
static void bpfa_unlock(void){ __atomic_store_n(&g_bpfa_lk,0,__ATOMIC_RELEASE); }

#define BPFA_TRN 64
static char g_bpfa_tr[BPFA_TRN][96]; static volatile u32 g_bpfa_trh;

static char* bpfa_u64(char* o,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*o++=t[--n]; return o; }
static char* bpfa_hex(char* o,u64 v){ char t[20]; int n=0; do{ t[n++]="0123456789abcdef"[v&15]; v>>=4; }while(v); while(n)*o++=t[--n]; return o; }

static long bpfa_printk(const char* f,u32 fl,u64 a,u64 b,u64 c){
    if(!f||fl==0||fl>128)return -22;
    char out[96]; char* o=out; char* e=out+96-24; u64 av[3]={a,b,c}; int ai=0;
    for(u32 i=0;i<fl&&f[i]&&o<e;i++){
        if(f[i]!='%'){ if(f[i]!='\n')*o++=f[i]; continue; }
        u32 j=i+1; int lng=0; while(j<fl&&f[j]=='l'&&lng<2){ lng++; j++; }
        if(j>=fl)break;
        char t=f[j];
        if(t=='%'){ *o++='%'; i=j; continue; }
        if(ai>=3||(t!='d'&&t!='u'&&t!='x'&&t!='i'))return -22;
        u64 v=av[ai++]; if(lng==0)v=(t=='d'||t=='i')?(u64)(i64)(int)v:(u32)v;
        if((t=='d'||t=='i')&&(i64)v<0){ *o++='-'; v=(u64)-(i64)v; }
        o=t=='x'?bpfa_hex(o,v):bpfa_u64(o,v); i=j; }
    *o=0;
    u32 h=__atomic_fetch_add(&g_bpfa_trh,1,__ATOMIC_RELAXED)%BPFA_TRN;
    char* d=g_bpfa_tr[h]; int k=0; while(out[k]&&k<95){ d[k]=out[k]; k++; } d[k]=0;
    return (long)(o-out); }

static int bpfa_kmapped(u64 a,u64 n){
    if(a<0x1000||a+n<a)return 0;
    for(u64 p=a&~0xFFFULL;p<a+n;p+=4096) if(!kva_to_pa(p))return 0;
    return 1; }

static u64 bh_probe(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ (void)a4; (void)a5;
    u8* d=(u8*)a1; if(a2>512)return (u64)-22;
    if(!bpfa_kmapped(a3,a2)){ for(u64 i=0;i<a2;i++)d[i]=0; return (u64)-14; }
    const u8* s=(const u8*)a3; for(u64 i=0;i<a2;i++)d[i]=s[i];
    return 0; }
static u64 bh_ktime(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; return hr_ns(); }
static u64 bh_printk(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ return (u64)bpfa_printk((const char*)a1,(u32)a2,a3,a4,a5); }
static u64 bh_rand(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; u32 r; csprng_bytes((u8*)&r,4); return r; }
static u64 bh_pid(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; int j=g_jcur; if(j<0||j>=JMAX)return 0; struct jproc* p=&g_jp[j]; return ((u64)(u32)jc_lead(p)->pid<<32)|(u32)p->pid; }
static u64 bh_uid(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; int j=g_jcur; if(j<0||j>=JMAX)return 0; return ((u64)(u32)g_jp[j].gid<<32)|(u32)g_jp[j].uid; }
static u64 bh_comm(u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){ (void)a3; (void)a4; (void)a5;
    char* d=(char*)a1; if(!a2||a2>512)return (u64)-22;
    int j=g_jcur; const char* s=(j>=0&&j<JMAX)?g_jp[j].cmd:"kernel"; u64 i=0;
    for(;i+1<a2&&i<47&&s[i]&&s[i]!=' ';i++)d[i]=s[i];
    for(;i<a2;i++)d[i]=0;
    return 0; }
#define BH_TP (BPF_TYPE_BIT(BPF_PROG_TYPE_TRACEPOINT)|BPF_TYPE_BIT(BPF_PROG_TYPE_RAW_TRACEPOINT)|BPF_TYPE_BIT(BPF_PROG_TYPE_KPROBE))
static const struct bpf_helper g_bpfa_helpers[]={
    {BPF_FUNC_probe_read,BH_TP,bh_probe,BPF_RET_INT,{BPF_ARG_MEM_WR,BPF_ARG_SIZE,BPF_ARG_ANY,0,0}},
    {BPF_FUNC_probe_read_kernel,BH_TP,bh_probe,BPF_RET_INT,{BPF_ARG_MEM_WR,BPF_ARG_SIZE,BPF_ARG_ANY,0,0}},
    {BPF_FUNC_ktime_get_ns,BPF_TYPES_ALL,bh_ktime,BPF_RET_INT,{0,0,0,0,0}},
    {BPF_FUNC_trace_printk,BPF_TYPES_ALL,bh_printk,BPF_RET_INT,{BPF_ARG_MEM_RD,BPF_ARG_SIZE,BPF_ARG_ANY,BPF_ARG_ANY,BPF_ARG_ANY}},
    {BPF_FUNC_get_prandom_u32,BPF_TYPES_ALL,bh_rand,BPF_RET_INT,{0,0,0,0,0}},
    {BPF_FUNC_get_current_pid_tgid,BH_TP,bh_pid,BPF_RET_INT,{0,0,0,0,0}},
    {BPF_FUNC_get_current_uid_gid,BH_TP,bh_uid,BPF_RET_INT,{0,0,0,0,0}},
    {BPF_FUNC_get_current_comm,BH_TP,bh_comm,BPF_RET_INT,{BPF_ARG_MEM_WR,BPF_ARG_SIZE,0,0,0}},
};
static volatile int g_bpfa_inited;
int bpf_tp_detach_id(int id);
void bpf_attach_init(void){
    if(__atomic_exchange_n(&g_bpfa_inited,1,__ATOMIC_ACQ_REL))return;
    for(u32 i=0;i<sizeof(g_bpfa_helpers)/sizeof(g_bpfa_helpers[0]);i++)bpf_helper_register(&g_bpfa_helpers[i]);
    bpf_link_hook(bpf_tp_detach_id); }

int bpf_tp_by_name(const char* n){
    if(!n)return -22;
    const char* c=n; for(const char* q=n;*q;q++) if(*q==':'||*q=='/')c=q+1;
    const char* t[BPF_TP_N]={"sys_enter","sys_exit","sched_switch"};
    for(int i=0;i<BPF_TP_N;i++){ const char* a=c; const char* b=t[i]; while(*a&&*a==*b){ a++; b++; } if(!*a&&!*b)return i; }
    return -2; }

int bpf_tp_attach(int tp,struct bpf_prog* prog){
    if(tp<0||tp>=BPF_TP_N||!prog)return -22;
    if(!bpfa_lock())return -16;
    for(int s=0;s<BPF_TP_SLOTS;s++) if(!g_bpf_tp[tp][s]){ bpf_prog_get(prog); __atomic_store_n(&g_bpf_tp[tp][s],prog,__ATOMIC_RELEASE); g_bpf_tp_any=1; bpfa_unlock(); return tp*BPF_TP_SLOTS+s; }
    bpfa_unlock(); return -28; }

static void bpfa_recount(void){ int a=0; for(int t=0;t<BPF_TP_N;t++) for(int s=0;s<BPF_TP_SLOTS;s++) if(g_bpf_tp[t][s])a=1; g_bpf_tp_any=a; }

int bpf_tp_detach_id(int id){
    if(id<0||id>=BPF_TP_N*BPF_TP_SLOTS)return -22;
    int tp=id/BPF_TP_SLOTS, s=id%BPF_TP_SLOTS;
    if(!bpfa_lock())return -16;
    struct bpf_prog* p=__atomic_exchange_n(&g_bpf_tp[tp][s],(struct bpf_prog*)0,__ATOMIC_ACQ_REL); bpfa_recount(); bpfa_unlock();
    if(!p)return -2;
    for(u32 i=0;i<50000000u;i++){ if(!__atomic_load_n(&g_bpf_tp_busy[tp],__ATOMIC_ACQUIRE)){ bpf_prog_put(p); return 0; } __builtin_ia32_pause(); }
    klog("bpf: detach timed out, program kept");
    return 0; }

static void bpfa_fire(int tp,void* ctx){
    int cpu=cpu_id(); if(cpu<0||cpu>=NCPU||g_bpf_inrun[cpu])return; g_bpf_inrun[cpu]=1;
    __atomic_fetch_add(&g_bpf_tp_busy[tp],1,__ATOMIC_ACQ_REL);
    for(int s=0;s<BPF_TP_SLOTS;s++){ struct bpf_prog* p=__atomic_load_n(&g_bpf_tp[tp][s],__ATOMIC_ACQUIRE); if(p)bpf_prog_run(p,ctx); }
    __atomic_fetch_sub(&g_bpf_tp_busy[tp],1,__ATOMIC_ACQ_REL);
    g_bpf_inrun[cpu]=0; }

static void bpfa_zero(void* d,u32 n){ u8* p=(u8*)d; for(u32 i=0;i<n;i++)p[i]=0; }

union bpfa_tctx { struct bpf_trace_ctx t; struct bpf_sched_ctx c; u8 pad[sizeof(struct bpf_trace_ctx)+64]; };
void bpf_tp_sys_enter_hit(u64 nr,const u64* a){
    union bpfa_tctx u; bpfa_zero(&u,sizeof u);
    u.t.nr=nr; for(int i=0;i<6;i++)u.t.args[i]=a[i];
    u.t.ts=hr_ns(); bpfa_fire(BPF_TP_SYS_ENTER,&u); }
void bpf_tp_sys_exit_hit(u64 nr,long ret){
    union bpfa_tctx u; bpfa_zero(&u,sizeof u);
    u.t.nr=nr; u.t.ret=(u64)ret; u.t.ts=hr_ns(); bpfa_fire(BPF_TP_SYS_EXIT,&u); }
static void bpfa_comm(char* d,int j){ const char* s=(j>=0&&j<JMAX)?g_jp[j].cmd:"swapper"; int i=0; for(;i<15&&s[i]&&s[i]!=' ';i++)d[i]=s[i]; for(;i<16;i++)d[i]=0; }
static int g_bpfa_last[NCPU];
void bpf_tp_sched_switch_hit(int next){
    int cpu=cpu_id(); if(cpu<0||cpu>=NCPU)return;
    int prev=g_bpfa_last[cpu]-1; g_bpfa_last[cpu]=next+1;
    union bpfa_tctx u; bpfa_zero(&u,sizeof u);
    bpfa_comm(u.c.prev_comm,prev); bpfa_comm(u.c.next_comm,next); u.c.cpu=(u32)cpu;
    if(prev>=0&&prev<JMAX){ u.c.prev_pid=(u32)g_jp[prev].pid; u.c.prev_state=g_jp[prev].state==S_RUN?0u:1u; }
    if(next>=0&&next<JMAX)u.c.next_pid=(u32)g_jp[next].pid;
    bpfa_fire(BPF_TP_SCHED_SWITCH,&u); }

struct bpfa_sf { u8 kind; u32 n; void* f; };
static struct bpfa_sf g_bpfa_sf[NLXS];
static volatile int g_bpfa_sf_busy[NLXS];

static void bpfa_sf_drop(int sid,struct bpfa_sf o){
    if(!o.kind||!o.f)return;
    for(u32 i=0;i<50000000u;i++){ if(!__atomic_load_n(&g_bpfa_sf_busy[sid],__ATOMIC_ACQUIRE)){ if(o.kind==1)kfree(o.f); else bpf_prog_put((struct bpf_prog*)o.f); return; } __builtin_ia32_pause(); }
    klog("bpf: socket filter still in use, old filter kept"); }
static struct bpfa_sf bpfa_sf_take(struct bpfa_sf* s){ struct bpfa_sf o=*s; s->kind=0; s->f=0; s->n=0; return o; }
static void bpfa_sf_any(void){ int a=0; for(int i=0;i<NLXS;i++) if(g_bpfa_sf[i].kind)a=1; g_bpf_sock_any=a; }

int bpf_sock_attach_cbpf(int sid,const void* insns,u32 n){
    if(sid<0||sid>=NLXS)return -9;
    int e=cbpf_check(insns,n); if(e)return e;
    void* c=kmalloc((unsigned long)n*8); if(!c)return -12;
    const u8* s=(const u8*)insns; u8* d=(u8*)c; for(u32 i=0;i<n*8;i++)d[i]=s[i];
    if(!bpfa_lock()){ kfree(c); return -16; } struct bpfa_sf o=bpfa_sf_take(&g_bpfa_sf[sid]); g_bpfa_sf[sid].f=c; g_bpfa_sf[sid].n=n; g_bpfa_sf[sid].kind=1; g_bpf_sock_any=1; bpfa_unlock();
    bpfa_sf_drop(sid,o); return 0; }
int bpf_sock_attach_fd(int sid,int fd){
    if(sid<0||sid>=NLXS)return -9;
    struct bpf_prog* p=bpf_prog_by_fd(fd); if(!p)return -22; if(p->type!=BPF_PROG_TYPE_SOCKET_FILTER){ bpf_prog_put(p); return -22; }
    if(!bpfa_lock()){ bpf_prog_put(p); return -16; } struct bpfa_sf o=bpfa_sf_take(&g_bpfa_sf[sid]); g_bpfa_sf[sid].f=p; g_bpfa_sf[sid].kind=2; g_bpf_sock_any=1; bpfa_unlock();
    bpfa_sf_drop(sid,o); return 0; }
int bpf_sock_detach(int sid){
    if(sid<0||sid>=NLXS)return -9;
    if(!bpfa_lock())return -16;
    struct bpfa_sf o=bpfa_sf_take(&g_bpfa_sf[sid]); bpfa_sf_any(); bpfa_unlock();
    bpfa_sf_drop(sid,o); return o.kind?0:-2; }
int bpf_sock_filter(int sid,const u8* pkt,int len){
    if(sid<0||sid>=NLXS||len<0)return len;
    if(!__atomic_load_n(&g_bpfa_sf[sid].kind,__ATOMIC_ACQUIRE))return len;
    if(!bpfa_lock())return 0;
    struct bpfa_sf s=g_bpfa_sf[sid]; if(s.kind)__atomic_fetch_add(&g_bpfa_sf_busy[sid],1,__ATOMIC_ACQ_REL); bpfa_unlock();
    if(!s.kind)return len;
    u32 r;
    if(s.kind==1)r=cbpf_run(s.f,s.n,pkt,(u32)len);
    else { struct bpf_skb_ctx c; bpfa_zero(&c,sizeof c); c.len=(u32)len; c.protocol=0x0800; c.data=pkt; r=(u32)bpf_prog_run((struct bpf_prog*)s.f,&c); }
    __atomic_fetch_sub(&g_bpfa_sf_busy[sid],1,__ATOMIC_ACQ_REL);
    return r<(u32)len?(int)r:len; }

struct bpfa_insn { u8 code; u8 regs; short off; int imm; };
#define BA_I(c,d,s,o,i) {(u8)(c),(u8)((d)|((s)<<4)),(short)(o),(int)(i)}
#define BA_LDX_DW(d,s,o) BA_I(0x79,d,s,o,0)
#define BA_LDX_W(d,s,o) BA_I(0x61,d,s,o,0)
#define BA_STX_DW(d,s,o) BA_I(0x7b,d,s,o,0)
#define BA_STX_W(d,s,o) BA_I(0x63,d,s,o,0)
#define BA_ST_DW(d,o,i) BA_I(0x7a,d,0,o,i)
#define BA_MOV(d,s) BA_I(0xbf,d,s,0,0)
#define BA_MOVI(d,i) BA_I(0xb7,d,0,0,i)
#define BA_ADDI(d,i) BA_I(0x07,d,0,0,i)
#define BA_ADD(d,s) BA_I(0x0f,d,s,0,0)
#define BA_SUB(d,s) BA_I(0x1f,d,s,0,0)
#define BA_RSHI(d,i) BA_I(0x77,d,0,0,i)
#define BA_JGTI(d,i,o) BA_I(0x25,d,0,o,i)
#define BA_JEQI(d,i,o) BA_I(0x15,d,0,o,i)
#define BA_JNEI(d,i,o) BA_I(0x55,d,0,o,i)
#define BA_JA(o) BA_I(0x05,0,0,o,0)
#define BA_MAP(d,idx) BA_I(0x18,d,BPF_PSEUDO_MAP_IDX,0,idx), BA_I(0,0,0,0,0)
#define BA_CALL(f) BA_I(0x85,0,0,0,f)
#define BA_EXIT() BA_I(0x95,0,0,0,0)

static const struct bpfa_insn g_bpfa_syscount[]={
    BA_LDX_DW(6,1,0), BA_JGTI(6,511,10), BA_STX_W(10,6,-4), BA_MAP(1,0), BA_MOV(2,10), BA_ADDI(2,-4), BA_CALL(1), BA_JEQI(0,0,3),
    BA_LDX_DW(1,0,0), BA_ADDI(1,1), BA_STX_DW(0,1,0), BA_MOVI(0,0), BA_EXIT() };
static const struct bpfa_insn g_bpfa_exec[]={
    BA_LDX_DW(6,1,0), BA_JEQI(6,59,1), BA_JNEI(6,322,15),
    BA_CALL(14), BA_STX_DW(10,0,-32), BA_CALL(5), BA_STX_DW(10,0,-24), BA_MOV(1,10), BA_ADDI(1,-16), BA_MOVI(2,16), BA_CALL(16),
    BA_MAP(1,0), BA_MOV(2,10), BA_ADDI(2,-32), BA_MOVI(3,32), BA_MOVI(4,0), BA_CALL(130),
    BA_MOVI(0,0), BA_EXIT() };
static const struct bpfa_insn g_bpfa_lat_in[]={
    BA_CALL(14), BA_STX_DW(10,0,-8), BA_CALL(5), BA_STX_DW(10,0,-16),
    BA_MAP(1,0), BA_MOV(2,10), BA_ADDI(2,-8), BA_MOV(3,10), BA_ADDI(3,-16), BA_MOVI(4,0), BA_CALL(2),
    BA_MOVI(0,0), BA_EXIT() };
static const struct bpfa_insn g_bpfa_lat_out[]={
    BA_CALL(14), BA_STX_DW(10,0,-8), BA_MAP(1,0), BA_MOV(2,10), BA_ADDI(2,-8), BA_CALL(1), BA_JEQI(0,0,40),
    BA_LDX_DW(7,0,0), BA_CALL(5), BA_SUB(0,7), BA_RSHI(0,10), BA_MOV(7,0), BA_MOVI(8,0),
    BA_JGTI(7,0xFFFF,1), BA_JA(2), BA_RSHI(7,16), BA_ADDI(8,16),
    BA_JGTI(7,0xFF,1), BA_JA(2), BA_RSHI(7,8), BA_ADDI(8,8),
    BA_JGTI(7,0xF,1), BA_JA(2), BA_RSHI(7,4), BA_ADDI(8,4),
    BA_JGTI(7,3,1), BA_JA(2), BA_RSHI(7,2), BA_ADDI(8,2),
    BA_JGTI(7,1,1), BA_JA(1), BA_ADDI(8,1),
    BA_STX_W(10,8,-12), BA_MAP(1,1), BA_MOV(2,10), BA_ADDI(2,-12), BA_CALL(1), BA_JEQI(0,0,3),
    BA_LDX_DW(1,0,0), BA_ADDI(1,1), BA_STX_DW(0,1,0),
    BA_MAP(1,0), BA_MOV(2,10), BA_ADDI(2,-8), BA_CALL(3),
    BA_MOVI(0,0), BA_EXIT() };
#define BPFA_N(a) ((u32)(sizeof(a)/sizeof((a)[0])))

static void bpfa_puts(const char* s){ term_puts(s); }
static char g_bpfa_log[512];
static struct bpf_prog* bpfa_load(const struct bpfa_insn* p,u32 n,struct bpf_map** maps,int nm){
    g_bpfa_log[0]=0; int e=0; struct bpf_prog* r=bpf_prog_new(BPF_PROG_TYPE_TRACEPOINT,(const struct bpf_insn*)(const void*)p,n,maps,nm,g_bpfa_log,sizeof g_bpfa_log,&e);
    if(!r){ bpfa_puts("bpf: verifier rejected program: "); bpfa_puts(g_bpfa_log[0]?g_bpfa_log:"(no log)"); bpfa_puts("\n"); }
    return r; }
static struct bpf_map* bpfa_map(u32 t,u32 ks,u32 vs,u32 mx){ int e=0; return bpf_map_new(t,ks,vs,mx,0,&e); }
static int bpfa_wait(u32 secs){
    u64 end=g_ticks+(u64)secs*100;
    while(g_ticks<end){ int c=kpop(); if(c==3||c=='q')return 1; kidle(); }
    return 0; }
static u32 bpfa_num(const char* s,u32 d,u32 mx){ while(*s==' ')s++; if(*s<'0'||*s>'9')return d; u32 v=0; while(*s>='0'&&*s<='9'&&v<100000)v=v*10+(u32)(*s++-'0'); if(v==0)v=1; return v>mx?mx:v; }
static int bpfa_is(const char* a,const char* w){ while(*a==' ')a++; while(*w&&*a==*w){ a++; w++; } return !*w&&(*a==0||*a==' '); }
static const char* bpfa_after(const char* a){ while(*a==' ')a++; while(*a&&*a!=' ')a++; return a; }

static void bpfa_syscount(u32 secs){
    struct bpf_map* m=bpfa_map(BPF_MAP_TYPE_ARRAY,4,8,512); if(!m){ bpfa_puts("bpf: map create failed\n"); return; }
    struct bpf_map* ms[1]={m}; struct bpf_prog* p=bpfa_load(g_bpfa_syscount,BPFA_N(g_bpfa_syscount),ms,1); if(!p){ bpf_map_put(m); return; }
    int id=bpf_tp_attach(BPF_TP_SYS_ENTER,p); bpf_prog_put(p);
    if(id<0){ bpfa_puts("bpf: attach failed\n"); bpf_map_put(m); return; }
    bpfa_puts("counting Linux system calls (q to stop)...\n"); bpfa_wait(secs); bpf_tp_detach_id(id);
    u32 nr[512]; u64 ct[512]; u32 k=0;
    for(u32 i=0;i<512;i++){ u64* v=(u64*)bpf_map_lookup(m,&i); if(v&&*v){ nr[k]=i; ct[k]=*v; k++; } }
    for(u32 i=1;i<k;i++){ u32 a=nr[i]; u64 b=ct[i]; u32 j=i; while(j>0&&ct[j-1]<b){ nr[j]=nr[j-1]; ct[j]=ct[j-1]; j--; } nr[j]=a; ct[j]=b; }
    bpfa_puts("syscall      count\n");
    for(u32 i=0;i<k&&i<20;i++){ char b[64],*o=b; o=bpfa_u64(o,nr[i]); while(o<b+10)*o++=' '; o=bpfa_u64(o,ct[i]); *o++='\n'; *o=0; bpfa_puts(b); }
    if(!k)bpfa_puts("(no Linux system calls seen)\n");
    bpf_map_put(m); }

static void bpfa_exec(u32 secs){
    struct bpf_map* m=bpfa_map(BPF_MAP_TYPE_RINGBUF,0,0,65536); if(!m){ bpfa_puts("bpf: map create failed\n"); return; }
    struct bpf_map* ms[1]={m}; struct bpf_prog* p=bpfa_load(g_bpfa_exec,BPFA_N(g_bpfa_exec),ms,1); if(!p){ bpf_map_put(m); return; }
    int id=bpf_tp_attach(BPF_TP_SYS_ENTER,p); bpf_prog_put(p);
    if(id<0){ bpfa_puts("bpf: attach failed\n"); bpf_map_put(m); return; }
    bpfa_puts("tracing exec (q to stop)\nPID     TID     COMM\n");
    u64 end=g_ticks+(u64)secs*100; u32 seen=0;
    while(g_ticks<end){
        u8 r[64]; long n;
        for(int g=0;g<64&&(n=bpf_ringbuf_read(m,r,sizeof r))>=32;g++){ u64 pt=*(u64*)r; char b[80],*o=b; o=bpfa_u64(o,pt>>32); while(o<b+8)*o++=' '; o=bpfa_u64(o,(u32)pt); while(o<b+16)*o++=' '; for(int i=0;i<16&&r[16+i];i++)*o++=(char)r[16+i]; *o++='\n'; *o=0; bpfa_puts(b); seen++; }
        int c=kpop(); if(c==3||c=='q')break; kidle(); }
    bpf_tp_detach_id(id); bpf_map_put(m);
    if(!seen)bpfa_puts("(no exec seen)\n"); }

static void bpfa_slow(u32 secs){
    struct bpf_map* h=bpfa_map(BPF_MAP_TYPE_HASH,8,8,1024); struct bpf_map* a=bpfa_map(BPF_MAP_TYPE_ARRAY,4,8,32);
    if(!h||!a){ if(h)bpf_map_put(h); if(a)bpf_map_put(a); bpfa_puts("bpf: map create failed\n"); return; }
    struct bpf_map* ms[2]={h,a};
    struct bpf_prog* p1=bpfa_load(g_bpfa_lat_in,BPFA_N(g_bpfa_lat_in),ms,1); struct bpf_prog* p2=p1?bpfa_load(g_bpfa_lat_out,BPFA_N(g_bpfa_lat_out),ms,2):0;
    int i1=-1,i2=-1; if(p1&&p2){ i1=bpf_tp_attach(BPF_TP_SYS_ENTER,p1); i2=bpf_tp_attach(BPF_TP_SYS_EXIT,p2); }
    if(p1)bpf_prog_put(p1);
    if(p2)bpf_prog_put(p2);
    if(i1<0||i2<0){ if(i1>=0)bpf_tp_detach_id(i1); if(i2>=0)bpf_tp_detach_id(i2); bpf_map_put(h); bpf_map_put(a); if(p1&&p2)bpfa_puts("bpf: attach failed\n"); return; }
    bpfa_puts("measuring syscall latency (q to stop)...\n"); bpfa_wait(secs); bpf_tp_detach_id(i1); bpf_tp_detach_id(i2);
    u64 mx=0, v[32]; for(u32 i=0;i<32;i++){ u64* q=(u64*)bpf_map_lookup(a,&i); v[i]=q?*q:0; if(v[i]>mx)mx=v[i]; }
    bpfa_puts("     usecs            count  distribution\n");
    int last=-1; for(int i=0;i<32;i++) if(v[i])last=i;
    for(int i=0;i<=last;i++){ char b[120],*o=b; u64 lo=i?(1ULL<<i):0, hi=(1ULL<<(i+1))-1; o=bpfa_u64(o,lo); *o++=' '; *o++='-'; *o++='>'; *o++=' '; o=bpfa_u64(o,hi); while(o<b+20)*o++=' '; o=bpfa_u64(o,v[i]); while(o<b+32)*o++=' '; *o++='|'; u32 bar=mx?(u32)(v[i]*40/mx):0; for(u32 k=0;k<bar;k++)*o++='*'; *o++='\n'; *o=0; bpfa_puts(b); }
    if(last<0)bpfa_puts("(no system calls completed)\n");
    bpf_map_put(h); bpf_map_put(a); }

static void bpfa_trace(void){
    u32 h=g_bpfa_trh, s=h>BPFA_TRN?h-BPFA_TRN:0;
    for(u32 i=s;i<h;i++){ bpfa_puts(g_bpfa_tr[i%BPFA_TRN]); bpfa_puts("\n"); }
    if(s==h)bpfa_puts("(trace buffer empty)\n"); }

static void bpfa_status(void){
    const char* t[BPF_TP_N]={"sys_enter","sys_exit","sched_switch"};
    for(int i=0;i<BPF_TP_N;i++){ int n=0; for(int s=0;s<BPF_TP_SLOTS;s++) if(g_bpf_tp[i][s])n++; char b[64],*o=b; o=scopy(o,"tracepoint "); o=scopy(o,t[i]); o=scopy(o,": "); o=bpfa_u64(o,(u64)n); o=scopy(o," program(s)\n"); *o=0; bpfa_puts(b); }
    int n=0; for(int i=0;i<NLXS;i++) if(g_bpfa_sf[i].kind)n++;
    char b[48],*o=b; o=scopy(o,"socket filters: "); o=bpfa_u64(o,(u64)n); *o++='\n'; *o=0; bpfa_puts(b);
    bpfa_puts("eBPF core: ready\n"); }

void bpf_cmd(const char* a){
    if(!a)a="";
    if(g_uid!=0){ bpfa_puts("bpf: permission denied (root only)\n"); return; }
    if(bpfa_is(a,"status")){ bpfa_status(); return; }
    if(bpfa_is(a,"trace")){ bpfa_trace(); return; }
    int w=bpfa_is(a,"syscount")?1:bpfa_is(a,"exec")?2:bpfa_is(a,"slow")?3:0;
    if(!w){ bpfa_puts("usage: bpf syscount [secs] | exec [secs] | slow [secs] | trace | status\n"); return; }
    bpf_attach_init();
    u32 secs=bpfa_num(bpfa_after(a),w==2?30:5,3600);
    if(w==1)bpfa_syscount(secs); else if(w==2)bpfa_exec(secs); else bpfa_slow(secs); }
