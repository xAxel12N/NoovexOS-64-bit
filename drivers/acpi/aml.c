#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/klibc.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/drivers/acpi/aml.h"


#define AML_MAXCREATED 64
#define AML_MAXFIELD (1u<<18)
typedef struct aframe { anode* scope; aobj loc[8]; aobj arg[7]; int nargs; aobj ret; int flow; anode* created[AML_MAXCREATED]; int ncreated; int depth; } aframe;
anode* g_aroot; int g_aml_rev=2, g_aml_ok=0, g_aml_err=0; static u32 g_aml_budget=0; u32 g_aml_nodes=0, g_aml_loaded_bytes=0;
char g_aml_lasterr[64]; static int g_aml_nest=0; static u32 g_aml_sleep_left=0;
typedef struct { anode* node; u32 val; } anotify; static anotify g_anq[32]; static int g_anq_h, g_anq_t;
int (*aml_rd_hook)(u8 space,u64 addr,int width,u64* val,anode* region);
int (*aml_wr_hook)(u8 space,u64 addr,int width,u64 val,anode* region);
void (*aml_sleep_hook)(u32 ms);
u64 (*aml_timer_hook)(void);
u64 (*aml_now_hook)(void);
void (*aml_stall_hook)(u32 us);
int (*aml_gl_hook)(int acquire);
u64 g_aml_deadline=0, g_aml_hard_deadline=0; u32 g_aml_time_ms=3000, g_aml_sleep_pool=3000, g_aml_stall_us=0; static int g_aml_gl_depth=0; static struct anode* g_aml_glnode;
int aml_gl(int acq){ if(!aml_gl_hook)return 0; if(acq){ if(g_aml_gl_depth++==0)return aml_gl_hook(1); return 0; } if(g_aml_gl_depth>0&&--g_aml_gl_depth==0)aml_gl_hook(0); return 0; }
static void* aml_alloc(u32 n){ void* p=kmalloc(n?n:1); if(p){ u8* q=(u8*)p; for(u32 i=0;i<n;i++)q[i]=0; } return p; }
static void aml_set_err(const char* m){ int i=0; while(m[i]&&i<63){ g_aml_lasterr[i]=m[i]; i++; } g_aml_lasterr[i]=0; g_aml_err++; }
static u64 aml_mask(u64 v){ return g_aml_rev<2?(v&0xFFFFFFFFULL):v; }
static u64 aml_ones(void){ return g_aml_rev<2?0xFFFFFFFFULL:~0ULL; }
void aobj_free(aobj* o){
    if(o->type==AT_STR||o->type==AT_BUF||o->type==AT_NAMESTR){ if(o->p)kfree(o->p); }
    else if(o->type==AT_PKG){ if(o->e){ for(u32 k=0;k<o->len;k++)aobj_free(&o->e[k]); kfree(o->e); } }
    else if(o->type==AT_REF&&o->own&&o->cont){ aobj_free(o->cont); kfree(o->cont); }
    o->own=0; o->type=AT_NONE; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->node=0; o->cont=0; }
void aobj_int(aobj* o,u64 v){ aobj_free(o); o->type=AT_INT; o->i=aml_mask(v); }
static int aobj_bytes(aobj* o,u8 type,const u8* src,u32 n){ aobj_free(o); o->type=type; o->len=n; o->p=(u8*)aml_alloc(n+1); if(!o->p){ o->type=AT_NONE; return -1; } if(src)for(u32 k=0;k<n;k++)o->p[k]=src[k]; o->p[n]=0; return 0; }
static int aobj_copy(aobj* d,const aobj* s){
    if(d==s)return 0;
    aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; t.len=0;
    switch(s->type){
        case AT_INT: t=*s; break;
        case AT_STR: case AT_BUF: case AT_NAMESTR: if(aobj_bytes(&t,s->type,s->p,s->len))return -1; break;
        case AT_PKG: t.type=AT_PKG; t.len=s->len; t.e=(aobj*)aml_alloc((u32)sizeof(aobj)*(s->len?s->len:1)); if(!t.e)return -1; for(u32 k=0;k<s->len;k++)aobj_copy(&t.e[k],&s->e[k]); break;
        case AT_REF: t=*s; if(s->own&&s->cont){ aobj* c=(aobj*)aml_alloc(sizeof(aobj)); if(!c)return -1; aobj_copy(c,s->cont); t.cont=c; } break;
        default: t.type=AT_NONE; }
    aobj_free(d); *d=t; return 0; }
static int aml_segok(u8 c,int first){ return (c>='A'&&c<='Z')||c=='_'||(!first&&c>='0'&&c<='9'); }
static int anode_sane(const anode* c){ u64 a=(u64)c; return a>=0x100000ULL&&a<0x100000000ULL&&!(a&7); }
static anode* anode_child(anode* p,const char* seg){ if(p&&!anode_sane(p))return 0; for(anode* c=p?p->child:0;c&&anode_sane(c);c=c->next) if(c->nm[0]==seg[0]&&c->nm[1]==seg[1]&&c->nm[2]==seg[2]&&c->nm[3]==seg[3])return c; return 0; }
static anode* anode_new(anode* parent,const char* seg,u8 kind){
    anode* n=anode_child(parent,seg); if(n){ if(n->kind==K_SCOPE&&kind!=K_SCOPE)n->kind=kind; return n; }
    n=(anode*)aml_alloc(sizeof(anode)); if(!n)return 0;
    for(int k=0;k<4;k++)n->nm[k]=seg[k]; n->parent=parent; n->kind=kind; n->next=parent->child; parent->child=n; g_aml_nodes++; return n; }
static void anode_unlink(anode* n){ anode* p=n->parent; if(!p)return; anode** pp=&p->child; while(*pp&&*pp!=n)pp=&(*pp)->next; if(*pp)*pp=n->next; aobj_free(&n->v); kfree(n); if(g_aml_nodes)g_aml_nodes--; }
typedef struct { int abs, up, nseg; char seg[16][4]; } apath;
static int aml_parse_name(const u8** pp,const u8* end,apath* a){
    const u8* p=*pp; a->abs=0; a->up=0; a->nseg=0;
    if(p<end&&*p=='\\'){ a->abs=1; p++; } else while(p<end&&*p=='^'){ a->up++; p++; }
    if(p>=end)return -1;
    int n=1; if(*p==0x00){ p++; *pp=p; return 0; } else if(*p==0x2E){ n=2; p++; } else if(*p==0x2F){ if(p+1>=end)return -1; n=p[1]; p+=2; if(n<1||n>16)return -1; }
    if(p+4*n>end)return -1;
    for(int s=0;s<n;s++){ for(int k=0;k<4;k++){ if(!aml_segok(p[k],k==0))return -1; a->seg[s][k]=(char)p[k]; } p+=4; }
    a->nseg=n; *pp=p; return 0; }
static int aml_is_name_start(u8 c){ return c=='\\'||c=='^'||c==0x2E||c==0x2F||(c>='A'&&c<='Z')||c=='_'; }
static anode* aml_lookup(anode* scope,const apath* a){
    if(!a->nseg)return 0;
    anode* base=a->abs?g_aroot:scope;
    for(int u=0;u<a->up;u++){ if(base->parent)base=base->parent; }
    if(a->abs||a->up||a->nseg>1){ anode* n=base; for(int s=0;s<a->nseg&&n;s++)n=anode_child(n,a->seg[s]); return n; }
    for(anode* sc=base;sc;sc=sc->parent){ anode* n=anode_child(sc,a->seg[0]); if(n)return n; }
    return 0; }
static anode* aml_resolve_alias(anode* n){ for(int g=0;n&&n->kind==K_ALIAS&&g<8;g++)n=n->target; return n; }
static anode* aml_create(anode* scope,const apath* a,u8 kind){
    if(!a->nseg)return 0;
    anode* base=a->abs?g_aroot:scope; for(int u=0;u<a->up;u++) if(base->parent)base=base->parent;
    for(int s=0;s<a->nseg-1;s++){ anode* c=anode_child(base,a->seg[s]); if(!c)c=anode_new(base,a->seg[s],K_SCOPE); if(!c)return 0; base=c; }
    return anode_new(base,a->seg[a->nseg-1],kind); }
static int amli_pkglen(const u8** pp,const u8* end,const u8** pend){
    const u8* p=*pp; if(p>=end)return -1; u8 b0=*p; int nb=b0>>6; if(p+1+nb>end)return -1;
    u32 len; if(!nb)len=b0&0x3F; else { len=b0&0x0F; for(int k=0;k<nb;k++)len|=(u32)p[1+k]<<(4+8*k); }
    if(p+len>end||len<(u32)(nb+1))return -1; *pend=p+len; *pp=p+1+nb; return 0; }
u64 aobj_to_int(const aobj* o){
    if(o->type==AT_INT)return o->i;
    if(o->type==AT_BUF){ u64 v=0; u32 n=o->len<(u32)(g_aml_rev<2?4:8)?o->len:(u32)(g_aml_rev<2?4:8); for(u32 k=0;k<n;k++)v|=(u64)o->p[k]<<(8*k); return v; }
    if(o->type==AT_STR){ u64 v=0; const u8* s=o->p; u32 k=0; if(o->len>=2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))k=2;
        for(;k<o->len;k++){ u8 c=s[k]; int d; if(c>='0'&&c<='9')d=c-'0'; else if(c>='a'&&c<='f')d=c-'a'+10; else if(c>='A'&&c<='F')d=c-'A'+10; else break; v=(v<<4)|(u64)d; } return aml_mask(v); }
    return 0; }
static int aobj_to_buf(aobj* d,const aobj* s){
    if(s->type==AT_BUF||s->type==AT_STR){ u32 n=s->len+(s->type==AT_STR?1:0); if(s->type==AT_STR&&!s->len)n=0; aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; if(aobj_bytes(&t,AT_BUF,s->p,n))return -1; if(s->type==AT_STR&&n)t.p[n-1]=0; aobj_free(d); *d=t; return 0; }
    u64 v=aobj_to_int(s); u32 n=g_aml_rev<2?4:8; aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; if(aobj_bytes(&t,AT_BUF,0,n))return -1; for(u32 k=0;k<n;k++)t.p[k]=(u8)(v>>(8*k)); aobj_free(d); *d=t; return 0; }
