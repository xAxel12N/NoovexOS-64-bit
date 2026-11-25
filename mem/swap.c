#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
extern u32 pframe_refcount(u64 pa); extern volatile u64 g_ticks; extern int pf_ready; extern u32 pf_n; extern u32 pframe_hi_count(void); extern int pf_owned(u64 pa);

typedef struct { u64 loc; u16 len; u16 ref; u32 nx; } zs_slot;
typedef struct { u64 pa; u64 used; int prev, next; u16 cls, cnt; int on; } zs_pg;
#define ZS_SCH 1024
#define ZS_PCH 256
#define ZS_NCL 32
#define ZS_MAXC 2048
static zs_slot* g_zs_sc[ZS_SCH]; static u32 g_zs_nsl, g_zs_sfree=0xFFFFFFFFu;
static zs_pg* g_zs_pc[ZS_PCH]; static int g_zs_npg, g_zs_pfree=-1;
static int g_zs_part[ZS_NCL]; static int g_zs_init;
u64 g_swap_total, g_swap_used, g_swap_bytes, g_swap_store, g_swap_outs, g_swap_ins, g_swap_same, g_swap_reject, g_swap_scans, g_swap_kicks, g_swap_cycles;
u64 swap_cycles(void){ return g_swap_cycles; }
int g_swap_on=1; int g_swappiness=60;
static zs_slot* zs_sl(u32 id){ return &g_zs_sc[id>>10][id&1023]; }
static zs_pg* zs_pg_at(int i){ return &g_zs_pc[i>>10][i&1023]; }
static void zs_setup(void){
    if(g_zs_init)return; g_zs_init=1; for(int i=0;i<ZS_NCL;i++)g_zs_part[i]=-1;
    u64 t=((u64)pf_n+(u64)pframe_hi_count())/2; if(t>(u64)ZS_SCH*1024ULL)t=(u64)ZS_SCH*1024ULL; g_swap_total=t; }
static int zs_slot_new(u32* out){
    if(g_zs_sfree!=0xFFFFFFFFu){ u32 id=g_zs_sfree; g_zs_sfree=zs_sl(id)->nx; *out=id; return 1; }
    if((g_zs_nsl&1023)==0){ u32 c=g_zs_nsl>>10; if(c>=ZS_SCH)return 0; if(!g_zs_sc[c]){ g_zs_sc[c]=(zs_slot*)kmalloc(sizeof(zs_slot)*1024); if(!g_zs_sc[c])return 0; } }
    *out=g_zs_nsl++; return 1; }
static void zs_slot_free(u32 id){ zs_slot* s=zs_sl(id); s->ref=0; s->len=0; s->loc=0; s->nx=g_zs_sfree; g_zs_sfree=id; }
static int zs_pg_new(void){
    if(g_zs_pfree>=0){ int i=g_zs_pfree; g_zs_pfree=zs_pg_at(i)->next; return i; }
    if((g_zs_npg&1023)==0){ int c=g_zs_npg>>10; if(c>=ZS_PCH)return -1; if(!g_zs_pc[c]){ g_zs_pc[c]=(zs_pg*)kmalloc(sizeof(zs_pg)*1024); if(!g_zs_pc[c])return -1; } }
    return g_zs_npg++; }
static void zs_unlink(int i){ zs_pg* g=zs_pg_at(i); if(!g->on)return;
    if(g->prev>=0)zs_pg_at(g->prev)->next=g->next; else g_zs_part[g->cls]=g->next;
    if(g->next>=0)zs_pg_at(g->next)->prev=g->prev; g->on=0; g->prev=g->next=-1; }
