#include "include/nvx/sys/bpf.h"
#ifdef BPF_HOST
#include <stdlib.h>
#define bv_V_ALLOC(n) calloc(1,(n))
#define bv_V_FREE(p) free(p)
#else
#include "include/nvx/mem/heap.h"
#define bv_V_ALLOC(n) kcalloc(1,(n))
#define bv_V_FREE(p) kfree(p)
#endif

enum { bv_T_NONE=0, bv_T_SCALAR, bv_T_CTX, bv_T_STACK, bv_T_MAPV, bv_T_MAPV_NULL, bv_T_MAP };
struct bv_vreg { u8 t; signed char map; u16 id; int omin, omax; u64 umin, umax; };
struct bv_vst { u32 pc; u16 nid; struct bv_vreg r[11]; u8 init[BPF_STACK/8]; u64 spill_mask; struct bv_vreg sp[BPF_STACK/8]; };
#define bv_VPEND 96
struct bv_venv { u32 type; const struct bpf_insn* in; u32 n; struct bpf_map** maps; int nmaps; char* log; u32 logsz, logn; const struct bpf_ctx_desc* cd; u8* seen; u32 work; struct bv_vst cur; struct bv_vst pend[bv_VPEND]; int npend; };

static void bv_vz(void* d,u64 n){ u8* p=(u8*)d; while(n--)*p++=0; }
static void bv_vlog(struct bv_venv* e,const char* m,u32 pc){
    if(!e->log||!e->logsz)return;
    char b[96]; u32 k=0; while(*m&&k<70)b[k++]=*m++; b[k++]=' '; b[k++]='@';
    char t[12]; int j=0; do{ t[j++]=(char)('0'+pc%10); pc/=10; }while(pc&&j<11); while(j)b[k++]=t[--j]; b[k++]='\n';
    for(u32 i=0;i<k&&e->logn+1<e->logsz;i++)e->log[e->logn++]=b[i];
    e->log[e->logn]=0;
}
#define bv_REJ(m) do{ bv_vlog(e,m,pc); return -1; }while(0)

static void bv_sc_unknown(struct bv_vreg* r){ bv_vz(r,sizeof *r); r->t=bv_T_SCALAR; r->umax=~0ull; }
static void bv_sc_const(struct bv_vreg* r,u64 v){ bv_vz(r,sizeof *r); r->t=bv_T_SCALAR; r->umin=r->umax=v; }
static void bv_sc_range(struct bv_vreg* r,u64 lo,u64 hi){ bv_vz(r,sizeof *r); r->t=bv_T_SCALAR; r->umin=lo; r->umax=hi; }
static int bv_is_ptr(const struct bv_vreg* r){ return r->t>=bv_T_CTX; }
static int bv_sz_of(u8 c){ return BPF_SIZE(c)==BPF_B?1:BPF_SIZE(c)==BPF_H?2:BPF_SIZE(c)==BPF_W?4:8; }

static int bv_stk_init(struct bv_vst* s,int lo,int hi){ for(int b=lo;b<hi;b++){ if(b<0||b>=BPF_STACK)return 0; if(!(s->init[b>>3]&(1u<<(b&7))))return 0; } return 1; }
static void bv_stk_mark(struct bv_vst* s,int lo,int hi){ for(int b=lo;b<hi;b++) if(b>=0&&b<BPF_STACK)s->init[b>>3]|=(u8)(1u<<(b&7)); }
static void bv_stk_clobber(struct bv_vst* s,int lo,int hi){
    for(int sl=lo>>3;sl<=(hi-1)>>3&&sl<BPF_STACK/8;sl++){ if(sl<0)continue; if(s->spill_mask&(1ull<<sl)){ s->spill_mask&=~(1ull<<sl); if(bv_is_ptr(&s->sp[sl]))for(int b=sl*8;b<sl*8+8;b++) if(b<lo||b>=hi)s->init[b>>3]&=(u8)~(1u<<(b&7)); } }
}