static void aml_hexstr(aobj* d,u64 v){ char b[20]; int n=0; b[n++]='0'; b[n++]='x'; int st=0; for(int k=15;k>=0;k--){ int dg=(int)((v>>(4*k))&15); if(dg||st||k==0){ st=1; b[n++]="0123456789ABCDEF"[dg]; } } aobj_bytes(d,AT_STR,(const u8*)b,(u32)n); }
static void aml_decstr(aobj* d,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v&&n<22); char b[24]; for(int k=0;k<n;k++)b[k]=t[n-1-k]; aobj_bytes(d,AT_STR,(const u8*)b,(u32)n); }
static int aobj_to_str_sep(aobj* d,const aobj* s,char sep){
    if(s->type==AT_STR){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; aobj_copy(&t,s); aobj_free(d); *d=t; return 0; }
    if(s->type==AT_INT){ char b[16]; int nd=g_aml_rev<2?8:16; for(int k=0;k<nd;k++)b[k]="0123456789ABCDEF"[(s->i>>(4*(nd-1-k)))&15]; aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; aobj_bytes(&t,AT_STR,(const u8*)b,(u32)nd); aobj_free(d); *d=t; return 0; }
    if(s->type==AT_BUF){ u32 n=s->len*5; char* b=(char*)aml_alloc(n+1); if(!b)return -1; u32 o=0; for(u32 k=0;k<s->len;k++){ if(k)b[o++]=sep; b[o++]='0'; b[o++]='x'; b[o++]="0123456789ABCDEF"[s->p[k]>>4]; b[o++]="0123456789ABCDEF"[s->p[k]&15]; }
        aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; aobj_bytes(&t,AT_STR,(const u8*)b,o); kfree(b); aobj_free(d); *d=t; return 0; }
    aobj_bytes(d,AT_STR,0,0); return 0; }
static int aobj_to_str(aobj* d,const aobj* s){ return aobj_to_str_sep(d,s,' '); }
static int aml_hw_rd(u8 space,u64 addr,int w,u64* v,anode* r){ *v=0; if(aml_rd_hook)return aml_rd_hook(space,addr,w,v,r); return -1; }
static int aml_hw_wr(u8 space,u64 addr,int w,u64 v,anode* r){ if(aml_wr_hook)return aml_wr_hook(space,addr,w,v,r); return -1; }
static int aml_acc_bytes(u8 acc,u8 space){ if(space==3||space==7)return 1; switch(acc&0x0F){ case 2: return 2; case 3: return 4; case 4: return 8; default: return 1; } }
static int aml_field_read(anode* f,aobj* out);
static int aml_field_write(anode* f,const aobj* in);
static int aml_unit_rd(anode* f,u64 off,int w,u64* v){
    if(f->kind==K_INDEXFIELD){ aobj ix; ix.type=AT_NONE; ix.p=0; ix.e=0; ix.own=0; aobj_int(&ix,off); aml_field_write(f->idxf,&ix); aobj dv; dv.type=AT_NONE; dv.p=0; dv.e=0; dv.own=0; int r=aml_field_read(f->dataf,&dv); *v=aobj_to_int(&dv); aobj_free(&dv); return r; }
    anode* rg=f->region; if(!rg)return -1;
    if(f->kind==K_BANKFIELD&&f->bankf){ aobj bv; bv.type=AT_NONE; bv.p=0; bv.e=0; bv.own=0; aobj_int(&bv,f->bankval); aml_field_write(f->bankf,&bv); }
    if(off+(u64)w>rg->rlen&&rg->rlen)return -1;
    if(rg->space==3){ u64 acc=0; for(int b=0;b<w;b++){ u64 x=0; if(aml_hw_rd(3,rg->base+off+(u64)b,1,&x,rg))return -1; acc|=(x&0xFF)<<(8*b); } *v=acc; return 0; }
    return aml_hw_rd(rg->space,rg->base+off,w,v,rg); }
static int aml_unit_wr(anode* f,u64 off,int w,u64 v){
    if(f->kind==K_INDEXFIELD){ aobj ix; ix.type=AT_NONE; ix.p=0; ix.e=0; ix.own=0; aobj_int(&ix,off); aml_field_write(f->idxf,&ix); aobj dv; dv.type=AT_NONE; dv.p=0; dv.e=0; dv.own=0; aobj_int(&dv,v); return aml_field_write(f->dataf,&dv); }
    anode* rg=f->region; if(!rg)return -1;
    if(f->kind==K_BANKFIELD&&f->bankf){ aobj bv; bv.type=AT_NONE; bv.p=0; bv.e=0; bv.own=0; aobj_int(&bv,f->bankval); aml_field_write(f->bankf,&bv); }
    if(off+(u64)w>rg->rlen&&rg->rlen)return -1;
    if(rg->space==3){ for(int b=0;b<w;b++) if(aml_hw_wr(3,rg->base+off+(u64)b,1,(v>>(8*b))&0xFF,rg))return -1; return 0; }
    return aml_hw_wr(rg->space,rg->base+off,w,v,rg); }
static u8 aml_field_space(anode* f){ return (f->kind==K_INDEXFIELD)?0:(f->region?f->region->space:0); }
static int aml_field_read(anode* f,aobj* out){
    if(!f)return -1; f=aml_resolve_alias(f);
    if(f->bitlen>AML_MAXFIELD){ aobj_int(out,0); aml_set_err("field too large"); return -1; }
    if(f->lock&&f->kind!=K_BUFFIELD){ aml_gl(1); f->lock=0; int r=aml_field_read(f,out); f->lock=1; aml_gl(0); return r; }
    if(f->kind==K_BUFFIELD){ aobj* src=f->bnode?&f->bnode->v:f->bsrc; if(!src||(src->type!=AT_BUF&&src->type!=AT_STR)){ aobj_int(out,0); return -1; }
        u32 bo=f->bitoff, bl=f->bitlen;
        if(bl>64){ u32 nb=(bl+7)/8; aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; aobj_bytes(&t,AT_BUF,0,nb); for(u32 k=0;k<bl;k++){ u32 sb=bo+k; if(sb/8<src->len&&(src->p[sb/8]>>(sb%8))&1)t.p[k/8]|=(u8)(1u<<(k%8)); } aobj_free(out); *out=t; return 0; }
        if(f->cfld){ u32 nb=(bl+7)/8; aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; if(aobj_bytes(&t,AT_BUF,0,nb))return -1; for(u32 k=0;k<bl;k++){ u32 sb=bo+k; if(sb/8<src->len&&(src->p[sb/8]>>(sb%8))&1)t.p[k/8]|=(u8)(1u<<(k%8)); } aobj_free(out); *out=t; return 0; }
        u64 v=0; for(u32 k=0;k<bl;k++){ u32 sb=bo+k; if(sb/8<src->len&&(src->p[sb/8]>>(sb%8))&1)v|=1ULL<<k; } aobj_int(out,v); return 0; }
    int w=aml_acc_bytes(f->acc,aml_field_space(f)); u32 bo=f->bitoff, bl=f->bitlen; if(!bl){ aobj_int(out,0); return 0; }
    u32 nb=(bl+7)/8; u8 tmp[64]; u8* res=tmp; u8* heap=0; if(nb>sizeof(tmp)){ heap=(u8*)aml_alloc(nb); if(!heap)return -1; res=heap; } for(u32 k=0;k<nb;k++)res[k]=0;
    u64 first=(bo/8)/(u32)w*(u32)w, last=((bo+bl-1)/8)/(u32)w*(u32)w; int rc=0;
    for(u64 u=first;u<=last;u+=(u64)w){ u64 v=0; if(aml_unit_rd(f,u,w,&v))rc=-1;
        for(int b=0;b<8*w;b++){ u64 abit=u*8+(u64)b; if(abit<bo||abit>=(u64)bo+bl)continue; u32 rb=(u32)(abit-bo); if((v>>b)&1)res[rb/8]|=(u8)(1u<<(rb%8)); } }
    if(bl>64){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; aobj_bytes(&t,AT_BUF,res,nb); aobj_free(out); *out=t; }
    else { u64 v=0; for(u32 k=0;k<nb&&k<8;k++)v|=(u64)res[k]<<(8*k); aobj_int(out,v); }
    if(heap)kfree(heap); return rc; }
static int aml_field_write(anode* f,const aobj* in){
    if(!f)return -1; f=aml_resolve_alias(f);
    if(f->bitlen>AML_MAXFIELD){ aml_set_err("field too large"); return -1; }
    if(f->lock&&f->kind!=K_BUFFIELD){ aml_gl(1); f->lock=0; int r=aml_field_write(f,in); f->lock=1; aml_gl(0); return r; }
    u32 bo=f->bitoff, bl=f->bitlen; u32 nb=(bl+7)/8; u8 tmp[64]; u8* src=tmp; u8* heap=0; if(nb>sizeof(tmp)){ heap=(u8*)aml_alloc(nb); if(!heap)return -1; src=heap; }
    for(u32 k=0;k<nb;k++)src[k]=0;
    if(in->type==AT_BUF||in->type==AT_STR){ for(u32 k=0;k<nb&&k<in->len;k++)src[k]=in->p[k]; } else { u64 v=aobj_to_int(in); for(u32 k=0;k<nb&&k<8;k++)src[k]=(u8)(v>>(8*k)); }
    if(f->kind==K_BUFFIELD){ aobj* dst=f->bnode?&f->bnode->v:f->bsrc; if(!dst||(dst->type!=AT_BUF&&dst->type!=AT_STR)){ if(heap)kfree(heap); return -1; }
        for(u32 k=0;k<bl;k++){ u32 db=bo+k; if(db/8>=dst->len)break; if((src[k/8]>>(k%8))&1)dst->p[db/8]|=(u8)(1u<<(db%8)); else dst->p[db/8]&=(u8)~(1u<<(db%8)); }
        if(heap)kfree(heap); return 0; }
    int w=aml_acc_bytes(f->acc,aml_field_space(f)); int rc=0;
    u64 first=(bo/8)/(u32)w*(u32)w, last=((bo+bl-1)/8)/(u32)w*(u32)w;
    for(u64 u=first;u<=last;u+=(u64)w){
        u64 lo=u*8, hi=lo+(u64)w*8; int full=(lo>=bo&&hi<=(u64)bo+bl);
        u64 v=0; if(!full){ if(f->upd==1)v=~0ULL; else if(f->upd==2)v=0; else if(aml_unit_rd(f,u,w,&v))rc=-1; }
        for(int b=0;b<8*w;b++){ u64 abit=lo+(u64)b; if(abit<bo||abit>=(u64)bo+bl)continue; u32 sb=(u32)(abit-bo); if((src[sb/8]>>(sb%8))&1)v|=1ULL<<b; else v&=~(1ULL<<b); }
        if(w<8)v&=(1ULL<<(8*w))-1ULL;
        if(aml_unit_wr(f,u,w,v))rc=-1; }
    if(heap)kfree(heap); return rc; }
