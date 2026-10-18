#include "include/nvx/drivers/video/gpukern.h"

#define UD G9_UD
#define SD G9_D
static g9asm* A;
static g9op D(int n){ return g9_d(n,0,UD); }
static g9op DS(int n,int i){ return g9_d(n,i,UD); }
static g9op V(int n){ return g9_r(n,UD); }
static g9op S(int n,int i){ return g9_s(n,i,UD); }
static g9op I(u32 v){ return g9_i(v,UD); }
static g9op C(int k){ return g9_s(1+k/8,k%8,UD); }
static g9op CW(int k){ return g9_sw(1+k/8,k%8); }
static void mov(int es,g9op d,g9op s){ g9_op1(A,G9_MOV,es,d,s); }
static void op(int o,int es,g9op d,g9op s0,g9op s1){ g9_op2(A,o,es,d,s0,s1); }
static void asr31(int r){ op(G9_ASR,8,g9_d(r,0,SD),g9_r(r,SD),I(31)); }
static void header(int h,int xreg_s,int xi,int ys,int yi,int dx,int dy){
    mov(8,D(h),V(0));
    if(dx){ op(G9_ADD,1,DS(5,6),S(xreg_s,xi),C(dx)); op(G9_SHL,1,DS(h,0),S(5,6),I(2)); } else op(G9_SHL,1,DS(h,0),S(xreg_s,xi),I(2));
    if(dy) op(G9_ADD,1,DS(h,1),S(ys,yi),C(dy)); else mov(1,DS(h,1),S(ys,yi));
    mov(1,DS(h,2),I(0x0007001Fu)); }
static void header_off(int h,int xoff,int yoff,int cdx,int cdy){
    mov(8,D(h),V(0));
    op(G9_ADD,1,DS(5,6),S(5,0),C(cdx)); if(xoff)op(G9_ADD,1,DS(5,6),S(5,6),I((u32)xoff)); op(G9_SHL,1,DS(h,0),S(5,6),I(2));
    op(G9_ADD,1,DS(h,1),S(5,1),C(cdy)); if(yoff)op(G9_ADD,1,DS(h,1),S(h,1),I((u32)yoff));
    mov(1,DS(h,2),I(0x0007001Fu)); }
static void rd(int dst,int hdr,int bti){ g9_send(A,8,D(dst),hdr,G9_SFID_DC1,g9_mbr_desc(bti,8),0); }
static void prologue(void){
    mov(8,g9_d(3,0,G9_UW),g9_i(0x76543210u,G9_UV));
    mov(8,D(4),g9_r(3,G9_UW));
    op(G9_SHL,1,DS(5,0),S(0,1),I(3)); op(G9_ADD,1,DS(5,0),S(5,0),C(GC_BX));
    op(G9_SHL,1,DS(5,1),S(0,6),I(3)); op(G9_ADD,1,DS(5,1),S(5,1),C(GC_BY));
    op(G9_ADD,8,D(6),V(4),S(5,0));
    op(G9_ADD,8,D(7),V(6),C(GC_NX));
    op(G9_ADD,8,D(90),V(7),I(0));
    op(G9_ADD,8,D(8),V(7),C(GC_NW));
    asr31(7); asr31(8); op(G9_XOR,8,D(7),V(7),I(0xFFFFFFFFu)); op(G9_AND,8,D(9),V(8),V(7));
    op(G9_ADD,8,D(10),V(4),S(5,1));
    op(G9_ADD,8,D(10),V(10),C(GC_NY));
    op(G9_ADD,8,D(93),V(10),I(0));
    op(G9_ADD,8,D(11),V(10),C(GC_NH));
    asr31(10); asr31(11); op(G9_XOR,8,D(10),V(10),I(0xFFFFFFFFu)); op(G9_AND,8,D(11),V(11),V(10));
    op(G9_XOR,1,DS(5,3),C(GC_A),I(0xFFFFFFFFu)); op(G9_ADD,1,DS(5,3),S(5,3),I(257));
    op(G9_XOR,1,DS(5,4),C(GC_KEY),I(0xFFFFFFFFu));
    header(20,5,0,5,1,0,0); rd(21,20,0); }
static void blend(int o,g9op d,g9op s,g9op a,g9op ia){
    op(G9_AND,8,D(46),s,I(0x00FF00FFu)); op(G9_AND,8,D(47),d,I(0x00FF00FFu));
    op(G9_SHR,8,D(48),s,I(8)); op(G9_AND,8,D(48),V(48),I(0x00FF00FFu)); op(G9_SHR,8,D(49),d,I(8)); op(G9_AND,8,D(49),V(49),I(0x00FF00FFu));
    op(G9_MUL,8,D(46),V(46),a); op(G9_MUL,8,D(47),V(47),ia); op(G9_MUL,8,D(48),V(48),a); op(G9_MUL,8,D(49),V(49),ia);
    op(G9_ADD,8,D(46),V(46),V(47)); op(G9_ADD,8,D(48),V(48),V(49));
    op(G9_SHR,8,D(46),V(46),I(8)); op(G9_AND,8,D(46),V(46),I(0x00FF00FFu));
    op(G9_AND,8,D(48),V(48),I(0xFF00FF00u));
    op(G9_OR,8,D(o),V(46),V(48)); }
