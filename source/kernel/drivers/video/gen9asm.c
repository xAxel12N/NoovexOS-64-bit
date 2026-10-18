#include "include/nvx/drivers/video/gen9asm.h"

static int tsz(int t){ return (t==G9_UW||t==G9_W)?2:4; }
static u8 es_code(int es){ return es==1?0:es==2?1:es==4?2:es==8?3:es==16?4:5; }
g9op g9_r(int nr,int type){ g9op o; o.file=G9_GRF; o.type=(u8)type; o.nr=(u8)nr; o.sub=0; o.vs=4; o.w=3; o.hs=1; o.imm=0; return o; }
g9op g9_s(int nr,int idx,int type){ g9op o; o.file=G9_GRF; o.type=(u8)type; o.nr=(u8)nr; o.sub=(u8)(idx*tsz(type)); o.vs=0; o.w=0; o.hs=0; o.imm=0; return o; }
g9op g9_d(int nr,int idx,int type){ g9op o; o.file=G9_GRF; o.type=(u8)type; o.nr=(u8)nr; o.sub=(u8)(idx*tsz(type)); o.vs=0; o.w=0; o.hs=1; o.imm=0; return o; }
g9op g9_i(u32 v,int type){ g9op o; o.file=G9_IMM; o.type=(u8)type; o.nr=0; o.sub=0; o.vs=0; o.w=0; o.hs=0; o.imm=(type==G9_UW||type==G9_W)?((v&0xFFFFu)|(v<<16)):v; return o; }
g9op g9_null(void){ g9op o; o.file=G9_ARF; o.type=G9_UD; o.nr=0; o.sub=0; o.vs=0; o.w=0; o.hs=1; o.imm=0; return o; }
static void sb(u32* w,int hi,int lo,u32 v){ for(int b=lo;b<=hi;b++){ u32 m=1u<<(b&31); if((v>>(b-lo))&1u)w[b>>5]|=m; else w[b>>5]&=~m; } }
static u8 rc_vs(int v){ return v==0?0:v==1?1:v==2?2:v==4?3:v==8?4:v==16?5:6; }
static u8 rc_w(int v){ return v==1?0:v==2?1:v==4?2:v==8?3:4; }
static u8 rc_hs(int v){ return v==0?0:v==1?1:v==2?2:3; }
g9op g9_rg(int nr,int byteoff,int type,int vs,int w,int hs){ g9op o; o.file=G9_GRF; o.type=(u8)type; o.nr=(u8)(nr+byteoff/32); o.sub=(u8)(byteoff%32); o.vs=rc_vs(vs); o.w=rc_w(w); o.hs=rc_hs(hs); o.imm=0; return o; }
g9op g9_rw(int nr,int dwidx){ return g9_rg(nr,dwidx*4,G9_UW,16,8,2); }
g9op g9_sw(int nr,int dwidx){ return g9_rg(nr,dwidx*4,G9_UW,0,1,0); }
void g9_begin(g9asm* a,u32* buf,int max_insn){ a->p=buf; a->n=0; a->max=max_insn; a->err=0; }
static u32* slot(g9asm* a){ if(a->n>=a->max){ a->err=1; return 0; } u32* w=a->p+a->n*4; w[0]=w[1]=w[2]=w[3]=0; a->n++; return w; }
static void hdr(u32* w,int op,int es){ sb(w,6,0,(u32)op); sb(w,23,21,es_code(es)); sb(w,34,34,1); }
static void dst(u32* w,g9op d){ sb(w,36,35,d.file); sb(w,40,37,d.type); sb(w,62,61,d.hs); sb(w,60,53,d.nr); sb(w,52,48,d.sub); }
static void src0(u32* w,g9op s){ sb(w,42,41,s.file); sb(w,46,43,s.type);
    if(s.file==G9_IMM){ w[3]=s.imm; return; }
    sb(w,76,69,s.nr); sb(w,68,64,s.sub); sb(w,81,80,s.hs); sb(w,84,82,s.w); sb(w,88,85,s.vs); }
static void src1(u32* w,g9op s){ sb(w,90,89,s.file); sb(w,94,91,s.type);
    if(s.file==G9_IMM){ w[3]=s.imm; return; }
    sb(w,108,101,s.nr); sb(w,100,96,s.sub); sb(w,113,112,s.hs); sb(w,116,114,s.w); sb(w,120,117,s.vs); }
void g9_op1(g9asm* a,int op,int es,g9op d,g9op s0){ u32* w=slot(a); if(!w)return; hdr(w,op,es); dst(w,d); src0(w,s0); }
void g9_op2(g9asm* a,int op,int es,g9op d,g9op s0,g9op s1){ u32* w=slot(a); if(!w)return; if(s0.file==G9_IMM){ a->err=2; return; } hdr(w,op,es); dst(w,d); src0(w,s0); src1(w,s1); }
void g9_send(g9asm* a,int es,g9op d,int src,int sfid,u32 desc,int eot){ u32* w=slot(a); if(!w)return;
    hdr(w,G9_SEND,es); sb(w,27,24,(u32)sfid); d.type=G9_UW; dst(w,d); sb(w,42,41,G9_GRF); sb(w,46,43,G9_UD); sb(w,76,69,(u32)src);
    sb(w,90,89,G9_IMM); sb(w,94,91,G9_UD); w[3]=(desc&0x7FFFFFFFu)|(eot?0x80000000u:0u); }
