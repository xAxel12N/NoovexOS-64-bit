#include "include/nvx/sys/bpf.h"
#ifdef BPF_HOST
#include <stdlib.h>
#define bv_BPF_ALLOC(n) calloc(1,(n))
#define bv_BPF_FREE(p) free(p)
static int bpfi_cpu(void){ return 0; }
static u64 bpfi_irq_save(void){ return 0; }
static void bpfi_irq_restore(u64 f){ (void)f; }
int bpf_allowed(void){ return 1; }
#else
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/core/proc/jobs.h"
#define bv_BPF_ALLOC(n) kcalloc(1,(n))
#define bv_BPF_FREE(p) kfree(p)
static int bpfi_cpu(void){ int c=cpu_id(); return c<0?0:c%BPF_NCPU; }
static u64 bpfi_irq_save(void){ return cpu_irq_save(); }
static void bpfi_irq_restore(u64 f){ cpu_irq_restore(f); }
int bpf_allowed(void){ return g_jcur>=0&&g_jcur<JMAX&&g_jp[g_jcur].uid==0; }
#endif

int g_bpf_unpriv_disabled=1;
static void bv_bz(void* d,u64 n){ u8* p=(u8*)d; while(n--)*p++=0; }
static void bv_bcp(void* d,const void* s,u64 n){ u8* a=(u8*)d; const u8* b=(const u8*)s; while(n--)*a++=*b++; }
static int bv_bcmp_(const void* x,const void* y,u64 n){ const u8* a=(const u8*)x; const u8* b=(const u8*)y; for(u64 i=0;i<n;i++) if(a[i]!=b[i])return 1; return 0; }
static int bv_lock_take(u32* l,u64* fo){ u64 f=bpfi_irq_save(); u32 spins=0; while(__atomic_exchange_n(l,1u,__ATOMIC_ACQUIRE)){ if(++spins>100000000u){ bpfi_irq_restore(f); return 0; } __builtin_ia32_pause(); } *fo=f; return 1; }
static void bv_lock_drop(u32* l,u64 f){ __atomic_store_n(l,0u,__ATOMIC_RELEASE); bpfi_irq_restore(f); }

static struct bpf_map bv_g_maps[BPF_MAXMAPS];
static struct bpf_prog bv_g_progs[BPF_MAXPROGS];
static int bv_g_bpf_id;
struct bpfi_slot { int kind; void* obj; int link; };
static int (*bv_g_link_put)(int);
#define bv_BPF_SLOTS 256
static struct bpfi_slot bv_g_slots[bv_BPF_SLOTS];
static u32 bv_g_slot_lock;