static int aml_term(aframe* f,const u8** pp,const u8* end,aobj* out);
static int aml_term_in(aframe* f,const u8** pp,const u8* end,aobj* out);
static int aml_exec_list(aframe* f,const u8* p,const u8* end);
static int aml_call(anode* m,aobj* args,int nargs,aobj* ret,int depth);
static int aml_arg(aframe* f,const u8** pp,const u8* end,aobj* out){ out->type=AT_NONE; out->p=0; out->e=0; out->own=0; out->len=0; return aml_term(f,pp,end,out); }
static int aml_node_value(anode* n,aobj* out){
    n=aml_resolve_alias(n); if(!n){ aobj_int(out,0); return -1; }
    switch(n->kind){
        case K_NAME: return aobj_copy(out,&n->v);
        case K_FIELD: case K_INDEXFIELD: case K_BANKFIELD: case K_BUFFIELD: return aml_field_read(n,out);
        case K_METHOD: case K_NATIVE: return aml_call(n,0,0,out,0);
        default: aobj_free(out); out->type=AT_REF; out->rk=RK_NODE; out->node=n; return 0; } }
int aml_deref(aobj* r,aobj* out){
    if(r->type!=AT_REF){ return aobj_copy(out,r); }
    if(r->rk==RK_NODE)return aml_node_value(r->node,out);
    if(r->rk==RK_SLOT){ return r->cont?aobj_copy(out,r->cont):-1; }
    if(r->rk==RK_PKG){ if(!r->cont||r->cont->type!=AT_PKG||r->idx>=r->cont->len)return -1; aobj* el=&r->cont->e[r->idx]; if(el->type==AT_REF||el->type==AT_NAMESTR){ if(el->type==AT_REF)return aml_deref(el,out); } return aobj_copy(out,el); }
    if(r->rk==RK_BUF){ if(!r->cont||r->idx>=r->cont->len)return -1; aobj_int(out,r->cont->p[r->idx]); return 0; }
    return -1; }
typedef struct { int kind; aobj* slot; anode* node; aobj ref; } atarget;
enum { TG_NONE=0, TG_SLOT, TG_NODE, TG_DEBUG, TG_REF, TG_ARGREF };
static int aml_parse_target(aframe* f,const u8** pp,const u8* end,atarget* t){
    const u8* p=*pp; t->kind=TG_NONE; t->slot=0; t->node=0; t->ref.type=AT_NONE; t->ref.p=0; t->ref.e=0; t->ref.own=0;
    if(p>=end)return -1;
    u8 c=*p;
    if(c==0x00){ *pp=p+1; return 0; }
    if(c>=0x60&&c<=0x67){ t->kind=TG_SLOT; t->slot=&f->loc[c-0x60]; *pp=p+1; return 0; }
    if(c>=0x68&&c<=0x6E){ aobj* a=&f->arg[c-0x68]; *pp=p+1; if(a->type==AT_REF){ t->kind=TG_REF; t->ref=*a; } else { t->kind=TG_SLOT; t->slot=a; } return 0; }
    if(c==0x5B&&p+1<end&&p[1]==0x31){ t->kind=TG_DEBUG; *pp=p+2; return 0; }
    if(aml_is_name_start(c)){ apath a; if(aml_parse_name(&p,end,&a))return -1; *pp=p; anode* n=aml_resolve_alias(aml_lookup(f->scope,&a)); if(!n){ aml_set_err("store to unknown name"); t->kind=TG_NONE; return 0; } t->kind=TG_NODE; t->node=n; return 0; }
    aobj r; r.type=AT_NONE; r.p=0; r.e=0; r.own=0; if(aml_term(f,pp,end,&r))return -1;
    if(r.type==AT_REF&&!r.own){ t->kind=TG_REF; t->ref=r; return 0; }
    aobj_free(&r); t->kind=TG_NONE; return 0; }
static int aml_store_node(anode* n,const aobj* v){
    n=aml_resolve_alias(n); if(!n)return -1;
    if(n->kind==K_FIELD||n->kind==K_INDEXFIELD||n->kind==K_BANKFIELD||n->kind==K_BUFFIELD){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; if(v->type==AT_REF)aml_deref((aobj*)v,&t); else aobj_copy(&t,v); int r=aml_field_write(n,&t); aobj_free(&t); return r; }
    if(n->kind!=K_NAME){ n->kind=K_NAME; }
    aobj src; src.type=AT_NONE; src.p=0; src.e=0; src.own=0; aobj_copy(&src,v);
    if(n->v.type==AT_INT&&src.type!=AT_INT&&src.type!=AT_REF&&src.type!=AT_PKG){ u64 x=aobj_to_int(&src); aobj_int(&n->v,x); aobj_free(&src); return 0; }
    if(n->v.type==AT_BUF&&(src.type==AT_INT||src.type==AT_STR||src.type==AT_BUF)){ aobj b; b.type=AT_NONE; b.p=0; b.e=0; b.own=0; aobj_to_buf(&b,&src); for(u32 k=0;k<n->v.len;k++)n->v.p[k]=k<b.len?b.p[k]:0; aobj_free(&b); aobj_free(&src); return 0; }
    if(n->v.type==AT_STR&&src.type==AT_INT){ aobj s; s.type=AT_NONE; s.p=0; s.e=0; s.own=0; aobj_to_str(&s,&src); aobj_free(&n->v); n->v=s; aobj_free(&src); return 0; }
    aobj_free(&n->v); n->v=src; return 0; }
static int aml_store_ref(aobj* r,const aobj* v){
    if(r->rk==RK_NODE)return aml_store_node(r->node,v);
    if(r->rk==RK_SLOT){ if(!r->cont)return -1; return aobj_copy(r->cont,v); }
    if(r->rk==RK_PKG){ if(!r->cont||r->cont->type!=AT_PKG||r->idx>=r->cont->len)return -1; return aobj_copy(&r->cont->e[r->idx],v); }
    if(r->rk==RK_BUF){ if(!r->cont||r->idx>=r->cont->len)return -1; r->cont->p[r->idx]=(u8)aobj_to_int(v); return 0; }
    return -1; }
static int aml_store(aframe* f,atarget* t,const aobj* v){ (void)f;
    switch(t->kind){ case TG_SLOT: return aobj_copy(t->slot,v); case TG_NODE: return aml_store_node(t->node,v); case TG_REF: return aml_store_ref(&t->ref,v); default: return 0; } }
static int aml_get_target_value(aframe* f,atarget* t,aobj* out){ (void)f;
    switch(t->kind){ case TG_SLOT: if(t->slot->type==AT_REF)return aml_deref(t->slot,out); return aobj_copy(out,t->slot); case TG_NODE: return aml_node_value(t->node,out); case TG_REF: return aml_deref(&t->ref,out); default: aobj_int(out,0); return 0; } }
static int aml_cmp(const aobj* a,const aobj* b){
    if(a->type==AT_STR||a->type==AT_BUF){ aobj bb; bb.type=AT_NONE; bb.p=0; bb.e=0; bb.own=0; if(a->type==AT_STR)aobj_to_str(&bb,b); else aobj_to_buf(&bb,b);
        u32 n=a->len<bb.len?a->len:bb.len; int r=0; for(u32 k=0;k<n&&!r;k++){ if(a->p[k]<bb.p[k])r=-1; else if(a->p[k]>bb.p[k])r=1; } if(!r&&a->len!=bb.len)r=a->len<bb.len?-1:1; aobj_free(&bb); return r; }
    u64 x=aobj_to_int(a), y=aobj_to_int(b); return x<y?-1:x>y?1:0; }
static int aml_osi(aobj* args,int n,aobj* ret){
    static const char* const ok[]={"Windows 2000","Windows 2001","Windows 2001 SP1","Windows 2001.1","Windows 2001 SP2","Windows 2001.1 SP1","Windows 2006","Windows 2006.1","Windows 2006 SP1","Windows 2006 SP2","Windows 2009","Windows 2012","Windows 2013","Windows 2015","Windows 2016","Windows 2017","Windows 2017.2","Windows 2018","Windows 2018.2","Windows 2019","Windows 2020","Windows 2021","Windows 2022","Module Device","Processor Device","3.0 Thermal Model","3.0 _SCP Extensions","Processor Aggregator Device","Extended Address Space Descriptor"};
    u64 r=0; if(n>=1&&args[0].type==AT_STR) for(u32 k=0;k<sizeof(ok)/sizeof(ok[0]);k++){ const char* s=ok[k]; u32 l=0; while(s[l])l++; if(l==args[0].len){ int m=1; for(u32 q=0;q<l;q++) if((char)args[0].p[q]!=s[q]){ m=0; break; } if(m){ r=aml_ones(); break; } } }
    aobj_int(ret,r); return 0; }
static int aml_skip_term(aframe* f,const u8** pp,const u8* end){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; int r=aml_term(f,pp,end,&t); aobj_free(&t); return r; }
static int aml_parse_fieldlist(aframe* f,const u8* p,const u8* end,u8 kind,anode* region,anode* idx,anode* data,anode* bank,u64 bankval,u8 flags){
    u32 bit=0; u8 acc=flags&0x0F, lock=(flags>>4)&1, upd=(flags>>5)&3;
    while(p<end){
        u8 c=*p;
        if(c==0x00){ p++; if(p>=end)return -1; u8 b0=p[0]; int nb=b0>>6; if(p+1+nb>end)return -1; u32 v=nb?(u32)(b0&0x0F):(u32)(b0&0x3F); for(int k=0;k<nb;k++)v|=(u32)p[1+k]<<(4+8*k); bit+=v; p+=1+nb; continue; }
        if(c==0x01){ if(p+3>end)return -1; acc=p[1]&0x0F; p+=3; continue; }
        if(c==0x03){ if(p+4>end)return -1; acc=p[1]&0x0F; p+=4; continue; }
        if(c==0x02){ p++; if(p<end&&*p==0x11){ if(aml_skip_term(f,&p,end))return -1; } else { apath a; if(aml_parse_name(&p,end,&a))return -1; } continue; }
        if(!aml_segok(c,1))return -1;
        if(p+5>end)return -1;
        char seg[4]; for(int k=0;k<4;k++){ if(!aml_segok(p[k],k==0))return -1; seg[k]=(char)p[k]; } p+=4;
        u8 b0=p[0]; int nb=b0>>6; if(p+1+nb>end)return -1; u32 v=nb?(u32)(b0&0x0F):(u32)(b0&0x3F); for(int k=0;k<nb;k++)v|=(u32)p[1+k]<<(4+8*k); p+=1+nb;
        anode* n=anode_new(f->scope,seg,kind); if(n){ n->kind=kind; n->region=region; n->idxf=idx; n->dataf=data; n->bankf=bank; n->bankval=bankval; n->bitoff=bit; n->bitlen=v; n->acc=acc; n->lock=lock; n->upd=upd; if(f->depth>0&&f->ncreated<AML_MAXCREATED){ n->temp=1; f->created[f->ncreated++]=n; } }
        bit+=v; }
    return 0; }
static void aml_note_created(aframe* f,anode* n){ if(n&&f->depth>0&&f->ncreated<AML_MAXCREATED&&!n->temp){ n->temp=1; f->created[f->ncreated++]=n; } }
static int aml_named_body(aframe* f,anode* n,const u8* p,const u8* end){
    anode* save=f->scope; f->scope=n; int r=aml_exec_list(f,p,end); f->scope=save; return r; }