static int bv_mem_region(struct bv_venv* e,struct bv_vst* s,const struct bv_vreg* p,int off,u64 szmin,u64 szmax,int wr,int need_init,u32 pc){
    (void)szmin;
    if(szmax>65536)bv_REJ("access too large");
    i64 lo=(i64)p->omin+off, hi=(i64)p->omax+off+(i64)szmax;
    if(p->t==bv_T_STACK){
        if(p->omin!=p->omax)bv_REJ("variable stack offset");
        if(lo<-BPF_STACK||hi>0)bv_REJ("stack out of bounds");
        int a=(int)(lo+BPF_STACK), b=(int)(hi+BPF_STACK);
        if(need_init&&!bv_stk_init(s,a,b))bv_REJ("read of uninitialized stack");
        if(wr){ bv_stk_clobber(s,a,b); bv_stk_mark(s,a,b); }
        return 0;
    }
    if(p->t==bv_T_MAPV){
        if(p->map<0||p->map>=e->nmaps)bv_REJ("bad map");
        if(lo<0||hi>(i64)e->maps[(int)p->map]->value_size)bv_REJ("map value out of bounds");
        return 0;
    }
    if(p->t==bv_T_CTX){
        if(wr)bv_REJ("ctx is read-only");
        if(p->omin!=p->omax)bv_REJ("variable ctx offset");
        if(lo<0||hi>(i64)e->cd->rd_size)bv_REJ("ctx out of bounds");
        return 0;
    }
    bv_REJ("invalid memory pointer");
}

static int bv_ptr_in(struct bv_vst* s,const struct bv_vreg* p,u64 n){
    if(p->t!=bv_T_STACK)return 0;
    int a=p->omin+BPF_STACK, b=a+(int)n;
    for(int sl=0;sl<BPF_STACK/8;sl++) if((s->spill_mask&(1ull<<sl))&&bv_is_ptr(&s->sp[sl])&&sl*8<b&&sl*8+8>a)return 1;
    return 0;
}
static void bv_null_mark(struct bv_vst* s,u16 id,int isnull){
    for(int i=0;i<11;i++) if(s->r[i].t==bv_T_MAPV_NULL&&s->r[i].id==id){ if(isnull)bv_sc_const(&s->r[i],0); else s->r[i].t=bv_T_MAPV; }
    for(int k=0;k<BPF_STACK/8;k++) if((s->spill_mask&(1ull<<k))&&s->sp[k].t==bv_T_MAPV_NULL&&s->sp[k].id==id){ if(isnull)bv_sc_const(&s->sp[k],0); else s->sp[k].t=bv_T_MAPV; }
}