static u64 bv_hl_lookup(u64 m,u64 k,u64 a,u64 b,u64 c){ (void)a;(void)b;(void)c; return (u64)bpf_map_lookup((struct bpf_map*)m,(const void*)k); }
static u64 bv_hl_update(u64 m,u64 k,u64 v,u64 f,u64 c){ (void)c; return (u64)(i64)bpf_map_update((struct bpf_map*)m,(const void*)k,(const void*)v,f); }
static u64 bv_hl_delete(u64 m,u64 k,u64 a,u64 b,u64 c){ (void)a;(void)b;(void)c; return (u64)(i64)bpf_map_delete((struct bpf_map*)m,(const void*)k); }
static u64 bv_hl_rbout(u64 m,u64 d,u64 n,u64 f,u64 c){ (void)f;(void)c; return (u64)(i64)bpf_ringbuf_output((struct bpf_map*)m,(const void*)d,(u32)n); }
static u64 bv_hl_cpu(u64 a,u64 b,u64 c,u64 d,u64 e){ (void)a;(void)b;(void)c;(void)d;(void)e; return (u64)bpfi_cpu(); }
static const struct bpf_helper bv_h_lookup={BPF_FUNC_map_lookup_elem,BPF_TYPES_ALL,bv_hl_lookup,BPF_RET_MAP_VALUE_OR_NULL,{BPF_ARG_CONST_MAP,BPF_ARG_MAP_KEY,0,0,0}};
static const struct bpf_helper bv_h_update={BPF_FUNC_map_update_elem,BPF_TYPES_ALL,bv_hl_update,BPF_RET_INT,{BPF_ARG_CONST_MAP,BPF_ARG_MAP_KEY,BPF_ARG_MAP_VALUE,BPF_ARG_ANY,0}};
static const struct bpf_helper bv_h_delete={BPF_FUNC_map_delete_elem,BPF_TYPES_ALL,bv_hl_delete,BPF_RET_INT,{BPF_ARG_CONST_MAP,BPF_ARG_MAP_KEY,0,0,0}};
static const struct bpf_helper bv_h_rbout={BPF_FUNC_ringbuf_output,BPF_TYPES_ALL,bv_hl_rbout,BPF_RET_INT,{BPF_ARG_CONST_MAP,BPF_ARG_MEM_RD,BPF_ARG_SIZE,BPF_ARG_ANY,0}};
static const struct bpf_helper bv_h_cpu={BPF_FUNC_get_smp_processor_id,BPF_TYPES_ALL,bv_hl_cpu,BPF_RET_INT,{0,0,0,0,0}};
static const struct bpf_helper* bv_g_helpers[BPF_FUNC_MAX]={[BPF_FUNC_map_lookup_elem]=&bv_h_lookup,[BPF_FUNC_map_update_elem]=&bv_h_update,[BPF_FUNC_map_delete_elem]=&bv_h_delete,[BPF_FUNC_ringbuf_output]=&bv_h_rbout,[BPF_FUNC_get_smp_processor_id]=&bv_h_cpu};
static struct bpf_ctx_desc bv_g_ctx[BPF_PROG_TYPE_MAX]={[BPF_PROG_TYPE_SOCKET_FILTER]={sizeof(struct bpf_skb_ctx),16,1},[BPF_PROG_TYPE_KPROBE]={sizeof(struct bpf_trace_ctx),sizeof(struct bpf_trace_ctx),0},
    [BPF_PROG_TYPE_TRACEPOINT]={sizeof(struct bpf_trace_ctx),sizeof(struct bpf_trace_ctx),0},[BPF_PROG_TYPE_RAW_TRACEPOINT]={sizeof(struct bpf_trace_ctx),sizeof(struct bpf_trace_ctx),0}};

int bpf_helper_register(const struct bpf_helper* h){ if(!h||!h->fn||h->id==0||h->id>=BPF_FUNC_MAX)return -1; if(h->id==BPF_FUNC_map_lookup_elem||h->id==BPF_FUNC_map_update_elem||h->id==BPF_FUNC_map_delete_elem||h->id==BPF_FUNC_ringbuf_output)return -1; bv_g_helpers[h->id]=h; return 0; }
const struct bpf_helper* bpf_helper_get(u32 id,u32 t){ if(id==0||id>=BPF_FUNC_MAX||t>=BPF_PROG_TYPE_MAX)return 0; const struct bpf_helper* h=bv_g_helpers[id]; if(!h||!(h->types&BPF_TYPE_BIT(t)))return 0; return h; }
void bpf_ctx_register(u32 t,u32 size,u32 rd,u32 pkt){ if(t==0||t>=BPF_PROG_TYPE_MAX||rd>size||size>4096)return; bv_g_ctx[t].size=size; bv_g_ctx[t].rd_size=rd; bv_g_ctx[t].pkt=pkt; }
const struct bpf_ctx_desc* bpf_ctx_get(u32 t){ if(t==0||t>=BPF_PROG_TYPE_MAX||!bv_g_ctx[t].size)return 0; return &bv_g_ctx[t]; }

static u32 bv_r8(u32 v){ return (v+7)&~7u; }
static u32 bv_hcap(struct bpf_map* m){ u32 c=1; while(c<m->max_entries*2u)c<<=1; return c; }
static u32 bv_hhash(const u8* k,u32 n){ u32 h=2166136261u; for(u32 i=0;i<n;i++){ h^=k[i]; h*=16777619u; } return h; }
#define bv_H_FREE 0
#define bv_H_USED 1
#define bv_H_DEAD 2