static int aml_ref_operand(aframe* f,const u8** pp,const u8* end,aobj** ptr,aobj* tmp){
    const u8* p=*pp; tmp->type=AT_NONE; tmp->p=0; tmp->e=0; tmp->own=0;
    if(p<end&&*p>=0x60&&*p<=0x67){ *ptr=&f->loc[*p-0x60]; *pp=p+1; if((*ptr)->type==AT_REF&&(*ptr)->rk==RK_SLOT&&(*ptr)->cont)*ptr=(*ptr)->cont; return 0; }
    if(p<end&&*p>=0x68&&*p<=0x6E){ aobj* a=&f->arg[*p-0x68]; *pp=p+1; if(a->type==AT_REF){ if(a->rk==RK_NODE&&a->node&&aml_resolve_alias(a->node)->kind==K_NAME){ *ptr=&aml_resolve_alias(a->node)->v; return 0; } if(a->rk==RK_SLOT&&a->cont){ *ptr=a->cont; return 0; } if(a->rk==RK_PKG&&a->cont&&a->idx<a->cont->len){ *ptr=&a->cont->e[a->idx]; return 0; } } *ptr=a; return 0; }
    if(p<end&&aml_is_name_start(*p)){ const u8* q=p; apath a; if(!aml_parse_name(&q,end,&a)){ anode* n=aml_resolve_alias(aml_lookup(f->scope,&a)); if(n&&n->kind==K_NAME){ *ptr=&n->v; *pp=q; return 0; } } }
    if(p+1<end&&*p==0x83){ const u8* q=p+1; aobj r; r.type=AT_NONE; r.p=0; r.e=0; r.own=0; if(!aml_term(f,&q,end,&r)&&r.type==AT_REF){
            if(r.rk==RK_NODE&&r.node&&aml_resolve_alias(r.node)->kind==K_NAME){ *ptr=&aml_resolve_alias(r.node)->v; *pp=q; return 0; }
            if(r.rk==RK_PKG&&r.cont&&r.idx<r.cont->len){ *ptr=&r.cont->e[r.idx]; *pp=q; return 0; }
            if(r.rk==RK_SLOT&&r.cont){ *ptr=r.cont; *pp=q; return 0; } } aobj_free(&r); }
    if(aml_term(f,pp,end,tmp))return -1; if(tmp->type==AT_REF){ aobj v; v.type=AT_NONE; v.p=0; v.e=0; v.own=0; aml_deref(tmp,&v); aobj_free(tmp); *tmp=v; } *ptr=tmp; return 0; }
static int aml_binop(aframe* f,const u8** pp,const u8* end,u8 op,aobj* out){
    aobj a,b; if(aml_arg(f,pp,end,&a))return -1; if(aml_arg(f,pp,end,&b)){ aobj_free(&a); return -1; }
    atarget t; if(aml_parse_target(f,pp,end,&t)){ aobj_free(&a); aobj_free(&b); return -1; }
    u64 x=aobj_to_int(&a), y=aobj_to_int(&b), r=0;
    switch(op){ case 0x72: r=x+y; break; case 0x74: r=x-y; break; case 0x77: r=x*y; break; case 0x79: r=y>=64?0:x<<y; break; case 0x7A: r=y>=64?0:x>>y; break;
        case 0x7B: r=x&y; break; case 0x7C: r=~(x&y); break; case 0x7D: r=x|y; break; case 0x7E: r=~(x|y); break; case 0x7F: r=x^y; break; case 0x85: r=y?x%y:0; break; }
    aobj_free(&a); aobj_free(&b); aobj_int(out,r); aml_store(f,&t,out); return 0; }
static void aml_concat(const aobj* a,const aobj* b,aobj* out){
    aobj r; r.type=AT_NONE; r.p=0; r.e=0; r.own=0;
    if(a->type==AT_INT){ aobj bb; bb.type=AT_NONE; bb.p=0; bb.e=0; bb.own=0; u32 w=g_aml_rev<2?4:8; u64 y=aobj_to_int(b); aobj_bytes(&r,AT_BUF,0,2*w); for(u32 k=0;k<w;k++){ r.p[k]=(u8)(a->i>>(8*k)); r.p[w+k]=(u8)(y>>(8*k)); } aobj_free(&bb); }
    else if(a->type==AT_STR){ aobj bs; bs.type=AT_NONE; bs.p=0; bs.e=0; bs.own=0; if(b->type==AT_STR)aobj_copy(&bs,b); else aobj_to_str(&bs,b); aobj_bytes(&r,AT_STR,0,a->len+bs.len); for(u32 k=0;k<a->len;k++)r.p[k]=a->p[k]; for(u32 k=0;k<bs.len;k++)r.p[a->len+k]=bs.p[k]; aobj_free(&bs); }
    else { aobj ab,bb; ab.type=AT_NONE; ab.p=0; ab.e=0; ab.own=0; bb.type=AT_NONE; bb.p=0; bb.e=0; bb.own=0; aobj_to_buf(&ab,a); aobj_to_buf(&bb,b); aobj_bytes(&r,AT_BUF,0,ab.len+bb.len); for(u32 k=0;k<ab.len;k++)r.p[k]=ab.p[k]; for(u32 k=0;k<bb.len;k++)r.p[ab.len+k]=bb.p[k]; aobj_free(&ab); aobj_free(&bb); }
    aobj_free(out); *out=r; }
static int aml_match_op(int op,const aobj* el,const aobj* v){ if(op==0)return 1; if(el->type!=AT_INT&&el->type!=AT_STR&&el->type!=AT_BUF)return 0; int c=aml_cmp(el,v); switch(op){ case 1: return c==0; case 2: return c<=0; case 3: return c<0; case 4: return c>=0; case 5: return c>0; } return 0; }
static int aml_term(aframe* f,const u8** pp,const u8* end,aobj* out){
    const u8* p=*pp; if(p>=end)return -1;
    if(g_aml_budget==0){ aml_set_err("instruction budget exhausted"); return -1; } g_aml_budget--;
    if(!(g_aml_budget&255)&&g_aml_deadline&&aml_now_hook&&aml_now_hook()>g_aml_deadline){ aml_set_err("time limit reached"); g_aml_budget=0; return -1; }
    if(g_aml_nest>=192){ aml_set_err("expression nesting too deep"); return -1; }
    g_aml_nest++; int rr=aml_term_in(f,pp,end,out); g_aml_nest--; if(rr)aobj_free(out); return rr; }
