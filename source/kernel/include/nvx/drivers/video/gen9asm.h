#ifndef NVX_DRIVERS_VIDEO_GEN9ASM_H
#define NVX_DRIVERS_VIDEO_GEN9ASM_H

#include "include/nvx/base.h"

#define G9_ARF 0
#define G9_GRF 1
#define G9_IMM 3

#define G9_UD 0
#define G9_D  1
#define G9_UW 2
#define G9_W  3
#define G9_UV 4

#define G9_MOV 0x01
#define G9_AND 0x05
#define G9_OR  0x06
#define G9_SHR 0x08
#define G9_SHL 0x09
#define G9_XOR 0x07
#define G9_ASR 0x0C
#define G9_MUL 0x41
#define G9_ADD 0x40
#define G9_SEND 0x31

#define G9_SFID_TS  7
#define G9_SFID_DC1 12

typedef struct { u8 file, type, nr, sub, vs, w, hs; u32 imm; } g9op;
typedef struct { u32* p; int n, max, err; } g9asm;

g9op g9_r(int nr,int type);
g9op g9_s(int nr,int idx,int type);
g9op g9_d(int nr,int idx,int type);
g9op g9_i(u32 v,int type);
g9op g9_null(void);
g9op g9_rg(int nr,int byteoff,int type,int vs,int w,int hs);
g9op g9_rw(int nr,int dwidx);
g9op g9_sw(int nr,int dwidx);
void g9_begin(g9asm* a,u32* buf,int max_insn);
void g9_op1(g9asm* a,int op,int es,g9op d,g9op s0);
void g9_op2(g9asm* a,int op,int es,g9op d,g9op s0,g9op s1);
void g9_send(g9asm* a,int es,g9op d,int src,int sfid,u32 desc,int eot);
u32 g9_mbw_desc(int bti,int rows_regs);
u32 g9_mbr_desc(int bti,int rows_regs);
void g9_eot(g9asm* a);
void g9_kernel_fill(g9asm* a);
void g9_kernel_copy(g9asm* a);

#endif