struct bpf_map* bpf_map_new(u32 type,u32 ks,u32 vs,u32 max,u32 flags,int* err){
    int e=-22; struct bpf_map* m=0; u64 bytes=0; u32 es=0;
    if(flags||max==0||max>65536){ if(err)*err=e; return 0; }
    if(type==BPF_MAP_TYPE_ARRAY||type==BPF_MAP_TYPE_PERCPU_ARRAY){ if(ks!=4||vs==0||vs>65536)goto out; es=bv_r8(vs); bytes=(u64)es*max*(type==BPF_MAP_TYPE_PERCPU_ARRAY?BPF_NCPU:1); }
    else if(type==BPF_MAP_TYPE_HASH){ if(ks==0||ks>512||vs==0||vs>65536)goto out; es=8+bv_r8(ks)+bv_r8(vs); u32 c=1; while(c<max*2u)c<<=1; bytes=(u64)es*c; }
    else if(type==BPF_MAP_TYPE_RINGBUF){ if(ks||vs||max<4096||max>(16u<<20)||(max&(max-1)))goto out; es=1; bytes=max; }
    else goto out;
    if(bytes>(32ull<<20)){ e=-7; goto out; }
    { u64 f; if(bv_lock_take(&bv_g_slot_lock,&f)){ for(int i=0;i<BPF_MAXMAPS;i++) if(!bv_g_maps[i].used){ m=&bv_g_maps[i]; bv_bz(m,sizeof *m); m->used=1; break; } bv_lock_drop(&bv_g_slot_lock,f); } }
    if(!m){ e=-12; goto out; }
    m->data=(u8*)bv_BPF_ALLOC(bytes); if(!m->data){ m->used=0; m=0; e=-12; goto out; }
    m->refs=1; m->id=__atomic_add_fetch(&bv_g_bpf_id,1,__ATOMIC_RELAXED); m->type=type; m->key_size=ks; m->value_size=vs; m->max_entries=max; m->elem_size=es; m->rb_size=type==BPF_MAP_TYPE_RINGBUF?max:0;
    return m;
out: if(err)*err=e; return 0;
}
void bpf_map_get(struct bpf_map* m){ if(m)__atomic_add_fetch(&m->refs,1,__ATOMIC_ACQ_REL); }
void bpf_map_put(struct bpf_map* m){ if(!m)return; if(__atomic_sub_fetch(&m->refs,1,__ATOMIC_ACQ_REL)==0){ bv_BPF_FREE(m->data); m->data=0; m->used=0; } }