static int bv_alu(struct bv_venv* e,struct bv_vst* s,const struct bpf_insn* i,u32 pc){
    u8 c=i->code; int w=BPF_CLASS(c)==BPF_ALU64; u8 op=BPF_OP(c); struct bv_vreg* d=&s->r[i->dst];
    if(i->dst==10)bv_REJ("frame pointer is read-only");
    struct bv_vreg src; if(BPF_SRC(c)==BPF_X){ if(i->src>10)bv_REJ("bad register"); src=s->r[i->src]; if(src.t==bv_T_NONE)bv_REJ("read of uninitialized register"); if(i->imm||i->off)bv_REJ("reserved fields"); }
    else { if(i->src||i->off)bv_REJ("reserved fields"); bv_sc_const(&src,w?(u64)(i64)i->imm:(u64)(u32)i->imm); }
    if(op==BPF_END){ if(BPF_CLASS(c)!=BPF_ALU||(i->imm!=16&&i->imm!=32&&i->imm!=64)||i->src)bv_REJ("bad endian op"); if(d->t!=bv_T_SCALAR)bv_REJ("endian on non-scalar"); if(i->imm==64)bv_sc_unknown(d); else bv_sc_range(d,0,(1ull<<i->imm)-1); return 0; }
    if(op==BPF_MOV){
        if(w){ *d=src; return 0; }
        if(bv_is_ptr(&src))bv_REJ("32-bit mov of pointer");
        if(src.umax<=0xffffffffull)*d=src; else bv_sc_range(d,0,0xffffffffull); return 0;
    }
    if(op==BPF_NEG){ if(BPF_SRC(c)!=BPF_K||i->imm)bv_REJ("bad neg"); if(d->t!=bv_T_SCALAR)bv_REJ("neg on pointer/uninit"); if(w)bv_sc_unknown(d); else bv_sc_range(d,0,0xffffffffull); return 0; }
    if(d->t==bv_T_NONE)bv_REJ("read of uninitialized register");
    if(op>BPF_ARSH)bv_REJ("unknown alu op");
    if((op==BPF_LSH||op==BPF_RSH||op==BPF_ARSH)&&BPF_SRC(c)==BPF_K&&(u32)i->imm>=(u32)(w?64:32))bv_REJ("shift out of range");
    if((op==BPF_DIV||op==BPF_MOD)&&BPF_SRC(c)==BPF_K&&i->imm==0)bv_REJ("division by zero");
    if(bv_is_ptr(d)||bv_is_ptr(&src)){
        if(!w)bv_REJ("32-bit pointer arithmetic");
        if(bv_is_ptr(d)&&bv_is_ptr(&src))bv_REJ("pointer-pointer arithmetic");
        if(op!=BPF_ADD&&op!=BPF_SUB)bv_REJ("pointer arithmetic op");
        struct bv_vreg* pp=bv_is_ptr(d)?d:&src; struct bv_vreg* sc=bv_is_ptr(d)?&src:d;
        if(op==BPF_SUB&&!bv_is_ptr(d))bv_REJ("scalar minus pointer");
        if(pp->t!=bv_T_STACK&&pp->t!=bv_T_MAPV&&pp->t!=bv_T_CTX)bv_REJ("arithmetic on this pointer type");
        if(sc->umax>(1u<<29)&&!(op==BPF_ADD&&sc->umin>=0xffffffffe0000000ull))bv_REJ("pointer offset unbounded");
        struct bv_vreg r=*pp; i64 lo,hi;
        lo=(i64)sc->umin; hi=(i64)sc->umax;
        if(op==BPF_SUB){ i64 t=-hi; hi=-lo; lo=t; }
        i64 nlo=(i64)r.omin+lo, nhi=(i64)r.omax+hi;
        if(nlo<-(1<<29)||nhi>(1<<29))bv_REJ("pointer offset out of range");
        r.omin=(int)nlo; r.omax=(int)nhi; *d=r; return 0;
    }
    struct bv_vreg a=*d; u64 lo=0,hi=~0ull; int ok=0;
    u64 m=w?~0ull:0xffffffffull;
    if(!w){ if(a.umax>m){ a.umin=0; a.umax=m; } if(src.umax>m){ src.umin=0; src.umax=m; } }
    switch(op){
    case BPF_ADD: if(a.umax+src.umax>=a.umax&&a.umax+src.umax<=m){ lo=a.umin+src.umin; hi=a.umax+src.umax; ok=1; } break;
    case BPF_SUB: if(a.umin>=src.umax){ lo=a.umin-src.umax; hi=a.umax-src.umin; ok=1; } break;
    case BPF_MUL: if((!a.umax||src.umax<=m/a.umax)){ lo=a.umin*src.umin; hi=a.umax*src.umax; ok=1; } break;
    case BPF_AND: lo=0; hi=a.umax<src.umax?a.umax:src.umax; ok=1; if(a.umin==a.umax&&src.umin==src.umax){ lo=hi=a.umin&src.umin; } break;
    case BPF_DIV: if(src.umin>0){ lo=a.umin/src.umax; hi=a.umax/src.umin; ok=1; } else { lo=0; hi=a.umax; ok=1; } break;
    case BPF_MOD: lo=0; hi=a.umax; if(src.umax&&src.umax-1<hi&&src.umin>0)hi=src.umax-1; ok=1; break;
    case BPF_RSH: if(src.umin==src.umax&&src.umin<(u64)(w?64:32)){ lo=a.umin>>src.umin; hi=a.umax>>src.umin; ok=1; } else { lo=0; hi=a.umax; ok=1; } break;
    case BPF_LSH: if(src.umin==src.umax&&src.umin<(u64)(w?64:32)&&(a.umax<<src.umin>>src.umin)==a.umax&&(a.umax<<src.umin)<=m){ lo=a.umin<<src.umin; hi=a.umax<<src.umin; ok=1; } break;
    case BPF_OR: case BPF_XOR: if(a.umin==a.umax&&src.umin==src.umax){ lo=hi=op==BPF_OR?(a.umin|src.umin):(a.umin^src.umin); ok=1; } break;
    default: break; }
    if(ok&&!w&&hi>m)ok=0;
    if(ok)bv_sc_range(d,lo,hi); else bv_sc_range(d,0,m);
    return 0;
}

