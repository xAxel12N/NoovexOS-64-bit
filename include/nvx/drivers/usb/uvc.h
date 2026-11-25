#ifndef NVX_DRIVERS_USB_UVC_H
#define NVX_DRIVERS_USB_UVC_H
#include "include/nvx/base.h"
extern int uvc_state, uvc_w, uvc_h, uvc_kind, uvc_fi, uvc_ri, uvc_altsel, uvc_lastcc, uvc_cfgcc, uvc_maxintr;
extern u32 uvc_maxpay, uvc_maxfs, uvc_interval, uvc_events, uvc_tds, uvc_tderr, uvc_frames, uvc_badframes, uvc_short, uvc_bytes, uvc_seq, uvc_fps10, uvc_jlen;
extern u16 uvc_vid, uvc_pid; extern char uvc_name[40], uvc_msg[96];
extern u32* uvc_rgb; extern u8* uvc_jpg; extern volatile int uvc_jnew;
int uvc_found(void); int uvc_nfr(void); int uvc_frame_info(int i,int* kind,int* w,int* h,u32* defint);
int uvc_speed(void); int uvc_nalt(void); int uvc_is_bulk(void);
enum { UVC_C_BRIGHTNESS, UVC_C_CONTRAST, UVC_C_SATURATION, UVC_C_SHARPNESS, UVC_C_GAMMA, UVC_C_HUE, UVC_C_GAIN, UVC_C_BACKLIGHT, UVC_C_WB_AUTO, UVC_C_WB_TEMP, UVC_C_PLF, UVC_C_EXPOSURE_AUTO, UVC_C_EXPOSURE, UVC_C_FOCUS_AUTO, UVC_C_FOCUS, UVC_C_N };
extern u32 uvc_hdrbad, uvc_restarts;
const char* uvc_ctrl_name(int id); int uvc_ctrl_avail(int id); int uvc_ctrl_range(int id,int* mn,int* mx,int* df); int uvc_ctrl_get(int id,int* v); int uvc_ctrl_set(int id,int v);
#define UVC_NCAM 2
int uvc_select(int i); int uvc_selected(void); int uvc_count(void); int uvc_cam_ok(int i); int uvc_streaming_n(void);
int uvc_start(int hd); void uvc_stop(void); void uvc_poll(void); void uvc_gone(void);
#endif