static int aml_term_in(aframe* f,const u8** pp,const u8* end,aobj* out){
    const u8* p=*pp;
    u8 op=*p++;
    aobj_free(out);
    switch(op){
    case 0x00: *pp=p; aobj_int(out,0); return 0;
    case 0x01: *pp=p; aobj_int(out,1); return 0;
    case 0xFF: *pp=p; aobj_int(out,aml_ones()); return 0;
    case 0x0A: if(p+1>end)return -1; aobj_int(out,p[0]); *pp=p+1; return 0;
    case 0x0B: if(p+2>end)return -1; aobj_int(out,(u64)p[0]|((u64)p[1]<<8)); *pp=p+2; return 0;
    case 0x0C: if(p+4>end)return -1; aobj_int(out,(u64)p[0]|((u64)p[1]<<8)|((u64)p[2]<<16)|((u64)p[3]<<24)); *pp=p+4; return 0;
    case 0x0E: { if(p+8>end)return -1; u64 v=0; for(int k=0;k<8;k++)v|=(u64)p[k]<<(8*k); aobj_int(out,v); *pp=p+8; return 0; }
    case 0x0D: { const u8* s=p; while(p<end&&*p)p++; if(p>=end)return -1; aobj_bytes(out,AT_STR,s,(u32)(p-s)); *pp=p+1; return 0; }
    case 0x11: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; aobj sz; if(aml_arg(f,&p,pe,&sz))return -1; u32 n=(u32)aobj_to_int(&sz); aobj_free(&sz); if(n>0x40000){ aml_set_err("buffer too large"); return -1; }
        u32 have=(u32)(pe-p); if(aobj_bytes(out,AT_BUF,0,n))return -1; for(u32 k=0;k<n&&k<have;k++)out->p[k]=p[k]; *pp=pe; return 0; }
    case 0x12: case 0x13: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; u32 n;
        if(op==0x12){ if(p>=pe)return -1; n=*p++; } else { aobj c; if(aml_arg(f,&p,pe,&c))return -1; n=(u32)aobj_to_int(&c); aobj_free(&c); }
        if(n>4096)return -1;
        out->type=AT_PKG; out->len=n; out->e=(aobj*)aml_alloc((u32)sizeof(aobj)*(n?n:1)); if(!out->e){ out->type=AT_NONE; return -1; }
        for(u32 k=0;k<n&&p<pe;k++){
            if(aml_is_name_start(*p)){ const u8* q=p; apath a; if(aml_parse_name(&q,pe,&a))return -1; anode* nd=aml_lookup(f->scope,&a);
                if(nd){ out->e[k].type=AT_REF; out->e[k].rk=RK_NODE; out->e[k].node=nd; } else { aobj_bytes(&out->e[k],AT_NAMESTR,p,(u32)(q-p)); } p=q; }
            else if(aml_term(f,&p,pe,&out->e[k]))return -1; }
        *pp=pe; return 0; }
    case 0x10: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; apath a; if(aml_parse_name(&p,pe,&a))return -1; anode* n=aml_lookup(f->scope,&a); if(!n)n=aml_create(f->scope,&a,K_SCOPE); *pp=pe; aobj_int(out,0); if(!n)return 0; return aml_named_body(f,n,p,pe); }
    case 0x08: { apath a; if(aml_parse_name(&p,end,&a))return -1; anode* n=aml_create(f->scope,&a,K_NAME); aobj v; v.type=AT_NONE; v.p=0; v.e=0; v.own=0; if(aml_term(f,&p,end,&v))return -1; if(n){ n->kind=K_NAME; aobj_free(&n->v); n->v=v; aml_note_created(f,n); } else aobj_free(&v); *pp=p; aobj_int(out,0); return 0; }
    case 0x14: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; apath a; if(aml_parse_name(&p,pe,&a))return -1; if(p>=pe)return -1; u8 fl=*p++;
        anode* n=aml_create(f->scope,&a,K_METHOD); if(n){ n->kind=K_METHOD; n->code=p; n->clen=(u32)(pe-p); n->argc=fl&7; n->mflags=fl; aml_note_created(f,n); } *pp=pe; aobj_int(out,0); return 0; }
    case 0x06: { apath a,b; if(aml_parse_name(&p,end,&a)||aml_parse_name(&p,end,&b))return -1; anode* src=aml_lookup(f->scope,&a); anode* n=aml_create(f->scope,&b,K_ALIAS); if(n){ n->kind=K_ALIAS; n->target=src; aml_note_created(f,n); } *pp=p; aobj_int(out,0); return 0; }
    case 0x15: { apath a; if(aml_parse_name(&p,end,&a))return -1; if(p+2>end)return -1; *pp=p+2; aobj_int(out,0); return 0; }
    case 0x5C: case 0x5E: case 0x2E: case 0x2F: default:
        if(op>=0x60&&op<=0x67){ aobj* l=&f->loc[op-0x60]; *pp=p; if(l->type==AT_REF&&l->rk==RK_SLOT)return aml_deref(l,out); return aobj_copy(out,l); }
        if(op>=0x68&&op<=0x6E){ aobj* a=&f->arg[op-0x68]; *pp=p; if(a->type==AT_REF&&a->rk!=RK_NODE)return aml_deref(a,out); return aobj_copy(out,a); }
        if(aml_is_name_start(op)){ p--; apath a; if(aml_parse_name(&p,end,&a))return -1; anode* n=aml_resolve_alias(aml_lookup(f->scope,&a));
            if(!n){ *pp=p; aml_set_err("unknown name"); aobj_int(out,0); return 0; }
            if(n->kind==K_METHOD||n->kind==K_NATIVE){ aobj args[7]; int na=n->argc; for(int k=0;k<7;k++){ args[k].type=AT_NONE; args[k].p=0; args[k].e=0; args[k].own=0; }
                for(int k=0;k<na;k++) if(aml_arg(f,&p,end,&args[k])){ for(int z=0;z<7;z++)aobj_free(&args[z]); return -1; }
                *pp=p; int r=aml_call(n,args,na,out,f->depth); for(int z=0;z<7;z++)aobj_free(&args[z]); return r; }
            *pp=p; return aml_node_value(n,out); }
        break;
    case 0x70: { aobj v; if(aml_arg(f,&p,end,&v))return -1; atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&v); return -1; } if(v.type==AT_REF&&v.rk!=RK_SLOT&&t.kind!=TG_SLOT){ aobj d; d.type=AT_NONE; d.p=0; d.e=0; d.own=0; aml_deref(&v,&d); aobj_free(&v); v=d; } aml_store(f,&t,&v); *out=v; *pp=p; return 0; }
    case 0x9D: { aobj v; if(aml_arg(f,&p,end,&v))return -1; atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&v); return -1; }
        if(t.kind==TG_NODE&&t.node&&t.node->kind==K_NAME){ aobj_free(&t.node->v); aobj_copy(&t.node->v,&v); } else aml_store(f,&t,&v); *out=v; *pp=p; return 0; }
    case 0x71: { atarget t; if(aml_parse_target(f,&p,end,&t))return -1; *pp=p;
        if(t.kind==TG_NODE){ out->type=AT_REF; out->rk=RK_NODE; out->node=t.node; return 0; }
        if(t.kind==TG_SLOT){ out->type=AT_REF; out->rk=RK_SLOT; out->cont=t.slot; return 0; }
        if(t.kind==TG_REF){ *out=t.ref; out->own=0; return 0; } aobj_int(out,0); return 0; }
    case 0x83: { aobj r; if(aml_arg(f,&p,end,&r))return -1; *pp=p;
        if(r.type==AT_STR){ const u8* q=r.p; apath a; if(!aml_parse_name(&q,r.p+r.len,&a)){ anode* n=aml_lookup(f->scope,&a); aobj_free(&r); if(n)return aml_node_value(n,out); } else aobj_free(&r); aobj_int(out,0); return 0; }
        int rc=aml_deref(&r,out); aobj_free(&r); return rc; }
    case 0x72: case 0x74: case 0x77: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: case 0x85: { int r=aml_binop(f,&p,end,op,out); *pp=p; return r; }
    case 0x73: case 0x84: { aobj a,b; if(aml_arg(f,&p,end,&a))return -1; if(aml_arg(f,&p,end,&b)){ aobj_free(&a); return -1; } atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&a); aobj_free(&b); return -1; }
        if(op==0x84){ aobj ab,bb; ab.type=AT_NONE; ab.p=0; ab.e=0; ab.own=0; bb.type=AT_NONE; bb.p=0; bb.e=0; bb.own=0; aobj_to_buf(&ab,&a); aobj_to_buf(&bb,&b); u32 la=ab.len>=2?ab.len-2:ab.len, lb=bb.len>=2?bb.len-2:bb.len;
            aobj_bytes(out,AT_BUF,0,la+lb+2); for(u32 k=0;k<la;k++)out->p[k]=ab.p[k]; for(u32 k=0;k<lb;k++)out->p[la+k]=bb.p[k]; out->p[la+lb]=0x79; u8 s=0; for(u32 k=0;k<la+lb+1;k++)s=(u8)(s+out->p[k]); out->p[la+lb+1]=(u8)(0-s); aobj_free(&ab); aobj_free(&bb); }
        else aml_concat(&a,&b,out);
        aobj_free(&a); aobj_free(&b); aml_store(f,&t,out); *pp=p; return 0; }
    case 0x75: case 0x76: { atarget t; if(aml_parse_target(f,&p,end,&t))return -1; aobj v; v.type=AT_NONE; v.p=0; v.e=0; v.own=0; aml_get_target_value(f,&t,&v); u64 x=aobj_to_int(&v); aobj_free(&v); aobj_int(out,op==0x75?x+1:x-1); aml_store(f,&t,out); *pp=p; return 0; }
    case 0x78: { aobj a,b; if(aml_arg(f,&p,end,&a))return -1; if(aml_arg(f,&p,end,&b)){ aobj_free(&a); return -1; } atarget tr,tq; if(aml_parse_target(f,&p,end,&tr)||aml_parse_target(f,&p,end,&tq)){ aobj_free(&a); aobj_free(&b); return -1; }
        u64 x=aobj_to_int(&a), y=aobj_to_int(&b); aobj_free(&a); aobj_free(&b); aobj rem; rem.type=AT_NONE; rem.p=0; rem.e=0; rem.own=0; aobj_int(&rem,y?x%y:0); aobj_int(out,y?x/y:0); aml_store(f,&tr,&rem); aml_store(f,&tq,out); *pp=p; return 0; }
    case 0x80: case 0x81: case 0x82: { aobj a; if(aml_arg(f,&p,end,&a))return -1; atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&a); return -1; } u64 x=aobj_to_int(&a), r=0; aobj_free(&a);
        if(op==0x80)r=~x; else if(op==0x81){ for(int k=63;k>=0;k--) if((x>>k)&1){ r=(u64)k+1; break; } } else { for(int k=0;k<64;k++) if((x>>k)&1){ r=(u64)k+1; break; } }
        aobj_int(out,r); aml_store(f,&t,out); *pp=p; return 0; }
    case 0x86: { atarget t; if(aml_parse_target(f,&p,end,&t))return -1; aobj v; if(aml_arg(f,&p,end,&v))return -1; anode* n=t.kind==TG_NODE?t.node:(t.kind==TG_REF&&t.ref.rk==RK_NODE?t.ref.node:0);
        if(n){ int nt=(g_anq_t+1)&31; if(nt!=g_anq_h){ g_anq[g_anq_t].node=n; g_anq[g_anq_t].val=(u32)aobj_to_int(&v); g_anq_t=nt; } } aobj_free(&v); aobj_int(out,0); *pp=p; return 0; }
    case 0x87: { aobj* o; aobj tmp; if(aml_ref_operand(f,&p,end,&o,&tmp))return -1; u64 n=0; if(o->type==AT_STR||o->type==AT_BUF)n=o->len; else if(o->type==AT_PKG)n=o->len; aobj_free(&tmp); aobj_int(out,n); *pp=p; return 0; }
    case 0x88: { aobj* o; aobj tmp; if(aml_ref_operand(f,&p,end,&o,&tmp))return -1; aobj ix; if(aml_arg(f,&p,end,&ix)){ aobj_free(&tmp); return -1; } atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&tmp); aobj_free(&ix); return -1; }
        u32 i=(u32)aobj_to_int(&ix); aobj_free(&ix);
        int own=0; aobj* keep=o; if(o==&tmp){ keep=(aobj*)aml_alloc(sizeof(aobj)); if(!keep){ aobj_free(&tmp); return -1; } *keep=tmp; tmp.type=AT_NONE; tmp.p=0; tmp.e=0; tmp.own=0; own=1; }
        if(keep->type!=AT_PKG&&keep->type!=AT_BUF&&keep->type!=AT_STR){ aml_set_err("Index of non-container"); }
        out->type=AT_REF; out->cont=keep; out->idx=i; out->rk=(keep->type==AT_PKG)?RK_PKG:RK_BUF; out->own=(u8)own;
        aml_store(f,&t,out); if(t.kind==TG_REF)aobj_free(&t.ref); *pp=p; return 0; }
    case 0x89: { aobj* pk; aobj tmp; if(aml_ref_operand(f,&p,end,&pk,&tmp))return -1; if(p+1>end){ aobj_free(&tmp); return -1; } int o1=*p++; aobj v1; if(aml_arg(f,&p,end,&v1)){ aobj_free(&tmp); return -1; } if(p+1>end){ aobj_free(&tmp); aobj_free(&v1); return -1; } int o2=*p++;
        aobj v2; if(aml_arg(f,&p,end,&v2)){ aobj_free(&tmp); aobj_free(&v1); return -1; } aobj st; if(aml_arg(f,&p,end,&st)){ aobj_free(&tmp); aobj_free(&v1); aobj_free(&v2); return -1; }
        u64 r=aml_ones(); if(pk->type==AT_PKG) for(u32 k=(u32)aobj_to_int(&st);k<pk->len;k++){ if(aml_match_op(o1,&pk->e[k],&v1)&&aml_match_op(o2,&pk->e[k],&v2)){ r=k; break; } }
        aobj_free(&tmp); aobj_free(&v1); aobj_free(&v2); aobj_free(&st); aobj_int(out,r); *pp=p; return 0; }
    case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8F: {
        aobj* src; aobj tmp; const u8* sp=p; int isname=p<end&&aml_is_name_start(*p); anode* srcnode=0;
        if(isname){ const u8* q=p; apath a; if(!aml_parse_name(&q,end,&a)){ anode* nn=aml_resolve_alias(aml_lookup(f->scope,&a)); if(nn&&nn->kind==K_NAME)srcnode=nn; } }
        (void)sp; if(aml_ref_operand(f,&p,end,&src,&tmp))return -1;
        aobj ix; if(aml_arg(f,&p,end,&ix)){ aobj_free(&tmp); return -1; } apath a; if(aml_parse_name(&p,end,&a)){ aobj_free(&tmp); aobj_free(&ix); return -1; }
        u32 idx=(u32)aobj_to_int(&ix); aobj_free(&ix);
        u32 bo, bl; switch(op){ case 0x8A: bo=idx*8; bl=32; break; case 0x8B: bo=idx*8; bl=16; break; case 0x8C: bo=idx*8; bl=8; break; case 0x8D: bo=idx; bl=1; break; default: bo=idx*8; bl=64; }
        anode* n=aml_create(f->scope,&a,K_BUFFIELD); if(n){ n->kind=K_BUFFIELD; n->bitoff=bo; n->bitlen=bl; n->bnode=srcnode;
            if(!srcnode){ aobj* keep=src; if(src==&tmp){ keep=(aobj*)aml_alloc(sizeof(aobj)); if(keep){ *keep=tmp; tmp.type=AT_NONE; tmp.p=0; tmp.e=0; tmp.own=0; } } n->bsrc=keep; } aml_note_created(f,n); }
        aobj_free(&tmp); aobj_int(out,0); *pp=p; return 0; }
    case 0x8E: { atarget t; t.kind=TG_NONE; t.slot=0; t.node=0; t.ref.type=AT_NONE; t.ref.own=0;
        if(p<end&&!(aml_is_name_start(*p)||(*p>=0x60&&*p<=0x6E)||*p==0x5B)){ aobj v; if(aml_arg(f,&p,end,&v))return -1; if(v.type==AT_REF){ t.kind=TG_REF; t.ref=v; } else { u64 ty0=v.type==AT_INT?1:v.type==AT_STR?2:v.type==AT_BUF?3:v.type==AT_PKG?4:0; aobj_free(&v); aobj_int(out,ty0); *pp=p; return 0; } }
        else if(aml_parse_target(f,&p,end,&t))return -1; u64 ty=0; anode* n=t.kind==TG_NODE?t.node:0; aobj* s=t.kind==TG_SLOT?t.slot:0;
        if(n){ switch(n->kind){ case K_NAME: ty=n->v.type==AT_INT?1:n->v.type==AT_STR?2:n->v.type==AT_BUF?3:n->v.type==AT_PKG?4:0; break; case K_FIELD: case K_INDEXFIELD: case K_BANKFIELD: ty=5; break; case K_DEVICE: ty=6; break; case K_EVENT: ty=7; break; case K_METHOD: case K_NATIVE: ty=8; break; case K_MUTEX: ty=9; break; case K_REGION: ty=10; break; case K_POWER: ty=11; break; case K_PROCESSOR: ty=12; break; case K_THERMAL: ty=13; break; case K_BUFFIELD: ty=14; break; default: ty=0; } }
        else { aobj tv; tv.type=AT_NONE; tv.p=0; tv.e=0; tv.own=0; const aobj* s2=s;
            if(t.kind==TG_REF){ aml_deref(&t.ref,&tv); aobj_free(&t.ref); s2=&tv; } else if(s&&s->type==AT_REF){ aml_deref(s,&tv); s2=&tv; }
            if(s2)ty=s2->type==AT_INT?1:s2->type==AT_STR?2:s2->type==AT_BUF?3:s2->type==AT_PKG?4:0; aobj_free(&tv); }
        aobj_int(out,ty); *pp=p; return 0; }
    case 0x90: case 0x91: { aobj a,b; if(aml_arg(f,&p,end,&a))return -1; if(aml_arg(f,&p,end,&b)){ aobj_free(&a); return -1; } u64 x=aobj_to_int(&a), y=aobj_to_int(&b); aobj_free(&a); aobj_free(&b); aobj_int(out,(op==0x90?(x&&y):(x||y))?aml_ones():0); *pp=p; return 0; }
    case 0x92: { aobj a; if(aml_arg(f,&p,end,&a))return -1; u64 x=aobj_to_int(&a); aobj_free(&a); aobj_int(out,x?0:aml_ones()); *pp=p; return 0; }
    case 0x93: case 0x94: case 0x95: { aobj a,b; if(aml_arg(f,&p,end,&a))return -1; if(aml_arg(f,&p,end,&b)){ aobj_free(&a); return -1; } int c=aml_cmp(&a,&b); aobj_free(&a); aobj_free(&b);
        int r=op==0x93?c==0:op==0x94?c>0:c<0; aobj_int(out,r?aml_ones():0); *pp=p; return 0; }
    case 0x96: case 0x97: case 0x98: case 0x99: { aobj a; if(aml_arg(f,&p,end,&a))return -1; atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&a); return -1; }
        if(op==0x96)aobj_to_buf(out,&a);
        else if(op==0x99){ if(a.type==AT_STR){ u64 v=0; const u8* s=a.p; u32 k=0, n=a.len; int hex=0; if(n>=2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')){ hex=1; k=2; }
                for(;k<n;k++){ u8 c=s[k]; int d=-1; if(c>='0'&&c<='9')d=c-'0'; else if(hex&&c>='a'&&c<='f')d=c-'a'+10; else if(hex&&c>='A'&&c<='F')d=c-'A'+10; if(d<0)break; v=hex?(v<<4)|(u64)d:v*10+(u64)d; } aobj_int(out,v); }
            else aobj_int(out,aobj_to_int(&a)); }
        else if(op==0x98){ if(a.type==AT_STR)aobj_copy(out,&a); else if(a.type==AT_INT)aml_hexstr(out,a.i); else aobj_to_str_sep(out,&a,','); }
        else { if(a.type==AT_STR)aobj_copy(out,&a); else if(a.type==AT_INT)aml_decstr(out,a.i);
            else if(a.type!=AT_BUF||!a.p)aobj_bytes(out,AT_STR,0,0);
            else { char b[512]; u32 o=0; for(u32 k=0;k<a.len&&o<480;k++){ if(k)b[o++]=','; char t2[4]; int n=0; u32 v=a.p[k]; do{ t2[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)b[o++]=t2[--n]; } aobj_bytes(out,AT_STR,(const u8*)b,o); } }
        aobj_free(&a); aml_store(f,&t,out); *pp=p; return 0; }
    case 0x9C: { aobj a,l; if(aml_arg(f,&p,end,&a))return -1; if(aml_arg(f,&p,end,&l)){ aobj_free(&a); return -1; } atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&a); aobj_free(&l); return -1; }
        aobj b; b.type=AT_NONE; b.p=0; b.e=0; b.own=0; aobj_to_buf(&b,&a); u64 mx=aobj_to_int(&l); u32 n=0; while(n<b.len&&b.p[n]&&(u64)n<mx)n++; aobj_bytes(out,AT_STR,b.p,n); aobj_free(&b); aobj_free(&a); aobj_free(&l); aml_store(f,&t,out); *pp=p; return 0; }
    case 0x9E: { aobj s,i,l; if(aml_arg(f,&p,end,&s))return -1; if(aml_arg(f,&p,end,&i)){ aobj_free(&s); return -1; } if(aml_arg(f,&p,end,&l)){ aobj_free(&s); aobj_free(&i); return -1; } atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&s); aobj_free(&i); aobj_free(&l); return -1; }
        u64 st=aobj_to_int(&i), ln=aobj_to_int(&l); u8 ty=s.type==AT_STR?AT_STR:AT_BUF; aobj src; src.type=AT_NONE; src.p=0; src.e=0; src.own=0; if(s.type==AT_STR||s.type==AT_BUF)aobj_copy(&src,&s); else aobj_to_buf(&src,&s);
        u32 n=0; if(st<src.len){ n=(u32)(src.len-st); if(ln<n)n=(u32)ln; } aobj_bytes(out,ty,n?src.p+st:0,n); aobj_free(&src); aobj_free(&s); aobj_free(&i); aobj_free(&l); aml_store(f,&t,out); *pp=p; return 0; }
    case 0x9F: f->flow=3; *pp=p; aobj_int(out,0); return 0;
    case 0xA5: f->flow=2; *pp=p; aobj_int(out,0); return 0;
    case 0xA3: case 0xCC: *pp=p; aobj_int(out,0); return 0;
    case 0xA4: { aobj v; if(aml_arg(f,&p,end,&v))return -1; if(v.type==AT_REF&&v.rk!=RK_NODE){ aobj d; d.type=AT_NONE; d.p=0; d.e=0; d.own=0; aml_deref(&v,&d); aobj_free(&v); v=d; } aobj_free(&f->ret); f->ret=v; f->flow=1; *pp=p; aobj_int(out,0); return 0; }
    case 0xA0: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; aobj c; if(aml_arg(f,&p,pe,&c))return -1; u64 cv=aobj_to_int(&c); aobj_free(&c);
        const u8* after=pe; const u8* ep=0; const u8* ee=0; if(after<end&&*after==0xA1){ const u8* q=after+1; if(!amli_pkglen(&q,end,&ee)){ ep=q; after=ee; } }
        *pp=after; aobj_int(out,0);
        if(cv)return aml_exec_list(f,p,pe); if(ep)return aml_exec_list(f,ep,ee); return 0; }
    case 0xA1: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; *pp=pe; aobj_int(out,0); return 0; }
    case 0xA2: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; const u8* body0=p; *pp=pe; aobj_int(out,0);
        for(int it=0;it<1000000;it++){ const u8* q=body0; aobj c; if(aml_arg(f,&q,pe,&c))return -1; u64 cv=aobj_to_int(&c); aobj_free(&c); if(!cv)break;
            if(aml_exec_list(f,q,pe))return -1; if(f->flow==1)return 0; if(f->flow==2){ f->flow=0; break; } if(f->flow==3)f->flow=0; if(!g_aml_budget)return -1; }
        return 0; }
    case 0x5B: { if(p>=end)return -1; u8 x=*p++;
        switch(x){
        case 0x01: { apath a; if(aml_parse_name(&p,end,&a))return -1; if(p>=end)return -1; p++; anode* n=aml_create(f->scope,&a,K_MUTEX); if(n){ n->kind=K_MUTEX; aml_note_created(f,n); } *pp=p; aobj_int(out,0); return 0; }
        case 0x02: { apath a; if(aml_parse_name(&p,end,&a))return -1; anode* n=aml_create(f->scope,&a,K_EVENT); if(n){ n->kind=K_EVENT; aml_note_created(f,n); } *pp=p; aobj_int(out,0); return 0; }
        case 0x12: { const u8* q=p; atarget t; int ok=0; if(q<end&&aml_is_name_start(*q)){ apath a; if(aml_parse_name(&q,end,&a))return -1; anode* n=aml_lookup(f->scope,&a); ok=n!=0; t.kind=n?TG_NODE:TG_NONE; t.node=n; }
            else { if(aml_parse_target(f,&q,end,&t))return -1; ok=t.kind!=TG_NONE; }
            atarget dst; if(aml_parse_target(f,&q,end,&dst))return -1;
            if(ok){ aobj r; r.type=AT_NONE; r.p=0; r.e=0; r.own=0; if(t.kind==TG_NODE){ r.type=AT_REF; r.rk=RK_NODE; r.node=t.node; } else if(t.kind==TG_SLOT){ r.type=AT_REF; r.rk=RK_SLOT; r.cont=t.slot; } else r=t.ref; if(dst.kind==TG_SLOT)aobj_copy(dst.slot,&r); else aml_store(f,&dst,&r); }
            aobj_int(out,ok?aml_ones():0); *pp=q; return 0; }
        case 0x13: { aobj* src; aobj tmp; anode* srcnode=0; if(p<end&&aml_is_name_start(*p)){ const u8* q=p; apath a; if(!aml_parse_name(&q,end,&a)){ anode* nn=aml_resolve_alias(aml_lookup(f->scope,&a)); if(nn&&nn->kind==K_NAME)srcnode=nn; } }
            if(aml_ref_operand(f,&p,end,&src,&tmp))return -1; aobj bi,nb; if(aml_arg(f,&p,end,&bi)){ aobj_free(&tmp); return -1; } if(aml_arg(f,&p,end,&nb)){ aobj_free(&tmp); aobj_free(&bi); return -1; } apath a; if(aml_parse_name(&p,end,&a)){ aobj_free(&tmp); aobj_free(&bi); aobj_free(&nb); return -1; }
            anode* n=aml_create(f->scope,&a,K_BUFFIELD); if(n){ n->kind=K_BUFFIELD; n->cfld=1; n->bitoff=(u32)aobj_to_int(&bi); n->bitlen=(u32)aobj_to_int(&nb); n->bnode=srcnode; if(!srcnode){ aobj* keep=src; if(src==&tmp){ keep=(aobj*)aml_alloc(sizeof(aobj)); if(keep){ *keep=tmp; tmp.type=AT_NONE; tmp.p=0; tmp.e=0; tmp.own=0; } } n->bsrc=keep; } aml_note_created(f,n); }
            aobj_free(&tmp); aobj_free(&bi); aobj_free(&nb); aobj_int(out,0); *pp=p; return 0; }
        case 0x1F: case 0x20: { if(x==0x20){ apath a; if(aml_parse_name(&p,end,&a))return -1; atarget t; if(aml_parse_target(f,&p,end,&t))return -1; } else { for(int k=0;k<6;k++) if(aml_skip_term(f,&p,end))return -1; } aobj_int(out,0); *pp=p; return 0; }
        case 0x21: case 0x22: { aobj t; if(aml_arg(f,&p,end,&t))return -1; u64 v=aobj_to_int(&t); aobj_free(&t); if(x==0x22){ if(v>g_aml_sleep_left)v=g_aml_sleep_left; g_aml_sleep_left-=(u32)v; if(aml_sleep_hook&&v)aml_sleep_hook((u32)v); } else { if(v>100)v=100; g_aml_stall_us+=(u32)v; if(g_aml_stall_us>=1000){ u32 ms=g_aml_stall_us/1000; g_aml_stall_us%=1000; if(ms>g_aml_sleep_left){ g_aml_sleep_left=0; } else g_aml_sleep_left-=ms; }
                if(g_aml_sleep_left){ if(aml_stall_hook)aml_stall_hook((u32)v); else for(u64 k=0;k<v;k++)io_wait(); } } *pp=p; aobj_int(out,0); return 0; }
        case 0x23: { atarget t; if(aml_parse_target(f,&p,end,&t))return -1; if(p+2>end)return -1; p+=2; *pp=p; int r=0; if(t.kind==TG_NODE&&t.node&&t.node==g_aml_glnode)r=aml_gl(1); aobj_int(out,r?aml_ones():0); return 0; }
        case 0x24: case 0x26: case 0x27: { atarget t; if(aml_parse_target(f,&p,end,&t))return -1; *pp=p; if(x==0x27&&t.kind==TG_NODE&&t.node&&t.node==g_aml_glnode)aml_gl(0); aobj_int(out,0); return 0; }
        case 0x25: { atarget t; if(aml_parse_target(f,&p,end,&t))return -1; aobj to; if(aml_arg(f,&p,end,&to))return -1; aobj_free(&to); *pp=p; aobj_int(out,0); return 0; }
        case 0x28: case 0x29: { aobj a; if(aml_arg(f,&p,end,&a))return -1; atarget t; if(aml_parse_target(f,&p,end,&t)){ aobj_free(&a); return -1; } u64 v=aobj_to_int(&a), r=0; aobj_free(&a);
            if(x==0x28){ u64 m=1; while(v){ r+=(v&15)*m; m*=10; v>>=4; } } else { int sh=0; do{ r|=(v%10)<<sh; sh+=4; v/=10; }while(v&&sh<64); }
            aobj_int(out,r); aml_store(f,&t,out); *pp=p; return 0; }
        case 0x30: aobj_int(out,0x20220331); *pp=p; return 0;
        case 0x31: aobj_int(out,0); *pp=p; return 0;
        case 0x32: { if(p+5>end)return -1; p+=5; aobj a; if(aml_arg(f,&p,end,&a))return -1; aobj_free(&a); aml_set_err("Fatal"); f->flow=1; *pp=p; aobj_int(out,0); return 0; }
        case 0x33: aobj_int(out,aml_timer_hook?aml_timer_hook():0); *pp=p; return 0;
        case 0x80: { apath a; if(aml_parse_name(&p,end,&a))return -1; if(p>=end)return -1; u8 sp=*p++; aobj o,l; if(aml_arg(f,&p,end,&o))return -1; if(aml_arg(f,&p,end,&l)){ aobj_free(&o); return -1; }
            anode* n=aml_create(f->scope,&a,K_REGION); if(n){ n->kind=K_REGION; n->space=sp; n->base=aobj_to_int(&o); n->rlen=aobj_to_int(&l); aml_note_created(f,n); } aobj_free(&o); aobj_free(&l); *pp=p; aobj_int(out,0); return 0; }
        case 0x81: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; apath a; if(aml_parse_name(&p,pe,&a))return -1; if(p>=pe)return -1; u8 fl=*p++; anode* rg=aml_resolve_alias(aml_lookup(f->scope,&a));
            aml_parse_fieldlist(f,p,pe,K_FIELD,rg,0,0,0,0,fl); *pp=pe; aobj_int(out,0); return 0; }
        case 0x86: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; apath a,b; if(aml_parse_name(&p,pe,&a)||aml_parse_name(&p,pe,&b))return -1; if(p>=pe)return -1; u8 fl=*p++;
            anode* ix=aml_resolve_alias(aml_lookup(f->scope,&a)); anode* dt=aml_resolve_alias(aml_lookup(f->scope,&b)); aml_parse_fieldlist(f,p,pe,K_INDEXFIELD,0,ix,dt,0,0,fl); *pp=pe; aobj_int(out,0); return 0; }
        case 0x87: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; apath a,b; if(aml_parse_name(&p,pe,&a)||aml_parse_name(&p,pe,&b))return -1; aobj bv; if(aml_arg(f,&p,pe,&bv))return -1; if(p>=pe){ aobj_free(&bv); return -1; } u8 fl=*p++;
            anode* rg=aml_resolve_alias(aml_lookup(f->scope,&a)); anode* bk=aml_resolve_alias(aml_lookup(f->scope,&b)); aml_parse_fieldlist(f,p,pe,K_BANKFIELD,rg,0,0,bk,aobj_to_int(&bv),fl); aobj_free(&bv); *pp=pe; aobj_int(out,0); return 0; }
        case 0x88: { apath a; if(aml_parse_name(&p,end,&a))return -1; for(int k=0;k<3;k++) if(aml_skip_term(f,&p,end))return -1; anode* n=aml_create(f->scope,&a,K_REGION); if(n){ n->kind=K_REGION; n->space=0x80; } *pp=p; aobj_int(out,0); return 0; }
        case 0x82: case 0x83: case 0x84: case 0x85: { const u8* pe; if(amli_pkglen(&p,end,&pe))return -1; apath a; if(aml_parse_name(&p,pe,&a))return -1;
            u8 k=x==0x82?K_DEVICE:x==0x83?K_PROCESSOR:x==0x84?K_POWER:K_THERMAL;
            if(x==0x83){ if(p+6>pe)return -1; p+=6; } else if(x==0x84){ if(p+3>pe)return -1; p+=3; }
            anode* n=aml_create(f->scope,&a,k); if(n){ n->kind=k; aml_note_created(f,n); } *pp=pe; aobj_int(out,0); if(!n)return 0; return aml_named_body(f,n,p,pe); }
        }
        aml_set_err("unknown extended opcode"); return -1; }
    }
    aml_set_err("unknown opcode"); return -1; }
