#ifndef NVX_DRIVERS_USB_UVC_IR_H
#define NVX_DRIVERS_USB_UVC_IR_H
#include "include/nvx/base.h"
enum { UIR_IDLE=0, UIR_BASE, UIR_TRY, UIR_ON, UIR_NONE, UIR_SAVED };
int uvc_ir_stream(void);
int uvc_streams(void); int uvc_stream_cur(void); int uvc_select_stream(int k);
int uvc_ir_search_begin(void);
int uvc_ir_emitter_apply(int unit,int sel,int len,const u8* v);
void uvc_ir_poll(void);
int uvc_ir_emitter(void);
int uvc_ir_emitter_ctl(int* unit,int* sel,int* len,u8* v);
void uvc_ir_reset(void);
#endif