static void rowmask(int j){ op(G9_AND,8,D(15),V(9),S(11,j)); }
static void keymask(int src){ op(G9_SHR,8,D(17),V(src),I(24)); op(G9_ADD,8,D(17),V(17),I(0xFFFFFFFFu)); asr31(17); op(G9_OR,8,D(17),V(17),S(5,4)); op(G9_AND,8,D(15),V(15),V(17)); }
static void select_n(int j,g9op n){ op(G9_XOR,8,D(16),V(21+j),n); op(G9_AND,8,D(16),V(16),V(15)); op(G9_XOR,8,D(21+j),V(21+j),V(16)); }
static void epilogue(void){ g9_send(A,8,g9_null(),20,G9_SFID_DC1,g9_mbw_desc(0,8),0); g9_eot(A); }
static void maskalpha(int src,int o,int oi){ op(G9_MUL,8,D(o),V(src),CW(GC_A)); op(G9_SHR,8,D(o),V(o),I(8)); op(G9_XOR,8,D(oi),V(o),I(0xFFFFFFFFu)); op(G9_ADD,8,D(oi),V(oi),I(257)); }
static void k_fill(void){ prologue(); for(int j=0;j<8;j++){ rowmask(j); select_n(j,C(GC_C0)); } epilogue(); }
static void k_blend(void){ prologue(); for(int j=0;j<8;j++){ rowmask(j); blend(16,V(21+j),C(GC_C0),CW(GC_A),g9_sw(5,3)); op(G9_XOR,8,D(16),V(21+j),V(16)); op(G9_AND,8,D(16),V(16),V(15)); op(G9_XOR,8,D(21+j),V(21+j),V(16)); } epilogue(); }
static void k_mask(void){ prologue(); header_off(14,0,0,GC_MDX,GC_MDY); rd(38,14,2);
    for(int j=0;j<8;j++){ rowmask(j); maskalpha(38+j,17,18); blend(16,V(21+j),C(GC_C0),g9_rw(17,0),g9_rw(18,0)); op(G9_XOR,8,D(16),V(21+j),V(16)); op(G9_AND,8,D(16),V(16),V(15)); op(G9_XOR,8,D(21+j),V(21+j),V(16)); } epilogue(); }
static void over(int j){ int s=30+j, d=21+j;
    op(G9_SHR,8,D(17),V(s),I(24)); op(G9_SHR,8,D(18),V(17),I(7)); op(G9_ADD,8,D(17),V(17),V(18));
    op(G9_AND,8,D(19),V(d),I(0x00FF00FFu)); op(G9_MUL,8,D(19),V(19),g9_rw(17,0)); op(G9_SHR,8,D(19),V(19),I(8)); op(G9_AND,8,D(19),V(19),I(0x00FF00FFu));
    op(G9_SHR,8,D(18),V(d),I(8)); op(G9_AND,8,D(18),V(18),I(0x00FF00FFu)); op(G9_MUL,8,D(18),V(18),g9_rw(17,0)); op(G9_AND,8,D(18),V(18),I(0xFF00FF00u));
    op(G9_ADD,8,D(s),V(s),V(19)); op(G9_ADD,8,D(s),V(s),V(18)); }
static void k_copy(int withmask,int ov){ prologue(); header_off(13,0,0,GC_SDX,GC_SDY); rd(30,13,1); if(withmask){ header_off(14,0,0,GC_MDX,GC_MDY); rd(38,14,2); }
    for(int j=0;j<8;j++){ rowmask(j); if(ov)over(j); else keymask(30+j);
        if(withmask){ maskalpha(38+j,18,19); blend(16,V(21+j),V(30+j),g9_rw(18,0),g9_rw(19,0)); }
        else blend(16,V(21+j),V(30+j),CW(GC_A),g9_sw(5,3));
        op(G9_XOR,8,D(16),V(21+j),V(16)); op(G9_AND,8,D(16),V(16),V(15)); op(G9_XOR,8,D(21+j),V(21+j),V(16)); }
    epilogue(); }
static void k_grad(int vert){ prologue();
    int t=vert?93:90; op(G9_ADD,8,D(t),V(t),C(GC_GOFF)); op(G9_MUL,8,D(t),V(t),CW(GC_INV)); op(G9_SHR,8,D(t),V(t),I(8)); op(G9_XOR,8,D(t+1),V(t),I(0xFFFFFFFFu)); op(G9_ADD,8,D(t+1),V(t+1),I(257));
    blend(t+2,C(GC_C0),C(GC_C1),g9_rw(t,0),g9_rw(t+1,0));
    for(int j=0;j<8;j++){ rowmask(j); select_n(j,vert?S(t+2,j):V(t+2)); } epilogue(); }