static int bv_push(struct bv_venv* e,struct bv_vst* s,u32 tgt,u32 pc){ if(e->npend>=bv_VPEND)bv_REJ("too many branches"); e->pend[e->npend]=*s; e->pend[e->npend].pc=tgt; e->npend++; return 0; }

static int bv_cond(struct bv_venv* e,struct bv_vst* s,const struct bpf_insn* i,u32 pc,u32 tgt){
    u8 c=i->code; u8 op=BPF_OP(c); int j32=BPF_CLASS(c)==BPF_JMP32; struct bv_vreg* d=&s->r[i->dst];
    if(d->t==bv_T_NONE)bv_REJ("read of uninitialized register");
    struct bv_vreg src; if(BPF_SRC(c)==BPF_X){ if(i->src>10)bv_REJ("bad register"); src=s->r[i->src]; if(src.t==bv_T_NONE)bv_REJ("read of uninitialized register"); if(i->imm)bv_REJ("reserved fields"); } else { if(i->src)bv_REJ("reserved fields"); bv_sc_const(&src,(u64)(i64)i->imm); }
    if(bv_is_ptr(d)||bv_is_ptr(&src)){
        struct bv_vreg* p=bv_is_ptr(d)?d:&src; struct bv_vreg* o=bv_is_ptr(d)?&src:d;
        if(p->t==bv_T_MAPV_NULL&&o->t==bv_T_SCALAR&&o->umin==0&&o->umax==0&&!j32&&(op==BPF_JEQ||op==BPF_JNE)){
            struct bv_vst t=*s; u16 id=p->id; bv_null_mark(&t,id,op==BPF_JEQ); if(bv_push(e,&t,tgt,pc))return -1; bv_null_mark(s,id,op!=BPF_JEQ); return 0; }
        bv_REJ("pointer comparison");
    }
    if(j32||(op!=BPF_JEQ&&op!=BPF_JNE&&op!=BPF_JGT&&op!=BPF_JGE&&op!=BPF_JLT&&op!=BPF_JLE)||src.umin!=src.umax){
        if(op!=BPF_JEQ&&op!=BPF_JNE&&op!=BPF_JGT&&op!=BPF_JGE&&op!=BPF_JLT&&op!=BPF_JLE&&op!=BPF_JSET&&op!=BPF_JSGT&&op!=BPF_JSGE&&op!=BPF_JSLT&&op!=BPF_JSLE)bv_REJ("unknown jump op");
        return bv_push(e,s,tgt,pc);
    }
    u64 k=src.umin; struct bv_vreg t=*d, f=*d; int tok=1, fok=1;
    switch(op){
    case BPF_JEQ: if(k<d->umin||k>d->umax)tok=0; else { t.umin=t.umax=k; } if(d->umin==k&&d->umax==k)fok=0; else { if(f.umin==k)f.umin++; else if(f.umax==k)f.umax--; } break;
    case BPF_JNE: if(k<d->umin||k>d->umax)fok=0; else { f.umin=f.umax=k; } if(d->umin==k&&d->umax==k)tok=0; else { if(t.umin==k)t.umin++; else if(t.umax==k)t.umax--; } break;
    case BPF_JGT: if(d->umax<=k)tok=0; else if(t.umin<k+1)t.umin=k+1; if(d->umin>k)fok=0; else if(f.umax>k)f.umax=k; break;
    case BPF_JGE: if(d->umax<k)tok=0; else if(t.umin<k)t.umin=k; if(d->umin>=k)fok=0; else if(f.umax>k-1)f.umax=k-1; break;
    case BPF_JLT: if(d->umin>=k)tok=0; else if(t.umax>k-1)t.umax=k-1; if(d->umax<k)fok=0; else if(f.umin<k)f.umin=k; break;
    case BPF_JLE: if(d->umin>k)tok=0; else if(t.umax>k)t.umax=k; if(d->umax<=k)fok=0; else if(f.umin<k+1)f.umin=k+1; break;
    }
    if(tok){ struct bv_vst ts=*s; ts.r[i->dst]=t; if(bv_push(e,&ts,tgt,pc))return -1; }
    if(!fok)return 1;
    s->r[i->dst]=f; return 0;
}