static void zs_link(int i){ zs_pg* g=zs_pg_at(i); if(g->on)return; g->prev=-1; g->next=g_zs_part[g->cls]; if(g->next>=0)zs_pg_at(g->next)->prev=i; g_zs_part[g->cls]=i; g->on=1; }
static u64 g_zs_res[16]; static int g_zs_nres;
static void zs_refill(void){ u32 fr=pframe_free_count(); while(g_zs_nres<16&&fr>64){ u64 f=pframe_alloc(); if(!f)break; g_zs_res[g_zs_nres++]=f; fr--; } }
int swap_reserve_level(void){ return g_zs_nres; }
static u64 zs_frame(void){ u64 f=pframe_alloc(); if(f||!g_zs_nres)return f; f=g_zs_res[--g_zs_nres]; u64* z=(u64*)f; for(int i=0;i<512;i++)z[i]=0; return f; }
static int zs_store(const u8* b,int len,u64* loc){
    int cls=(len+63)/64-1; if(cls<0)cls=0; if(cls>=ZS_NCL)return 0; u32 sz=(u32)(cls+1)*64u, n=4096u/sz;
    int i=g_zs_part[cls];
    if(i<0){ u64 f=zs_frame(); if(!f)return 0; i=zs_pg_new(); if(i<0){ pframe_free(f); return 0; }
        zs_pg* g=zs_pg_at(i); g->pa=f; g->used=0; g->cls=(u16)cls; g->cnt=0; g->on=0; g->prev=g->next=-1; zs_link(i); g_swap_store++; }
    zs_pg* g=zs_pg_at(i); u32 k=0; while(k<n&&((g->used>>k)&1))k++; if(k>=n)return 0;
    g->used|=1ULL<<k; g->cnt++; if(g->cnt>=n)zs_unlink(i);
    u8* d=(u8*)(g->pa+(u64)k*sz); for(int q=0;q<len;q++)d[q]=b[q];
    *loc=((u64)i<<8)|k; return 1; }
static const u8* zs_obj(u64 loc){ int i=(int)(loc>>8); u32 k=(u32)(loc&255); if(i<0||i>=g_zs_npg)return 0; zs_pg* g=zs_pg_at(i); if(!g->pa)return 0; return (const u8*)(g->pa+(u64)k*((u32)(g->cls+1)*64u)); }
static void zs_drop(u64 loc){
    int i=(int)(loc>>8); u32 k=(u32)(loc&255); if(i<0||i>=g_zs_npg)return; zs_pg* g=zs_pg_at(i); if(!g->pa||!((g->used>>k)&1))return;
    u32 n=4096u/((u32)(g->cls+1)*64u); int full=g->cnt>=n; g->used&=~(1ULL<<k); g->cnt--;
    if(!g->cnt){ zs_unlink(i); pframe_free(g->pa); g->pa=0; g->next=g_zs_pfree; g_zs_pfree=i; if(g_swap_store)g_swap_store--; return; }
    if(full)zs_link(i); }

static u16 g_lzh[4096];
static inline u32 lz_rd32(const u8* p){ u32 v; __builtin_memcpy(&v,p,4); return v; }
static int lz_put_len(u8* d,int op,int cap,int v){ while(v>=255){ if(op>=cap)return -1; d[op++]=255; v-=255; } if(op>=cap)return -1; d[op++]=(u8)v; return op; }
int lz_comp4k(const u8* s,u8* d,int cap){
    for(int i=0;i<4096;i++)g_lzh[i]=0xFFFF;
    int ip=0, anchor=0, op=0;
    while(ip<=4096-9){
        u32 v=lz_rd32(s+ip); u32 h=(v*2654435761u)>>20; int ref=g_lzh[h]; g_lzh[h]=(u16)ip;
        if(ref==0xFFFF||lz_rd32(s+ref)!=v){ ip++; continue; }
        int ml=4; while(ip+ml<4096&&s[ref+ml]==s[ip+ml])ml++;
        int ll=ip-anchor; if(op>=cap)return 0; int tk=op++; d[tk]=(u8)(((ll<15?ll:15)<<4)|((ml-4)<15?(ml-4):15));
        if(ll>=15){ op=lz_put_len(d,op,cap,ll-15); if(op<0)return 0; }
        if(op+ll+2>cap)return 0; for(int q=0;q<ll;q++)d[op++]=s[anchor+q];
        int off=ip-ref; d[op++]=(u8)off; d[op++]=(u8)(off>>8);
        if(ml-4>=15){ op=lz_put_len(d,op,cap,ml-4-15); if(op<0)return 0; }
        ip+=ml; anchor=ip; }
    int ll=4096-anchor; if(op>=cap)return 0; d[op++]=(u8)((ll<15?ll:15)<<4);
    if(ll>=15){ op=lz_put_len(d,op,cap,ll-15); if(op<0)return 0; }
    if(op+ll>cap)return 0; for(int q=0;q<ll;q++)d[op++]=s[anchor+q];
    return op; }