static u8* bv_hfind(struct bpf_map* m,const void* key,int ins){
    u32 c=bv_hcap(m), i=bv_hhash((const u8*)key,m->key_size)&(c-1); u8* tomb=0;
    for(u32 n=0;n<c;n++,i=(i+1)&(c-1)){ u8* e=m->data+(u64)i*m->elem_size; u32 st=*(u32*)e;
        if(st==bv_H_FREE)return ins?(tomb?tomb:e):0;
        if(st==bv_H_DEAD){ if(!tomb)tomb=e; continue; }
        if(!bv_bcmp_(e+8,key,m->key_size))return e; }
    return ins?tomb:0;
}
void* bpf_map_lookup(struct bpf_map* m,const void* key){
    if(!m||!key)return 0;
    if(m->type==BPF_MAP_TYPE_ARRAY){ u32 k=*(const u32*)key; return k<m->max_entries?m->data+(u64)k*m->elem_size:0; }
    if(m->type==BPF_MAP_TYPE_PERCPU_ARRAY){ u32 k=*(const u32*)key; return k<m->max_entries?m->data+((u64)bpfi_cpu()*m->max_entries+k)*m->elem_size:0; }
    if(m->type==BPF_MAP_TYPE_HASH){ u64 f; u8* e=0; if(bv_lock_take(&m->lock,&f)){ e=bv_hfind(m,key,0); bv_lock_drop(&m->lock,f); } return e?e+8+bv_r8(m->key_size):0; }
    return 0;
}
int bpf_map_update(struct bpf_map* m,const void* key,const void* val,u64 flags){
    if(!m||!key||!val||flags>BPF_EXIST)return -22;
    if(m->type==BPF_MAP_TYPE_ARRAY||m->type==BPF_MAP_TYPE_PERCPU_ARRAY){ if(flags==BPF_NOEXIST)return -17; void* d=bpf_map_lookup(m,key); if(!d)return -7; bv_bcp(d,val,m->value_size); return 0; }
    if(m->type!=BPF_MAP_TYPE_HASH)return -22;
    u64 f; if(!bv_lock_take(&m->lock,&f))return -16; int r=0; u8* e=bv_hfind(m,key,0);
    if(e){ if(flags==BPF_NOEXIST)r=-17; else bv_bcp(e+8+bv_r8(m->key_size),val,m->value_size); }
    else if(flags==BPF_EXIST)r=-2;
    else if(m->count>=m->max_entries)r=-7;
    else { e=bv_hfind(m,key,1); if(!e)r=-7; else { bv_bcp(e+8,key,m->key_size); bv_bcp(e+8+bv_r8(m->key_size),val,m->value_size); *(u32*)e=bv_H_USED; m->count++; } }
    bv_lock_drop(&m->lock,f); return r;
}
int bpf_map_delete(struct bpf_map* m,const void* key){
    if(!m||!key||m->type!=BPF_MAP_TYPE_HASH)return -22;
    u64 f; if(!bv_lock_take(&m->lock,&f))return -16; u8* e=bv_hfind(m,key,0); int r=-2; if(e){ *(u32*)e=bv_H_DEAD; m->count--; r=0; } bv_lock_drop(&m->lock,f); return r;
}
int bpf_map_next_key(struct bpf_map* m,const void* key,void* next){
    if(!m||!next)return -22;
    if(m->type==BPF_MAP_TYPE_ARRAY||m->type==BPF_MAP_TYPE_PERCPU_ARRAY){ u32 k=key?*(const u32*)key:0xFFFFFFFFu; u32 n=(k>=m->max_entries)?0:k+1; if(n>=m->max_entries)return -2; *(u32*)next=n; return 0; }
    if(m->type!=BPF_MAP_TYPE_HASH)return -22;
    u64 f; if(!bv_lock_take(&m->lock,&f))return -16; u32 c=bv_hcap(m), i=0; int r=-2;
    if(key){ u8* e=bv_hfind(m,key,0); if(e)i=(u32)((e-m->data)/m->elem_size)+1; }
    for(;i<c;i++){ u8* e=m->data+(u64)i*m->elem_size; if(*(u32*)e==bv_H_USED){ bv_bcp(next,e+8,m->key_size); r=0; break; } }
    bv_lock_drop(&m->lock,f); return r;
}
int bpf_ringbuf_output(struct bpf_map* m,const void* data,u32 len){
    if(!m||m->type!=BPF_MAP_TYPE_RINGBUF||!data||len==0||len>m->rb_size/4)return -22;
    u32 need=8+bv_r8(len); u64 f; if(!bv_lock_take(&m->lock,&f))return -16; int r=0;
    if(m->rb_head-m->rb_tail+need>m->rb_size)r=-7;
    else { u32 hdr[2]={len,0}; for(u32 i=0;i<8;i++)m->data[(m->rb_head+i)&(m->rb_size-1)]=((u8*)hdr)[i]; for(u32 i=0;i<len;i++)m->data[(m->rb_head+8+i)&(m->rb_size-1)]=((const u8*)data)[i]; m->rb_head+=need; }
    bv_lock_drop(&m->lock,f); return r;
}
int bpf_ringbuf_read(struct bpf_map* m,void* buf,u32 cap){
    if(!m||m->type!=BPF_MAP_TYPE_RINGBUF||!buf)return -22;
    u64 f; if(!bv_lock_take(&m->lock,&f))return -16; int r=0;
    if(m->rb_head!=m->rb_tail){ u32 len=0; for(u32 i=0;i<4;i++)((u8*)&len)[i]=m->data[(m->rb_tail+i)&(m->rb_size-1)];
        u32 cp=len<cap?len:cap; for(u32 i=0;i<cp;i++)((u8*)buf)[i]=m->data[(m->rb_tail+8+i)&(m->rb_size-1)]; m->rb_tail+=8+bv_r8(len); r=(int)cp; }
    bv_lock_drop(&m->lock,f); return r;
}