static int bv_call(struct bv_venv* e,struct bv_vst* s,const struct bpf_insn* i,u32 pc){
    if(i->src||i->dst||i->off)bv_REJ("unsupported call form");
    const struct bpf_helper* h=bpf_helper_get((u32)i->imm,e->type); if(!h)bv_REJ("unknown or disallowed helper");
    int mapi=-1; u32 id=(u32)i->imm;
    for(int a=0;a<5;a++){
        u8 at=h->arg[a]; struct bv_vreg* r=&s->r[a+1];
        if(at==BPF_ARG_NONE)continue;
        if(r->t==bv_T_NONE)bv_REJ("helper argument uninitialized");
        switch(at){
        case BPF_ARG_ANY: if(r->t!=bv_T_SCALAR)bv_REJ("pointer passed as scalar argument"); break;
        case BPF_ARG_CONST_MAP: { if(r->t!=bv_T_MAP)bv_REJ("expected map"); mapi=r->map; u32 mt=e->maps[mapi]->type;
            if(id==BPF_FUNC_ringbuf_output){ if(mt!=BPF_MAP_TYPE_RINGBUF)bv_REJ("ringbuf helper on non-ringbuf map"); }
            else if(mt==BPF_MAP_TYPE_RINGBUF)bv_REJ("map helper on ringbuf");
            if(id==BPF_FUNC_map_delete_elem&&mt!=BPF_MAP_TYPE_HASH)bv_REJ("delete on array map");
            break; }
        case BPF_ARG_MAP_KEY: case BPF_ARG_MAP_VALUE: { if(mapi<0)bv_REJ("map argument missing"); struct bpf_map* m=e->maps[mapi]; u32 n=at==BPF_ARG_MAP_KEY?m->key_size:m->value_size;
            if(r->t!=bv_T_STACK&&r->t!=bv_T_MAPV)bv_REJ("key/value must point to stack or map value");
            if(bv_mem_region(e,s,r,0,n,n,0,1,pc))return -1;
            if(bv_ptr_in(s,r,n))bv_REJ("helper would read a spilled pointer");
            break; }
        case BPF_ARG_MEM_RD: case BPF_ARG_MEM_WR: { if(a==4)bv_REJ("memory arg without size"); u8 nt=h->arg[a+1]; if(nt!=BPF_ARG_SIZE&&nt!=BPF_ARG_SIZE_OR_ZERO)bv_REJ("memory arg without size");
            struct bv_vreg* sz=&s->r[a+2]; if(sz->t!=bv_T_SCALAR)bv_REJ("size must be scalar"); if(nt==BPF_ARG_SIZE&&sz->umin==0)bv_REJ("size may be zero");
            if(sz->umax==0&&nt==BPF_ARG_SIZE_OR_ZERO)break;
            if(r->t!=bv_T_STACK&&r->t!=bv_T_MAPV)bv_REJ("memory arg must point to stack or map value");
            if(at==BPF_ARG_MEM_WR&&r->t==bv_T_STACK&&sz->umin!=sz->umax)bv_REJ("variable size stack write");
            if(at==BPF_ARG_MEM_RD&&bv_ptr_in(s,r,sz->umax))bv_REJ("helper would read a spilled pointer");
            if(bv_mem_region(e,s,r,0,sz->umin,sz->umax,at==BPF_ARG_MEM_WR,at==BPF_ARG_MEM_RD,pc))return -1;
            break; }
        case BPF_ARG_SIZE: case BPF_ARG_SIZE_OR_ZERO: if(r->t!=bv_T_SCALAR)bv_REJ("size must be scalar"); if(a==0||(h->arg[a-1]!=BPF_ARG_MEM_RD&&h->arg[a-1]!=BPF_ARG_MEM_WR))bv_REJ("size without memory arg"); break;
        case BPF_ARG_CTX: if(r->t!=bv_T_CTX||r->omin||r->omax)bv_REJ("expected ctx"); break;
        default: bv_REJ("bad helper signature");
        }
    }
    for(int k=1;k<=5;k++)bv_vz(&s->r[k],sizeof s->r[k]);
    if(h->ret==BPF_RET_MAP_VALUE_OR_NULL){ if(mapi<0)bv_REJ("map return without map"); bv_vz(&s->r[0],sizeof s->r[0]); s->r[0].t=bv_T_MAPV_NULL; s->r[0].map=(signed char)mapi; s->r[0].id=++s->nid; if(!s->nid)bv_REJ("too many ids"); }
    else if(h->ret==BPF_RET_VOID)bv_vz(&s->r[0],sizeof s->r[0]);
    else bv_sc_unknown(&s->r[0]);
    return 0;
}