u32 g9_mbw_desc(int bti,int rows_regs){ return ((u32)(1+rows_regs)<<25)|(1u<<19)|(10u<<14)|(u32)(bti&0xFF); }
u32 g9_mbr_desc(int bti,int rows_regs){ return (1u<<25)|((u32)rows_regs<<20)|(1u<<19)|(4u<<14)|(u32)(bti&0xFF); }
void g9_eot(g9asm* a){ g9_op1(a,G9_MOV,8,g9_d(127,0,G9_UD),g9_r(0,G9_UD)); g9_send(a,8,g9_null(),127,G9_SFID_TS,0x02000010u,1); }
void g9_kernel_fill(g9asm* a){
    g9_op1(a,G9_MOV,8,g9_d(3,0,G9_UW),g9_i(0x76543210u,G9_UV));
    g9_op1(a,G9_MOV,8,g9_d(4,0,G9_UD),g9_r(3,G9_UW));
    g9_op2(a,G9_SHL,1,g9_d(2,0,G9_UD),g9_s(0,1,G9_UD),g9_i(3,G9_UD));
    g9_op2(a,G9_SHL,1,g9_d(2,1,G9_UD),g9_s(0,6,G9_UD),g9_i(3,G9_UD));
    g9_op2(a,G9_ADD,8,g9_d(4,0,G9_UD),g9_r(4,G9_UD),g9_s(2,0,G9_UD));
    g9_op2(a,G9_SHR,8,g9_d(5,0,G9_UD),g9_r(4,G9_UD),g9_s(1,1,G9_UD));
    g9_op2(a,G9_AND,8,g9_d(5,0,G9_UD),g9_r(5,G9_UD),g9_i(0xFF,G9_UD));
    g9_op2(a,G9_SHL,8,g9_d(5,0,G9_UD),g9_r(5,G9_UD),g9_i(16,G9_UD));
    g9_op2(a,G9_OR,8,g9_d(5,0,G9_UD),g9_r(5,G9_UD),g9_s(1,0,G9_UD));
    for(int j=0;j<8;j++){
        g9_op2(a,G9_ADD,1,g9_d(2,2,G9_UD),g9_s(2,1,G9_UD),g9_i((u32)j,G9_UD));
        g9_op2(a,G9_SHR,1,g9_d(2,3,G9_UD),g9_s(2,2,G9_UD),g9_s(1,2,G9_UD));
        g9_op2(a,G9_AND,1,g9_d(2,3,G9_UD),g9_s(2,3,G9_UD),g9_i(0xFF,G9_UD));
        g9_op2(a,G9_SHL,1,g9_d(2,3,G9_UD),g9_s(2,3,G9_UD),g9_i(8,G9_UD));
        g9_op2(a,G9_OR,8,g9_d(11+j,0,G9_UD),g9_r(5,G9_UD),g9_s(2,3,G9_UD)); }
    g9_op1(a,G9_MOV,8,g9_d(10,0,G9_UD),g9_r(0,G9_UD));
    g9_op2(a,G9_SHL,1,g9_d(10,0,G9_UD),g9_s(0,1,G9_UD),g9_i(5,G9_UD));
    g9_op1(a,G9_MOV,1,g9_d(10,1,G9_UD),g9_s(2,1,G9_UD));
    g9_op1(a,G9_MOV,1,g9_d(10,2,G9_UD),g9_i(0x0007001Fu,G9_UD));
    g9_send(a,8,g9_null(),10,G9_SFID_DC1,g9_mbw_desc(0,8),0);
    g9_eot(a); }
void g9_kernel_copy(g9asm* a){
    g9_op1(a,G9_MOV,8,g9_d(10,0,G9_UD),g9_r(0,G9_UD));
    g9_op2(a,G9_SHL,1,g9_d(10,0,G9_UD),g9_s(0,1,G9_UD),g9_i(5,G9_UD));
    g9_op2(a,G9_SHL,1,g9_d(10,1,G9_UD),g9_s(0,6,G9_UD),g9_i(3,G9_UD));
    g9_op1(a,G9_MOV,1,g9_d(10,2,G9_UD),g9_i(0x0007001Fu,G9_UD));
    g9_send(a,8,g9_d(11,0,G9_UD),10,G9_SFID_DC1,g9_mbr_desc(1,8),0);
    g9_send(a,8,g9_null(),10,G9_SFID_DC1,g9_mbw_desc(0,8),0);
    g9_eot(a); }
