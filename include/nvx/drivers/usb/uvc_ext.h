#ifndef NVX_DRIVERS_USB_UVC_EXT_H
#define NVX_DRIVERS_USB_UVC_EXT_H
#include "include/nvx/base.h"
extern u32 uvx_stills, uvx_h264_frames;
int uvx_h264_n(void); int uvx_h264_info(int i,int* w,int* h,u32* iv); int uvx_h264_start(int w,int h);
int uvx_is_capture(void); int uvx_still_method(void); int uvx_still_n(void); int uvx_still_info(int i,int* fi,int* w,int* h);
int uvx_still_snap(void); const u8* uvx_still_data(int* w,int* h,u32* len);
#endif