static const u8* aml_skip_pkg(const u8* p,const u8* end){
    if(p>=end)return 0; u8 op=*p++; const u8* pe;
    if(op==0x10||op==0x14||op==0xA0||op==0xA1||op==0xA2||op==0x11||op==0x12||op==0x13){ if(amli_pkglen(&p,end,&pe))return 0; return pe; }
    if(op==0x5B&&p<end){ u8 x=*p++; if(x==0x81||x==0x82||x==0x83||x==0x84||x==0x85||x==0x86||x==0x87){ if(amli_pkglen(&p,end,&pe))return 0; return pe; } }
    return 0; }
static int aml_exec_list(aframe* f,const u8* p,const u8* end){
    int rc=0;
    while(p<end){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; const u8* before=p; int r=aml_term(f,&p,end,&t); aobj_free(&t);
        if(r){ if(f->depth>0||!g_aml_budget)return -1; const u8* nx=aml_skip_pkg(before,end); if(!nx||nx<=before)return -1; p=nx; rc=-1; continue; }
        if(f->flow)return 0; }
    return rc; }
static int aml_call(anode* m,aobj* args,int nargs,aobj* ret,int depth){
    aobj_free(ret);
    if(!m)return -1; m=aml_resolve_alias(m);
    if(m->kind==K_NATIVE){ aobj_int(ret,0); return m->native?m->native(args,nargs,ret):0; }
    if(m->kind!=K_METHOD){ return aml_node_value(m,ret); }
    if(depth>=32){ aml_set_err("call depth"); aobj_int(ret,0); return -1; }
    aframe* f=(aframe*)aml_alloc(sizeof(aframe)); if(!f)return -1;
    f->scope=m; f->depth=depth+1; f->nargs=nargs;
    for(int k=0;k<nargs&&k<7;k++)aobj_copy(&f->arg[k],&args[k]);
    int r=aml_exec_list(f,m->code,m->code+m->clen);
    if(f->ret.type!=AT_NONE){ *ret=f->ret; f->ret.type=AT_NONE; f->ret.p=0; f->ret.e=0; } else aobj_int(ret,0);
    for(int k=0;k<8;k++)aobj_free(&f->loc[k]); for(int k=0;k<7;k++)aobj_free(&f->arg[k]);
    for(int k=f->ncreated-1;k>=0;k--){ anode* n=f->created[k]; if(n->temp&&!n->child)anode_unlink(n); }
    kfree(f); return r; }
