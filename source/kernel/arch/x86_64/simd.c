#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/simd.h"
/* ---- software 2D acceleration: SSE fill/copy (SSE on at boot; kernel is -mno-sse so xmm is untouched by codegen) ---- */

int g_accel2d=1, g_ssse3=0;   /* 2D acceleration toggle; SSSE3 (pshufb) available */
/* pack 32bpp BGRX -> 24bpp BGR, 4 pixels per pshufb. Stores 16 bytes (4-byte overrun stays
   inside the row because we require >=4 more pixels after the group), tail is scalar. */
const u8 pack24_mask[16] __attribute__((aligned(16)))={0,1,2,4,5,6,8,9,10,12,13,14,0x80,0x80,0x80,0x80};

