#ifndef NVX_CORE_KASLR_H
#define NVX_CORE_KASLR_H

#include "include/nvx/base.h"

#define KASLR_MMAP_BITS 26
#define KASLR_MMNEXT_BITS 20
#define KASLR_STK_MAX 0x10000ULL
#define KASLR_INTERP_LO 0x44010000000ULL
#define KASLR_INTERP_WIN 0x2000000000ULL
#define KASLR_STK_LO 0x46100000000ULL
#define KASLR_STK_BITS 22

extern int g_kaslr_on;
extern u64 g_kaslr_text_slide, g_kaslr_heap, g_kaslr_kstk, g_kaslr_pf_seed;
u64 kaslr_rand(u64 n);
u64 kaslr_kstk_off(void);
u64 kaslr_user_brk(void);
u64 kaslr_user_mmap(void);
u64 kaslr_gap_hint(void);
u64 kaslr_pie_slide(u64 room,int shift,u64 maxn);
u64 kaslr_stack_off(void);
u64 kaslr_stack_room(u64 room);
u64 kaslr_user_stack(u64 fixed,u64 below);
u64 kaslr_interp_slide(u64 room,int shift);
int kaslr_align_shift(u64 a);
int kaslr_hide_addr(void);
int kaslr_mask(char* s,int n);
int kaslr_report(char* o,int cap);

#endif
