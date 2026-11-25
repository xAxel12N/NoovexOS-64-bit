#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVFLIP_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVFLIP_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/damage.h"

#define NVC57D_UPDATE                     0x0200u
#define NVC57D_SET_CONTEXT_DMA_NOTIFIER   0x0208u
#define NVC57D_SET_NOTIFIER_CONTROL       0x020Cu
#define NVC57D_SET_INTERLOCK_FLAGS        0x0218u
#define NVC57D_SET_WINDOW_INTERLOCK_FLAGS 0x021Cu
#define NVC57D_SOR_SET_CONTROL(o)         (0x0300u+(u32)(o)*0x20u)
#define NVC57D_WINDOW_SET_CONTROL(w)      (0x1000u+(u32)(w)*0x80u)
#define NVC57D_WINDOW_SET_FORMAT_USAGE(w) (0x1004u+(u32)(w)*0x80u)
#define NVC57D_WINDOW_SET_USAGE_BOUNDS(w) (0x1010u+(u32)(w)*0x80u)
#define NVC57D_HEAD(h)                    ((u32)(h)*0x400u)
#define NVC57D_HEAD_SET_PROCAMP(h)        (0x2000u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_OUTPUT_RESOURCE(h) (0x2004u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_CONTROL(h)        (0x2008u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_PIXEL_CLOCK(h)    (0x200Cu+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_DITHER(h)         (0x2018u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_DISPLAY_ID(h)     (0x2020u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_PIXEL_CLOCK_MAX(h) (0x2028u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_USAGE_BOUNDS(h)   (0x2030u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_VIEWPORT_IN(h)    (0x204Cu+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_VIEWPORT_OUT(h)   (0x2058u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_RASTER_SIZE(h)    (0x2064u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_RASTER_LOCK(h)    (0x2074u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_OLUT_CONTROL(h)   (0x2280u+NVC57D_HEAD(h))
#define NVC57D_HEAD_SET_CONTEXT_DMA_OLUT(h) (0x2288u+NVC57D_HEAD(h))

#define NVC57E_UPDATE                     0x0200u
#define NVC57E_SET_CONTEXT_DMA_NOTIFIER   0x021Cu
#define NVC57E_SET_NOTIFIER_CONTROL       0x0220u
#define NVC57E_SET_SIZE                   0x0224u
#define NVC57E_SET_STORAGE                0x0228u
#define NVC57E_SET_PARAMS                 0x022Cu
#define NVC57E_SET_PLANAR_STORAGE0        0x0230u
#define NVC57E_SET_CONTEXT_DMA_ISO0       0x0240u
#define NVC57E_SET_OFFSET0                0x0260u
#define NVC57E_SET_POINT_IN0              0x0290u
#define NVC57E_SET_SIZE_IN                0x0298u
#define NVC57E_SET_SIZE_OUT               0x02A4u
#define NVC57E_SET_PRESENT_CONTROL        0x0308u
#define NVC57E_SET_INTERLOCK_FLAGS        0x0370u
#define NVC57E_SET_WINDOW_INTERLOCK_FLAGS 0x0374u

#define NVC57E_PRESENT_NON_TEARING(iv)    ((u32)(iv)&0xFu)
#define NVC57E_PRESENT_IMMEDIATE          (1u<<4)
#define NVC57E_FMT_X8R8G8B8               0xE6u
#define NVC57E_FMT_A8R8G8B8               0xCFu
#define NVC57E_STORAGE_PITCH              0x10u
#define NV_DISP_NTFY_STATUS(v)            ((u32)(v)>>30)
#define NV_DISP_NTFY_NOT_BEGUN            0u
#define NV_DISP_NTFY_BEGUN                1u
#define NV_DISP_NTFY_FINISHED             2u

#define NVFLIP_HSYNC  0xF0000000u
#define NVFLIP_HVRAM  0xF0000001u
#define NVFLIP_HFB    0xFB000000u
#define NVFLIP_NBUF   2
#define NVFLIP_NTFY_OFF 0x100u
#define NVFLIP_RMAX 32

typedef struct { u32 khz; u16 hact, hfp, hsw, hbp, vact, vfp, vsw, vbp; u8 nhs, nvs; } nvflip_mode_t;

typedef struct {
    int up, head, win, sor, cur, pend;
    u32 w, h, pitch, khz;
    u64 vram[NVFLIP_NBUF];
    u32 flips, waits, timeouts, fallbacks;
    const char* why;
    int ce, native;
    u32 fww, fwh, ceframes, cpuframes, rframes, idle, done;
    const char* cewhy;
    u32 mw, mh, scale;
} nvflip_info_t;

extern int g_nvflip_want, g_nvflip_live;

int nvflip_setting(int on);
int nvflip_on(void);
void nvflip_off(const char* why);
int nvflip_active(void);
int nvflip_info(nvflip_info_t* o);
char* nvflip_why(void);

volatile u32* nvflip_back(u32* pitch_bytes);
int nvflip_flip(int vsync);
int nvflip_wait(u32 ms);
int nvflip_present(const u32* src,u32 src_pitch_px,u32 w,u32 h);
int nvflip_present_rects(const u32* src,u32 pitch_px,u32 w,u32 h,const dmg_rect* r,int n);
int nvflip_native(u32* fww,u32* fwh);

int nvflip_head(void);
int nvflip_core_methods(const u32* mv,int npairs);
int nvflip_core_update(u32 win_mask);
int nvflip_cursor_buf(u32 len,u64* vram,volatile u32** cpu);

u32 nvflip_emit_image(u32* pb,u32 cap,u32 w,u32 h,u32 pitch,u32 handle,u32 off8);
u32 nvflip_emit_flip(u32* pb,u32 cap,u32 off8,u32 win_mask,int vsync,u32 ntfy_handle,u32 ntfy_off);
u32 nvflip_emit_head(u32* pb,u32 cap,int head,int sor,u32 display_id,const nvflip_mode_t* m,u32 vw,u32 vh,u32 lut8,int link);

#endif
