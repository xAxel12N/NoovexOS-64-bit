#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVCURSOR_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVCURSOR_H

#include "include/nvx/base.h"

#define NVC37A_UPDATE 0x0200u
#define NVC37A_SET_CURSOR_HOT_SPOT_POINT_OUT(b) (0x0208u+(b)*4u)
#define NVC37D_HEAD_SET_CONTEXT_DMA_CURSOR(a,b) (0x2088u+(a)*0x400u+(b)*4u)
#define NVC37D_HEAD_SET_OFFSET_CURSOR(a,b) (0x2090u+(a)*0x400u+(b)*4u)
#define NVC37D_HEAD_SET_CONTROL_CURSOR(a) (0x209Cu+(a)*0x400u)
#define NVC37D_HEAD_SET_CONTROL_CURSOR_COMPOSITION(a) (0x20A0u+(a)*0x400u)
#define NVC37D_CURSOR_FORMAT_A8R8G8B8 0xCFu
#define NVC57A_CURS_CHID 73
#define NVC57A_USER_BASE 0x690000u
#define NVC57A_CTRL_BASE 0x6104E0u
#define NVC57A_STAT_BASE 0x610664u

typedef struct{ u32 mthd, data; } nvcur_m;

extern int g_nvcur_enable, g_nvcur_on[4];
int nvcur_layout(int w);
u32 nvcur_ctrl_word(int on,int layout,int hx,int hy);
u32 nvcur_comp_word(void);
int nvcur_set_mthds(nvcur_m* m,int max,int head,u32 handle,u64 offset,int w,int hx,int hy);
int nvcur_clr_mthds(nvcur_m* m,int max,int head);
u32 nvcur_user(int head);
u32 nvcur_point_word(int x,int y);
int nvcur_gen_ok(u32 boot0);
int nvcur_supported(void);
int nvcur_chan_init(int head);
void nvcur_chan_fini(int head);
int nvcur_move(int head,int x,int y);
int nvcur_cmd(const char* a,char* out,int n);
u32 nvcur_emit(u32* pb,u32 cap,const nvcur_m* m,int n);
int nvcur_img_build(const u32* bk,const u32* wh,int sw,int sh,int hx,int hy,u32* out,int* ow,int* ohx,int* ohy);
void nvcur_clip(int x,int y,int w,int scw,int sch,int* ox,int* oy);
int nvcur_hw_active(void);
int nvcur_hw_head(void);
int nvcur_hw_shape(const u32* img,int w,int hx,int hy);
int nvcur_hw_move(int x,int y,int scw,int sch);
void nvcur_hw_off(void);

#endif