static int bv_ldst(struct bv_venv* e,struct bv_vst* s,const struct bpf_insn* i,u32 pc){
    u8 c=i->code, cl=BPF_CLASS(c); int sz=bv_sz_of(c);
    if(cl==BPF_LDX){
        if(BPF_MODE(c)!=BPF_MEM||i->imm)bv_REJ("bad ldx");
        if(i->dst==10)bv_REJ("frame pointer is read-only");
        if(i->src>10)bv_REJ("bad register");
        struct bv_vreg p=s->r[i->src]; if(p.t==bv_T_NONE)bv_REJ("read of uninitialized register"); if(p.t==bv_T_MAPV_NULL)bv_REJ("possible null dereference");
        if(p.t!=bv_T_STACK&&p.t!=bv_T_MAPV&&p.t!=bv_T_CTX)bv_REJ("load through non-pointer");
        if((p.t==bv_T_STACK||p.t==bv_T_CTX)&&((p.omin+i->off)&(sz-1)))bv_REJ("misaligned access");
        if(bv_mem_region(e,s,&p,i->off,sz,sz,0,1,pc))return -1;
        if(p.t==bv_T_STACK){ int a=p.omin+i->off+BPF_STACK, sl=a>>3;
            if(s->spill_mask&(1ull<<sl)){ if(sz==8&&!(a&7)){ s->r[i->dst]=s->sp[sl]; return 0; } if(bv_is_ptr(&s->sp[sl]))bv_REJ("partial read of spilled pointer"); } }
        if(sz==8)bv_sc_unknown(&s->r[i->dst]); else bv_sc_range(&s->r[i->dst],0,(1ull<<(sz*8))-1);
        return 0;
    }
    if(i->dst>10)bv_REJ("bad register");
    struct bv_vreg p=s->r[i->dst]; if(p.t==bv_T_NONE)bv_REJ("store through uninitialized register"); if(p.t==bv_T_MAPV_NULL)bv_REJ("possible null dereference");
    if(p.t!=bv_T_STACK&&p.t!=bv_T_MAPV)bv_REJ("store through non-writable pointer");
    if(p.t==bv_T_STACK&&((p.omin+i->off)&(sz-1)))bv_REJ("misaligned access");
    struct bv_vreg v; if(cl==BPF_STX){ if(i->src>10)bv_REJ("bad register"); v=s->r[i->src]; if(v.t==bv_T_NONE)bv_REJ("store of uninitialized register"); if(i->imm&&BPF_MODE(c)!=BPF_ATOMIC)bv_REJ("reserved fields"); } else { if(i->src)bv_REJ("reserved fields"); bv_sc_const(&v,(u64)(i64)i->imm); }
    if(BPF_MODE(c)==BPF_ATOMIC){
        if(cl!=BPF_STX||(sz!=4&&sz!=8))bv_REJ("bad atomic");
        if((i->imm&~BPF_FETCH)!=BPF_ADD)bv_REJ("unsupported atomic op");
        if(v.t!=bv_T_SCALAR)bv_REJ("atomic with pointer");
        if(p.t==bv_T_STACK&&(p.omin+i->off)&(sz-1))bv_REJ("misaligned atomic");
        if(bv_mem_region(e,s,&p,i->off,sz,sz,0,1,pc))return -1;
        if(p.t==bv_T_STACK){ int a=p.omin+i->off+BPF_STACK; if(s->spill_mask&(1ull<<(a>>3))&&bv_is_ptr(&s->sp[a>>3]))bv_REJ("atomic on spilled pointer"); }
        if(p.t==bv_T_STACK){ int a=p.omin+i->off+BPF_STACK; if(s->spill_mask&(1ull<<(a>>3))){ s->spill_mask&=~(1ull<<(a>>3)); } }
        if(i->imm&BPF_FETCH)bv_sc_unknown(&s->r[i->src]);
        return 0;
    }
    if(BPF_MODE(c)!=BPF_MEM)bv_REJ("bad store mode");
    if(p.t==bv_T_MAPV&&bv_is_ptr(&v))bv_REJ("pointer leak into map");
    if(p.t==bv_T_STACK&&bv_is_ptr(&v)&&(sz!=8||((p.omin+i->off)&7)))bv_REJ("partial pointer spill");
    if(bv_mem_region(e,s,&p,i->off,sz,sz,1,0,pc))return -1;
    if(p.t==bv_T_STACK&&sz==8&&!((p.omin+i->off)&7)){ int sl=(p.omin+i->off+BPF_STACK)>>3; s->spill_mask|=1ull<<sl; s->sp[sl]=v; }
    return 0;
}