int lz_dec4k(const u8* s,int n,u8* d){
    int ip=0, op=0;
    while(ip<n){ int t=s[ip++]; int ll=t>>4;
        if(ll==15){ int b; do{ if(ip>=n)return -1; b=s[ip++]; ll+=b; }while(b==255&&ll<8192); }
        if(ll>4096-op||ll>n-ip)return -1; for(int q=0;q<ll;q++)d[op++]=s[ip++];
        if(ip==n)break;
        if(ip+2>n)return -1; int off=s[ip]|(s[ip+1]<<8); ip+=2; if(off==0||off>op)return -1;
        int ml=(t&15)+4; if((t&15)==15){ int b; do{ if(ip>=n)return -1; b=s[ip++]; ml+=b; }while(b==255&&ml<8192); }
        if(ml>4096-op)return -1; for(int q=0;q<ml;q++){ d[op]=d[op-off]; op++; } }
    return op==4096?0:-1; }

static int zs_put_page(const u8* p,u32* out){
    zs_setup(); if(g_swap_used>=g_swap_total)return 0;
    const u64* w=(const u64*)p; int same=1; for(int i=1;i<512;i++) if(w[i]!=w[0]){ same=0; break; }
    u32 id; if(!zs_slot_new(&id))return 0; zs_slot* s=zs_sl(id);
    if(same){ s->loc=w[0]; s->len=0; s->ref=1; g_swap_same++; }
    else { static u8 cb[ZS_MAXC]; int n=lz_comp4k(p,cb,ZS_MAXC); u64 loc;
        if(n<=0){ zs_slot_free(id); g_swap_reject++; return -1; }
        if(!zs_store(cb,n,&loc)){ zs_slot_free(id); return 0; }
        s->loc=loc; s->len=(u16)n; s->ref=1; g_swap_bytes+=(u64)n; }
    g_swap_used++; g_swap_outs++; *out=id; return 1; }
static int zs_ok(u32 id){ return id<g_zs_nsl&&g_zs_sc[id>>10]&&zs_sl(id)->ref>0; }
void swap_put(u64 e){ u32 id=(u32)((e&PTE_ADDR)>>12); if(!zs_ok(id))return; zs_slot* s=zs_sl(id);
    if(--s->ref)return; if(s->len){ zs_drop(s->loc); if(g_swap_bytes>=s->len)g_swap_bytes-=s->len; } zs_slot_free(id); if(g_swap_used)g_swap_used--; }
int swap_dup(u64 e){ u32 id=(u32)((e&PTE_ADDR)>>12); if(!zs_ok(id))return -1; zs_slot* s=zs_sl(id); if(s->ref>=0xFFFE)return -1; s->ref++; return 0; }
int swap_read(u64 e,u64 dst){ u32 id=(u32)((e&PTE_ADDR)>>12); if(!zs_ok(id))return -1; zs_slot* s=zs_sl(id); u8* d=(u8*)dst;
    if(!s->len){ u64* w=(u64*)d; for(int i=0;i<512;i++)w[i]=s->loc; return 0; }
    const u8* src=zs_obj(s->loc); if(!src)return -1; return lz_dec4k(src,s->len,d); }
int swap_in(u64 e,u64 dst){ if(swap_read(e,dst))return -1; swap_put(e); g_swap_ins++; return 0; }

extern int lx_fm_covers(u64 cr3,u64 va);
static int zs_futex_pa(u64 pa){
    for(int j=0;j<JMAX;j++){ struct jproc* p=&g_jp[j]; if(p->state==S_FREE||!p->fx_wait||p->fx_tg!=-77)continue; if((p->fx_addr&PTE_ADDR)==pa)return 1; }
    return 0; }