struct bpf_prog* bpf_prog_new(u32 type,const struct bpf_insn* insns,u32 n,struct bpf_map** maps,int nmaps,char* log,u32 logsz,int* err){
    struct bpf_prog* p=0; int e=-22;
    if(!insns||n==0||n>BPF_MAXINSNS||nmaps<0||nmaps>BPF_PROG_MAPS||!bpf_ctx_get(type))goto out;
    struct bpf_insn* c=(struct bpf_insn*)bv_BPF_ALLOC((u64)n*sizeof *c); if(!c){ e=-12; goto out; }
    bv_bcp(c,insns,(u64)n*sizeof *c);
    e=bpf_verify(type,c,n,maps,nmaps,log,logsz); if(e){ bv_BPF_FREE(c); goto out; }
    { u64 f; if(bv_lock_take(&bv_g_slot_lock,&f)){ for(int i=0;i<BPF_MAXPROGS;i++) if(!bv_g_progs[i].used){ p=&bv_g_progs[i]; bv_bz(p,sizeof *p); p->used=1; break; } bv_lock_drop(&bv_g_slot_lock,f); } }
    if(!p){ bv_BPF_FREE(c); e=-12; goto out; }
    p->refs=1; p->id=__atomic_add_fetch(&bv_g_bpf_id,1,__ATOMIC_RELAXED); p->type=type; p->len=n; p->insns=c; p->nmaps=nmaps;
    for(int i=0;i<nmaps;i++){ p->maps[i]=maps[i]; bpf_map_get(maps[i]); }
    return p;
out: if(err)*err=e; return 0;
}
void bpf_prog_get(struct bpf_prog* p){ if(p)__atomic_add_fetch(&p->refs,1,__ATOMIC_ACQ_REL); }
void bpf_prog_put(struct bpf_prog* p){ if(!p)return; if(__atomic_sub_fetch(&p->refs,1,__ATOMIC_ACQ_REL)==0){ for(int i=0;i<p->nmaps;i++)bpf_map_put(p->maps[i]); bv_BPF_FREE(p->insns); p->insns=0; p->used=0; } }

int bpf_slot_new(struct bpf_map* m,struct bpf_prog* p){
    if((!m)==(!p))return -22;
    int r=-24; u64 f; if(!bv_lock_take(&bv_g_slot_lock,&f))return -16;
    for(int i=0;i<bv_BPF_SLOTS;i++) if(!bv_g_slots[i].kind){ bv_g_slots[i].kind=m?1:2; bv_g_slots[i].obj=m?(void*)m:(void*)p; bv_g_slots[i].link=0; r=i; break; }
    bv_lock_drop(&bv_g_slot_lock,f); if(r>=0){ if(m)bpf_map_get(m); else bpf_prog_get(p); } return r;
}
struct bpf_map* bpf_slot_map(int s){ return s>=0&&s<bv_BPF_SLOTS&&bv_g_slots[s].kind==1?(struct bpf_map*)bv_g_slots[s].obj:0; }
struct bpf_prog* bpf_slot_prog(int s){ return s>=0&&s<bv_BPF_SLOTS&&bv_g_slots[s].kind==2?(struct bpf_prog*)bv_g_slots[s].obj:0; }
int bpf_slot_get(int s,struct bpf_map** m,struct bpf_prog** p){ *m=0; *p=0; if(s<0||s>=bv_BPF_SLOTS)return -9; u64 f; if(!bv_lock_take(&bv_g_slot_lock,&f))return -16; int k=bv_g_slots[s].kind; if(k==1){ *m=(struct bpf_map*)bv_g_slots[s].obj; bpf_map_get(*m); } else if(k==2){ *p=(struct bpf_prog*)bv_g_slots[s].obj; bpf_prog_get(*p); } bv_lock_drop(&bv_g_slot_lock,f); return k?0:-9; }
void bpf_link_hook(int (*put)(int)){ bv_g_link_put=put; }
int bpf_slot_set_link(int s,int id){ if(s<0||s>=bv_BPF_SLOTS||id<0)return -22; u64 f; if(!bv_lock_take(&bv_g_slot_lock,&f))return -16; int r=bv_g_slots[s].kind==2&&!bv_g_slots[s].link?0:-22; if(!r)bv_g_slots[s].link=id+1; bv_lock_drop(&bv_g_slot_lock,f); return r; }
void bpf_fd_close(int s){
    if(s<0||s>=bv_BPF_SLOTS)return;
    u64 f; if(!bv_lock_take(&bv_g_slot_lock,&f))return; int k=bv_g_slots[s].kind; void* o=bv_g_slots[s].obj; int lk=bv_g_slots[s].link; bv_g_slots[s].kind=0; bv_g_slots[s].obj=0; bv_g_slots[s].link=0; bv_lock_drop(&bv_g_slot_lock,f);
    if(lk&&bv_g_link_put)bv_g_link_put(lk-1);
    if(k==1)bpf_map_put((struct bpf_map*)o); else if(k==2)bpf_prog_put((struct bpf_prog*)o);
}