static int bv_run(struct bv_venv* e){
    struct bv_vst* s=&e->cur; u32 pc=s->pc;
    for(;;){
        if(pc>=e->n)bv_REJ("fell off end of program");
        if(++e->work>BPF_COMPLEXITY)bv_REJ("program too complex");
        const struct bpf_insn* i=&e->in[pc]; e->seen[pc]=1; u8 c=i->code, cl=BPF_CLASS(c);
        if(i->dst>10||(i->src>10&&!(c==(BPF_LD|BPF_DW|BPF_IMM))))bv_REJ("bad register");
        if(cl==BPF_ALU||cl==BPF_ALU64){ if(bv_alu(e,s,i,pc))return -1; pc++; continue; }
        if(cl==BPF_JMP||cl==BPF_JMP32){
            u8 op=BPF_OP(c);
            if(op==BPF_EXIT){ if(cl!=BPF_JMP||i->src||i->dst||i->off||i->imm||BPF_SRC(c))bv_REJ("bad exit"); if(s->r[0].t!=bv_T_SCALAR)bv_REJ("R0 must be an initialized scalar at exit"); return 0; }
            if(op==BPF_CALL){ if(cl!=BPF_JMP||BPF_SRC(c))bv_REJ("bad call"); if(bv_call(e,s,i,pc))return -1; pc++; continue; }
            i64 t=(i64)pc+1+i->off;
            if(t<=(i64)pc)bv_REJ("back-edge (loop) rejected");
            if(t>=(i64)e->n)bv_REJ("jump out of range");
            if(t>0&&e->in[t-1].code==(BPF_LD|BPF_DW|BPF_IMM))bv_REJ("jump into ld_imm64");
            if(op==BPF_JA){ if(cl!=BPF_JMP||i->src||i->dst||i->imm||BPF_SRC(c))bv_REJ("bad ja"); pc=(u32)t; continue; }
            int r=bv_cond(e,s,i,pc,(u32)t); if(r<0)return -1; if(r==1)return 0; pc++; continue;
        }
        if(c==(BPF_LD|BPF_DW|BPF_IMM)){
            if(pc+1>=e->n)bv_REJ("truncated ld_imm64");
            const struct bpf_insn* h=&e->in[pc+1];
            if(h->code||h->dst||h->src||h->off||i->off)bv_REJ("bad ld_imm64");
            if(i->dst==10)bv_REJ("frame pointer is read-only");
            if(i->src==BPF_PSEUDO_MAP_IDX){ if(i->imm<0||i->imm>=e->nmaps||h->imm||!e->maps[i->imm])bv_REJ("bad map reference"); bv_vz(&s->r[i->dst],sizeof(struct bv_vreg)); s->r[i->dst].t=bv_T_MAP; s->r[i->dst].map=(signed char)i->imm; }
            else if(i->src==0)bv_sc_const(&s->r[i->dst],(u64)(u32)i->imm|((u64)(u32)h->imm<<32));
            else bv_REJ("unsupported ld_imm64 source");
            e->seen[pc+1]=1; pc+=2; continue;
        }
        if(cl==BPF_LD){
            if(BPF_MODE(c)!=BPF_ABS&&BPF_MODE(c)!=BPF_IND)bv_REJ("bad ld");
            if(!e->cd->pkt)bv_REJ("packet access not allowed for this program type");
            if(BPF_SIZE(c)==BPF_DW||i->dst||i->off)bv_REJ("bad ld_abs");
            if(s->r[6].t!=bv_T_CTX||s->r[6].omin||s->r[6].omax)bv_REJ("ld_abs requires ctx in R6");
            if(BPF_MODE(c)==BPF_IND){ if(s->r[i->src].t!=bv_T_SCALAR)bv_REJ("ld_ind index must be scalar"); } else if(i->src)bv_REJ("reserved fields");
            for(int k=1;k<=5;k++)bv_vz(&s->r[k],sizeof s->r[k]);
            { int sz=bv_sz_of(c); bv_sc_range(&s->r[0],0,(1ull<<(sz*8))-1); }
            pc++; continue;
        }
        if(cl==BPF_LDX||cl==BPF_ST||cl==BPF_STX){ if(bv_ldst(e,s,i,pc))return -1; pc++; continue; }
        bv_REJ("unknown instruction");
    }
}