anode* aml_find(const char* path){ if(!g_aroot)return 0; const u8* p=(const u8*)path; u32 n=0; while(path[n])n++;
    u8 enc[80]; u32 o=0; u32 i=0; if(p[0]=='\\'){ enc[o++]='\\'; i=1; }
    u8 segs[16][4]; int ns=0; while(i<n&&ns<16){ int k=0; while(i<n&&p[i]!='.'&&k<4)segs[ns][k++]=p[i++]; while(k<4)segs[ns][k++]='_'; ns++; while(i<n&&p[i]!='.')i++; if(i<n&&p[i]=='.')i++; }
    if(!ns)return g_aroot; if(ns==2)enc[o++]=0x2E; else if(ns>2){ enc[o++]=0x2F; enc[o++]=(u8)ns; } for(int s=0;s<ns;s++)for(int k=0;k<4;k++)enc[o++]=segs[s][k];
    const u8* q=enc; apath a; if(aml_parse_name(&q,enc+o,&a))return 0; return aml_resolve_alias(aml_lookup(g_aroot,&a)); }
anode* aml_child(anode* n,const char* seg4){ char s[4]; int k=0; for(;k<4&&seg4[k];k++)s[k]=seg4[k]; for(;k<4;k++)s[k]='_'; return aml_resolve_alias(anode_child(n,s)); }
static int g_aml_inside=0;
int aml_eval(anode* n,aobj* args,int nargs,aobj* out){ out->type=AT_NONE; out->p=0; out->e=0; out->own=0; out->len=0; if(!n)return -1;
    if(g_aml_inside)return aml_call(n,args,nargs,out,0);
    if(g_aml_hard_deadline&&aml_now_hook&&aml_now_hook()>g_aml_hard_deadline){ aml_set_err("ACPI time limit reached"); return -1; }
    int batch=g_aml_hard_deadline!=0; u32 tms=batch?g_aml_time_ms:1000;
    g_aml_budget=2000000; g_aml_sleep_left=batch?g_aml_sleep_pool:1000; g_aml_nest=0; g_aml_inside=1; g_aml_deadline=aml_now_hook?aml_now_hook()+(u64)tms*1000000ULL:0; if(batch&&g_aml_deadline>g_aml_hard_deadline)g_aml_deadline=g_aml_hard_deadline;
    int r=aml_call(n,args,nargs,out,0); g_aml_inside=0; g_aml_deadline=0; if(batch)g_aml_sleep_pool=g_aml_sleep_left; while(g_aml_gl_depth>0)aml_gl(0); return r; }