#define PTE_NOZ 0x0080000000000000ULL
static int zs_cand(u64 e){ return (e&7)==7&&(e&PTE_ANON)&&!(e&(PTE_SHR|PTE_COW|PTE_LAZY))&&(!(e&PTE_NOZ)||(e&0x40ULL)); }
static int g_sc_j; static u64 g_sc_va;
#define ZS_B 64
static u32 zs_batch(u64 cr3,u64** pts,u64* vas,u64* olds,int n){
    if(!n)return 0; u64 a=cr3&~0xFFFULL;
    for(int i=0;i<n;i++){ *pts[i]=0; }
    if((cpu_get_pt()&~0xFFFULL)==a)cpu_set_pt(cpu_get_pt());
    smp_tlb_sync(a);
    u32 got=0;
    for(int i=0;i<n;i++){ u64 o=olds[i], pa=o&PTE_ADDR; u32 id;
        int r=zs_put_page((const u8*)pa,&id);
        if(r>0){ *pts[i]=PTE_LAZY|PTE_SWP|((u64)id<<12)|PTE_ANON|(o&PTE_PBITS); if(g_zs_nres<4)g_zs_res[g_zs_nres++]=pa; else pframe_free(pa); got++; }
        else *pts[i]=r<0?((o&~0x40ULL)|PTE_NOZ):o; (void)vas; }
    return got; }
static int zs_scan_space(u64 cr3,u64* pos,u32 want,u32* got,u32* budget){
    u64* pml4=(u64*)(cr3&~0xFFFULL); if(!(pml4[8]&1))return 1; u64* pdpt=(u64*)(pml4[8]&PTE_ADDR);
    u64* pts[ZS_B]; u64 vas[ZS_B], olds[ZS_B]; int nb=0;
    u64 v=*pos; if(v<0x40000000000ULL)v=0x40000000000ULL;
    while(v<0x48000000000ULL){
        int i3=(int)((v>>30)&511), i2=(int)((v>>21)&511), i1=(int)((v>>12)&511);
        if(!(pdpt[i3]&1)){ v=((v>>30)+1)<<30; continue; } u64* pd=(u64*)(pdpt[i3]&PTE_ADDR);
        if(!(pd[i2]&1)||(pd[i2]&0x80ULL)){ v=((v>>21)+1)<<21; continue; } u64* pt=(u64*)(pd[i2]&PTE_ADDR);
        for(;i1<512;i1++,v+=0x1000ULL){
            if(!*budget)break; (*budget)--;
            u64 e=pt[i1]; if(!zs_cand(e))continue;
            if(e&0x20ULL){ __atomic_and_fetch(&pt[i1],~0x20ULL,__ATOMIC_RELAXED); continue; }
            u64 pa=e&PTE_ADDR; if(!pf_owned(pa)||pframe_refcount(pa)!=1||zs_futex_pa(pa)||lx_fm_covers(cr3,v))continue;
            pts[nb]=&pt[i1]; vas[nb]=v; olds[nb]=e; nb++;
            if(nb==ZS_B||*got+(u32)nb>=want){ *got+=zs_batch(cr3,pts,vas,olds,nb); nb=0; if(*got>=want){ *pos=v+0x1000ULL; return 0; } } }
        if(i1<512){ if(nb)*got+=zs_batch(cr3,pts,vas,olds,nb); *pos=v; return 0; }
        v=((v>>21)+1)<<21; }
    if(nb)*got+=zs_batch(cr3,pts,vas,olds,nb);
    return 1; }
static u32 zs_reclaim(u32 want,int self_ok){
    if(!g_swap_on||!pf_ready||!want){ g_swap_cycles++; return 0; } zs_setup(); if(g_swap_used>=g_swap_total){ g_swap_cycles++; return 0; }
    zs_refill(); u32 got=0, budget=200000; g_swap_scans++; u64 me=cpu_get_pt()&~0xFFFULL;
    for(int steps=0;steps<JMAX+1&&got<want&&budget;steps++){
        if(g_sc_j<0||g_sc_j>=JMAX){ g_sc_j=0; g_sc_va=0; g_swap_cycles++; }
        struct jproc* p=&g_jp[g_sc_j];
        if(p->state==S_FREE||!p->cr3||p->tg!=g_sc_j||(!self_ok&&(p->cr3&~0xFFFULL)==me)){ g_sc_j++; g_sc_va=0; continue; }
        if(zs_scan_space(p->cr3,&g_sc_va,want,&got,&budget)){ g_sc_j++; g_sc_va=0; } }
    return got; }