static int bv_ld_pkt(const struct bpf_skb_ctx* s,u64 off,u32 sz,u64* out){
    if(!s||!s->data||off>s->len||sz>s->len-off)return 0;
    const u8* d=s->data+off;
    if(sz==1)*out=d[0]; else if(sz==2)*out=((u32)d[0]<<8)|d[1]; else *out=((u32)d[0]<<24)|((u32)d[1]<<16)|((u32)d[2]<<8)|d[3]; return 1;
}
static u64 bv_bswap(u64 v,int w){ if(w==16)return (u16)((v>>8)|(v<<8)); if(w==32)return __builtin_bswap32((u32)v); return __builtin_bswap64(v); }

u64 bpf_run_insns(const struct bpf_insn* in,u32 n,struct bpf_map** maps,void* ctx,u32 type){
    u64 r[11]; u64 stk[BPF_STACK/8]; bv_bz(r,sizeof r); bv_bz(stk,sizeof stk);
    r[1]=(u64)ctx; r[10]=(u64)((u8*)stk+BPF_STACK);
    u32 pc=0, budget=0; const struct bpf_ctx_desc* cd=bpf_ctx_get(type);
    while(pc<n){
        if(++budget>BPF_MAXINSNS*2)return 0;
        const struct bpf_insn* i=&in[pc]; u8 c=i->code; u8 cl=BPF_CLASS(c); u64* d=&r[i->dst&15]; u64 s=BPF_SRC(c)==BPF_X?r[i->src&15]:(u64)(i64)i->imm;
        if((i->dst&15)>10||(i->src&15)>10)return 0;
        if(cl==BPF_ALU64||cl==BPF_ALU){
            int w=cl==BPF_ALU64; u64 a=*d; if(!w){ a=(u32)a; s=(u32)s; } u64 v=a;
            switch(BPF_OP(c)){
            case BPF_ADD: v=a+s; break; case BPF_SUB: v=a-s; break; case BPF_MUL: v=a*s; break;
            case BPF_DIV: v=s?a/s:0; break; case BPF_MOD: v=s?a%s:a; break;
            case BPF_OR: v=a|s; break; case BPF_AND: v=a&s; break; case BPF_XOR: v=a^s; break;
            case BPF_LSH: v=a<<(s&(w?63:31)); break; case BPF_RSH: v=a>>(s&(w?63:31)); break;
            case BPF_ARSH: v=w?(u64)((i64)a>>(s&63)):(u64)(u32)((int)(u32)a>>(s&31)); break;
            case BPF_NEG: v=-a; break; case BPF_MOV: v=s; break;
            case BPF_END: { int bw=i->imm; if(bw!=16&&bw!=32&&bw!=64)return 0; v=BPF_SRC(c)==BPF_TO_BE?bv_bswap(*d,bw):(bw==64?*d:*d&((1ull<<bw)-1)); w=1; break; }
            default: return 0; }
            *d=w?v:(u32)v; pc++; continue;
        }
        if(cl==BPF_JMP||cl==BPF_JMP32){
            u8 op=BPF_OP(c);
            if(op==BPF_EXIT)return r[0];
            if(op==BPF_CALL){ const struct bpf_helper* h=bpf_helper_get((u32)i->imm,type); if(!h)return 0; r[0]=h->fn(r[1],r[2],r[3],r[4],r[5]); pc++; continue; }
            if(op==BPF_JA){ pc=pc+1+i->off; continue; }
            u64 a=*d, b=s; if(cl==BPF_JMP32){ a=(u32)a; b=(u32)b; }
            i64 sa=cl==BPF_JMP32?(i64)(int)(u32)a:(i64)a, sb=cl==BPF_JMP32?(i64)(int)(u32)b:(i64)b; int t;
            switch(op){ case BPF_JEQ: t=a==b; break; case BPF_JNE: t=a!=b; break; case BPF_JGT: t=a>b; break; case BPF_JGE: t=a>=b; break;
            case BPF_JLT: t=a<b; break; case BPF_JLE: t=a<=b; break; case BPF_JSET: t=(a&b)!=0; break;
            case BPF_JSGT: t=sa>sb; break; case BPF_JSGE: t=sa>=sb; break; case BPF_JSLT: t=sa<sb; break; case BPF_JSLE: t=sa<=sb; break; default: return 0; }
            pc=t?pc+1+i->off:pc+1; continue;
        }
        if(c==(BPF_LD|BPF_DW|BPF_IMM)){ if(pc+1>=n)return 0; if(i->src==BPF_PSEUDO_MAP_FD||i->src==BPF_PSEUDO_MAP_IDX){ if(!maps||i->imm<0||i->imm>=BPF_PROG_MAPS)return 0; *d=(u64)maps[i->imm]; } else *d=(u64)(u32)i->imm|((u64)(u32)in[pc+1].imm<<32); pc+=2; continue; }
        if(cl==BPF_LD&&(BPF_MODE(c)==BPF_ABS||BPF_MODE(c)==BPF_IND)){
            if(!cd||!cd->pkt)return 0;
            u32 sz=BPF_SIZE(c)==BPF_B?1:BPF_SIZE(c)==BPF_H?2:4; u64 off=(u64)(i64)i->imm; if(BPF_MODE(c)==BPF_IND)off+=(u32)r[i->src&15];
            u64 v; if(!bv_ld_pkt((const struct bpf_skb_ctx*)r[6],off,sz,&v))return 0; r[0]=v; for(int k=1;k<=5;k++)r[k]=0; pc++; continue;
        }
        if(cl==BPF_LDX||cl==BPF_ST||cl==BPF_STX){
            u32 sz=BPF_SIZE(c)==BPF_B?1:BPF_SIZE(c)==BPF_H?2:BPF_SIZE(c)==BPF_W?4:8;
            if(cl==BPF_LDX){ if(BPF_MODE(c)!=BPF_MEM)return 0; u8* p=(u8*)(r[i->src&15]+(i64)i->off); u64 v=0; bv_bcp(&v,p,sz); *d=v; pc++; continue; }
            u8* p=(u8*)(*d+(i64)i->off);
            if(BPF_MODE(c)==BPF_ATOMIC){ if(cl!=BPF_STX||(sz!=4&&sz!=8))return 0; u64 old; int fetch=(i->imm&BPF_FETCH)!=0; if((i->imm&~BPF_FETCH)!=BPF_ADD)return 0;
                if(sz==8)old=__atomic_fetch_add((u64*)p,r[i->src&15],__ATOMIC_SEQ_CST); else old=__atomic_fetch_add((u32*)p,(u32)r[i->src&15],__ATOMIC_SEQ_CST);
                if(fetch)r[i->src&15]=sz==8?old:(u32)old;
                pc++; continue; }
            if(BPF_MODE(c)!=BPF_MEM)return 0;
            u64 v=cl==BPF_ST?(u64)(i64)i->imm:r[i->src&15]; bv_bcp(p,&v,sz); pc++; continue;
        }
        return 0;
    }
    return 0;
}
u64 bpf_prog_run(struct bpf_prog* p,void* ctx){
    if(!p||!p->insns)return 0;
    u64 v=bpf_run_insns(p->insns,p->len,p->maps,ctx,p->type); p->run_cnt++; return v;
}