int aml_eval_int(anode* n,u64* v){ aobj o; if(aml_eval(n,0,0,&o)){ aobj_free(&o); return -1; } if(o.type==AT_REF){ aobj d; d.type=AT_NONE; d.p=0; d.e=0; d.own=0; aml_deref(&o,&d); aobj_free(&o); o=d; } *v=aobj_to_int(&o); int ok=o.type==AT_INT||o.type==AT_BUF||o.type==AT_STR; aobj_free(&o); return ok?0:-1; }
static anode* aml_predef(const char* seg,u8 kind){ char s[4]; int k=0; for(;k<4&&seg[k];k++)s[k]=seg[k]; for(;k<4;k++)s[k]='_'; return anode_new(g_aroot,s,kind); }
static void aml_reset(void);
int aml_init_ns(void){
    if(g_aroot)return 0;
    g_aroot=(anode*)aml_alloc(sizeof(anode)); if(!g_aroot)return -1; g_aroot->nm[0]='\\'; g_aroot->nm[1]=g_aroot->nm[2]=g_aroot->nm[3]='_'; g_aroot->kind=K_SCOPE;
    int ok=aml_predef("_SB",K_DEVICE)&&aml_predef("_GPE",K_SCOPE)&&aml_predef("_PR",K_SCOPE)&&aml_predef("_TZ",K_SCOPE)&&aml_predef("_SI",K_SCOPE);
    anode* o=aml_predef("_OSI",K_NATIVE); anode* os=aml_predef("_OS",K_NAME); anode* rv=aml_predef("_REV",K_NAME); g_aml_glnode=aml_predef("_GL",K_MUTEX); if(!g_aml_glnode)ok=0;
    if(!ok||!o||!os||!rv||aobj_bytes(&os->v,AT_STR,(const u8*)"Microsoft Windows NT",20)){ aml_reset(); return -1; }
    o->native=aml_osi; o->argc=1; aobj_int(&rv->v,2);
    return 0; }
int aml_load_table(const u8* aml,u32 len){
    if(aml_init_ns())return -1;
    aframe* f=(aframe*)aml_alloc(sizeof(aframe)); if(!f)return -1; f->scope=g_aroot; f->depth=0;
    g_aml_budget=4000000; g_aml_sleep_left=g_aml_sleep_pool; g_aml_nest=0; g_aml_inside=1; g_aml_deadline=aml_now_hook?aml_now_hook()+(u64)g_aml_time_ms*1000000ULL:0;
    const u8* p=aml; const u8* end=aml+len; int bad=0;
    while(p<end){ aobj t; t.type=AT_NONE; t.p=0; t.e=0; t.own=0; const u8* before=p; int r=aml_term(f,&p,end,&t); aobj_free(&t);
        if(r){ bad++; const u8* nx=g_aml_budget?aml_skip_pkg(before,end):0; if(!nx||nx<=before)break; p=nx; if(bad>64)break; } f->flow=0; }
    for(int k=0;k<8;k++)aobj_free(&f->loc[k]); for(int k=0;k<7;k++)aobj_free(&f->arg[k]); aobj_free(&f->ret); kfree(f); g_aml_inside=0; g_aml_deadline=0; g_aml_sleep_pool=g_aml_sleep_left; while(g_aml_gl_depth>0)aml_gl(0);
    g_aml_loaded_bytes+=len; return bad?1:0; }
int aml_sta(anode* dev){ anode* s=aml_child(dev,"_STA"); if(!s)return 0x0F; u64 v=0x0F; if(aml_eval_int(s,&v))return 0x0F; return (int)v; }
void aml_run_ini(anode* n,int depth){
    if(depth>16)return;
    for(anode* c=n->child;c;c=c->next){
        if(c->kind==K_DEVICE||c->kind==K_PROCESSOR||c->kind==K_THERMAL||c->kind==K_POWER){
            int sta=aml_sta(c); if(sta&1){ anode* ini=aml_child(c,"_INI"); if(ini){ aobj r; aml_eval(ini,0,0,&r); aobj_free(&r); } }
            if((sta&1)||(sta&8))aml_run_ini(c,depth+1); }
        else if(c->kind==K_SCOPE)aml_run_ini(c,depth+1); } }
void aml_run_reg(anode* n,int depth,u32 spaces){
    if(depth>16)return;
    for(anode* c=n->child;c;c=c->next){
        if(c->kind==K_REGION&&c->space<32&&((spaces>>c->space)&1)&&n!=g_aroot){ anode* reg=aml_child(n,"_REG"); if(reg){ aobj a[2]; a[0].type=AT_NONE; a[0].p=0; a[0].e=0; a[0].own=0; a[1].type=AT_NONE; a[1].p=0; a[1].e=0; a[1].own=0; aobj_int(&a[0],c->space); aobj_int(&a[1],1); aobj r; aml_eval(reg,a,2,&r); aobj_free(&r); aobj_free(&a[0]); aobj_free(&a[1]); } }
        if(c->child&&c->kind!=K_METHOD)aml_run_reg(c,depth+1,spaces); } }
int aml_notify_pop(anode** n,u32* v){ if(g_anq_h==g_anq_t)return 0; *n=g_anq[g_anq_h].node; *v=g_anq[g_anq_h].val; g_anq_h=(g_anq_h+1)&31; return 1; }
int aml_hid_is(anode* dev,const char* id){
    const char* names[2]={"_HID","_CID"};
    for(int k=0;k<2;k++){ anode* h=aml_child(dev,names[k]); if(!h)continue; aobj o; o.type=AT_NONE; o.p=0; o.e=0; o.own=0; o.len=0; if(h->kind==K_NAME)aobj_copy(&o,&h->v); else if(aml_eval(h,0,0,&o)){ aobj_free(&o); continue; }
        int m=0; aobj* list=&o; u32 cnt=1; if(o.type==AT_PKG){ list=o.e; cnt=o.len; }
        for(u32 q=0;q<cnt&&!m;q++){ aobj* x=&list[q]; char s[12]; s[0]=0;
            if(x->type==AT_STR){ u32 l=x->len<11?x->len:11; for(u32 z=0;z<l;z++)s[z]=(char)x->p[z]; s[l]=0; }
            else if(x->type==AT_INT){ u32 idv=(u32)x->i; u8 b0=(u8)idv,b1=(u8)(idv>>8),b2=(u8)(idv>>16),b3=(u8)(idv>>24); u16 w=(u16)((b0<<8)|b1); static const char* H="0123456789ABCDEF";
                s[0]=(char)(((w>>10)&0x1F)+0x40); s[1]=(char)(((w>>5)&0x1F)+0x40); s[2]=(char)((w&0x1F)+0x40); s[3]=H[b2>>4]; s[4]=H[b2&15]; s[5]=H[b3>>4]; s[6]=H[b3&15]; s[7]=0; }
            int z=0; while(id[z]&&s[z]==id[z])z++; if(!id[z]&&!s[z])m=1; }
        aobj_free(&o); if(m)return 1; }
    return 0; }
int aml_find_devices(anode* n,const char* id,anode** out,int max,int cnt,int depth){
    if(depth>16)return cnt;
    for(anode* c=n->child;c&&cnt<max;c=c->next){ if(c->kind==K_DEVICE&&aml_hid_is(c,id))out[cnt++]=c; if(c->child)cnt=aml_find_devices(c,id,out,max,cnt,depth+1); }
    return cnt; }
void aml_path(anode* n,char* out,int cap){ anode* st[16]; int d=0; for(anode* x=n;x&&x!=g_aroot&&d<16;x=x->parent)st[d++]=x; int o=0; if(o<cap-1)out[o++]='\\';
    for(int i=d-1;i>=0;i--){ for(int k=0;k<4&&o<cap-1;k++)out[o++]=st[i]->nm[k]; if(i&&o<cap-1)out[o++]='.'; } out[o]=0; }
static void aml_free_tree(anode* n){ while(n->child){ anode* c=n->child; aml_free_tree(c); n->child=c->next; aobj_free(&c->v); kfree(c); } }
static void aml_reset(void){ g_aml_glnode=0; g_aml_gl_depth=0; if(g_aroot){ aml_free_tree(g_aroot); aobj_free(&g_aroot->v); kfree(g_aroot); g_aroot=0; } g_aml_nodes=0; g_aml_loaded_bytes=0; g_aml_err=0; g_aml_lasterr[0]=0; g_anq_h=g_anq_t=0; }