extern int g_sr_nolock[64];
int g_sw_self[64];
u32 swap_reclaim(u32 want){ int c=cpu_id()&63; return zs_reclaim(want,g_sr_nolock[c]||g_sw_self[c]); }
static u32 zs_mem_reclaim(u32 want,int self_ok){
    extern u32 sysroot_pc_reclaim(u32); u32 n=sysroot_pc_reclaim(want); if(n>=want)return n;
    return n+zs_reclaim(want-n,self_ok); }
u32 mem_reclaim(u32 want){ int c=cpu_id()&63; return zs_mem_reclaim(want,g_sr_nolock[c]||g_sw_self[c]); }
u32 swap_low_mark(void){ u32 t=pf_n+pframe_hi_count(); u32 lo=t/32; if(lo<2048)lo=2048; return lo; }
void swap_poll(void){
    if(!pf_ready)return; static u64 last, wait; if(g_ticks==last||g_ticks<wait)return; last=g_ticks;
    zs_refill(); u32 fr=pframe_free_count(), lo=swap_low_mark();
    if(fr>=lo)return;
    g_swap_kicks++; u32 want=lo*2-fr; if(want>256)want=256; if(!g_swappiness){ extern u32 sysroot_pc_reclaim(u32); sysroot_pc_reclaim(want); return; }
    if(!zs_mem_reclaim(want,1))wait=g_ticks+20; }
void swap_stats(u64* total_kb,u64* used_kb,u64* store_kb,u64* comp_kb){ zs_setup(); if(total_kb)*total_kb=g_swap_total*4; if(used_kb)*used_kb=g_swap_used*4; if(store_kb)*store_kb=g_swap_store*4; if(comp_kb)*comp_kb=g_swap_bytes/1024; }
int swap_set_total(u64 pages){ zs_setup(); if(pages>(u64)ZS_SCH*1024ULL)pages=(u64)ZS_SCH*1024ULL; g_swap_total=pages; return 0; }
u32 swap_pageout(u64 cr3,u64 a,u64 e){
    if(!g_swap_on||!pf_ready)return 0; zs_setup(); u64 c=cr3&~0xFFFULL; u64* pts[ZS_B]; u64 vas[ZS_B], olds[ZS_B]; int nb=0; u32 got=0, seen=0, cands=0;
    for(u64 v=a&~0xFFFULL;v<e&&seen<262144&&cands<16384;v+=0x1000ULL){ seen++; u64* pt=vm_walk(c,v,0); if(!pt){ v=vm_skip(c,v)-0x1000ULL; continue; }
        u64 x=*pt; if(!zs_cand(x))continue; u64 pa=x&PTE_ADDR; if(!pf_owned(pa)||pframe_refcount(pa)!=1||zs_futex_pa(pa)||lx_fm_covers(c,v))continue;
        pts[nb]=pt; vas[nb]=v; olds[nb]=x; nb++; cands++; if(nb==ZS_B){ got+=zs_batch(c,pts,vas,olds,nb); nb=0; } }
    if(nb)got+=zs_batch(c,pts,vas,olds,nb);
    return got; }
static long zs_all_in(void){
    for(int j=0;j<JMAX;j++){ struct jproc* p=&g_jp[j]; if(p->state==S_FREE||!p->cr3||p->tg!=j)continue;
        u64* pml4=(u64*)(p->cr3&~0xFFFULL); if(!(pml4[8]&1))continue; u64* pdpt=(u64*)(pml4[8]&PTE_ADDR);
        for(int i3=0;i3<512;i3++){ if(!(pdpt[i3]&1))continue; u64* pd=(u64*)(pdpt[i3]&PTE_ADDR);
            for(int i2=0;i2<512;i2++){ if(!(pd[i2]&1)||(pd[i2]&0x80ULL))continue; u64* pt=(u64*)(pd[i2]&PTE_ADDR);
                for(int i1=0;i1<512;i1++){ u64 x=pt[i1]; if(!pte_is_swap(x))continue; u64 f=pframe_alloc(); if(!f)return -12;
                    if(swap_in(x,f)){ pframe_free(f); return -5; } pt[i1]=vm_live(f,x)|PTE_ANON; } } } }
    return 0; }
long swap_onoff(int on){ zs_setup(); if(on){ g_swap_on=1; return 0; } g_swap_on=0; long r=zs_all_in(); if(r){ g_swap_on=1; return r; } return 0; }