int bpf_verify(u32 type,struct bpf_insn* in,u32 n,struct bpf_map** maps,int nmaps,char* log,u32 logsz){
    if(log&&logsz)log[0]=0;
    if(!in||n==0||n>BPF_MAXINSNS||nmaps<0||nmaps>BPF_PROG_MAPS||(nmaps&&!maps))return -22;
    const struct bpf_ctx_desc* cd=bpf_ctx_get(type); if(!cd)return -22;
    for(int k=0;k<nmaps;k++) if(!maps[k])return -22;
    struct bv_venv* e=(struct bv_venv*)bv_V_ALLOC(sizeof(struct bv_venv)); if(!e)return -12;
    u8* seen=(u8*)bv_V_ALLOC(n); if(!seen){ bv_V_FREE(e); return -12; }
    e->type=type; e->in=in; e->n=n; e->maps=maps; e->nmaps=nmaps; e->log=log; e->logsz=logsz; e->cd=cd; e->seen=seen;
    bv_vz(&e->cur,sizeof e->cur); e->cur.r[1].t=bv_T_CTX; e->cur.r[10].t=bv_T_STACK; e->cur.pc=0;
    int rc=0;
    for(;;){
        if(bv_run(e)){ rc=-13; break; }
        if(!e->npend)break;
        e->npend--; e->cur=e->pend[e->npend];
    }
    if(!rc) for(u32 k=0;k<n;k++) if(!seen[k]){ u32 pc=k; bv_vlog(e,"unreachable instruction",pc); rc=-22; break; }
    bv_V_FREE(seen); bv_V_FREE(e);
    return rc;
}
