#ifndef NVX_DRIVERS_USB_UVC_H
#define NVX_DRIVERS_USB_UVC_H
#include "include/nvx/base.h"
extern int uvc_state, uvc_w, uvc_h, uvc_kind, uvc_fi, uvc_ri, uvc_altsel, uvc_lastcc, uvc_cfgcc, uvc_maxintr;
extern u32 uvc_maxpay, uvc_maxfs, uvc_interval, uvc_events, uvc_tds, uvc_tderr, uvc_frames, uvc_badframes, uvc_short, uvc_bytes, uvc_seq, uvc_fps10, uvc_jlen;
extern u16 uvc_vid, uvc_pid; extern char uvc_name[40], uvc_msg[96];
extern u32* uvc_rgb; extern u8* uvc_jpg; extern volatile int uvc_jnew;
int uvc_found(void); int uvc_nfr(void); int uvc_frame_info(int i,int* kind,int* w,int* h,u32* defint);
int uvc_speed(void); int uvc_nalt(void); int uvc_is_bulk(void);
int uvc_start(int hd); void uvc_stop(void); void uvc_poll(void); void uvc_gone(void);
#endif