static void k_tint(void){ prologue();
    for(int j=0;j<8;j++){ rowmask(j); int d=21+j;
        op(G9_AND,8,D(16),V(d),I(0xFFFF0000u));
        op(G9_SHR,8,D(17),V(d),I(8)); op(G9_AND,8,D(17),V(17),I(0xFF)); op(G9_MUL,8,D(17),V(17),g9_i(15,G9_UW)); op(G9_SHR,8,D(17),V(17),I(4)); op(G9_SHL,8,D(17),V(17),I(8)); op(G9_OR,8,D(16),V(16),V(17));
        op(G9_AND,8,D(18),V(d),I(0xFF)); op(G9_MUL,8,D(18),V(18),g9_i(5,G9_UW)); op(G9_SHR,8,D(18),V(18),I(3)); op(G9_OR,8,D(16),V(16),V(18));
        select_n(j,V(16)); }
    epilogue(); }
static void k_blurh(void){ prologue(); header_off(13,-4,0,GC_SDX,GC_SDY); rd(30,13,1); header_off(14,4,0,GC_SDX,GC_SDY); rd(38,14,1);
    for(int j=0;j<8;j++){ rowmask(j);
        mov(8,D(50),V(30+j)); mov(8,D(51),V(38+j));
        op(G9_AND,16,D(52),V(50),I(0x00FF00FFu)); op(G9_SHR,16,D(54),V(50),I(8)); op(G9_AND,16,D(54),V(54),I(0x00FF00FFu));
        mov(8,D(56),V(52)); mov(8,D(57),V(54));
        for(int k=1;k<8;k++){ op(G9_ADD,8,D(56),V(56),g9_rg(52,k*4,UD,1,1,0)); op(G9_ADD,8,D(57),V(57),g9_rg(54,k*4,UD,1,1,0)); }
        op(G9_SHR,8,D(56),V(56),I(3)); op(G9_AND,8,D(56),V(56),I(0x00FF00FFu)); op(G9_SHL,8,D(57),V(57),I(5)); op(G9_AND,8,D(57),V(57),I(0xFF00FF00u)); op(G9_OR,8,D(16),V(56),V(57));
        op(G9_XOR,8,D(16),V(21+j),V(16)); op(G9_AND,8,D(16),V(16),V(15)); op(G9_XOR,8,D(21+j),V(21+j),V(16)); }
    epilogue(); }
static void k_blurv(void){ prologue(); header_off(13,0,-4,GC_SDX,GC_SDY); rd(30,13,1); header_off(14,0,4,GC_SDX,GC_SDY); rd(38,14,1);
    for(int i=0;i<16;i+=2){ op(G9_AND,16,D(50+i),V(30+i),I(0x00FF00FFu)); op(G9_SHR,16,D(66+i),V(30+i),I(8)); op(G9_AND,16,D(66+i),V(66+i),I(0x00FF00FFu)); }
    for(int j=0;j<8;j++){ rowmask(j);
        mov(8,D(56+30),V(50+j)); mov(8,D(57+30),V(66+j));
        for(int k=1;k<8;k++){ op(G9_ADD,8,D(86),V(86),V(50+j+k)); op(G9_ADD,8,D(87),V(87),V(66+j+k)); }
        op(G9_SHR,8,D(86),V(86),I(3)); op(G9_AND,8,D(86),V(86),I(0x00FF00FFu)); op(G9_SHL,8,D(87),V(87),I(5)); op(G9_AND,8,D(87),V(87),I(0xFF00FF00u)); op(G9_OR,8,D(16),V(86),V(87));
        op(G9_XOR,8,D(16),V(21+j),V(16)); op(G9_AND,8,D(16),V(16),V(15)); op(G9_XOR,8,D(21+j),V(21+j),V(16)); }
    epilogue(); }
void gk_build(int id,g9asm* a){ A=a;
    switch(id){ case GK_FILL: k_fill(); break; case GK_BLEND: k_blend(); break; case GK_MASK: k_mask(); break; case GK_COPY: k_copy(0,0); break; case GK_COPYM: k_copy(1,0); break; case GK_OVER: k_copy(0,1); break; case GK_OVERM: k_copy(1,1); break;
        case GK_GRADH: k_grad(0); break; case GK_GRADV: k_grad(1); break; case GK_TINT: k_tint(); break; case GK_BLURH: k_blurh(); break; case GK_BLURV: k_blurv(); break; } }
const char* gk_name(int id){ static const char* N[GK_N]={"fill","blend","mask","copy","copy+mask","gradient h","gradient v","night tint","blur h","blur v","over","over+mask"}; return (id>=0&&id<GK_N)?N[id]:"?"; }
