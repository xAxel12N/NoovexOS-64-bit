#ifndef NVX_EVDEV_ABI_H
#define NVX_EVDEV_ABI_H
#define AEV_IOC(d,t,n,s) ((u32)(((u32)(d)<<30)|((u32)(s)<<16)|((u32)(t)<<8)|(u32)(n)))
#define AEV_EVSZ 24
#define AEV_ABSSZ 24
#define AEV_FFSZ 48
#define AEV_JSSZ 8
#define AEV_VERSION 0x010001
#define AEV_JSVERSION 0x020100
#define AEV_GVERSION AEV_IOC(2,'E',0x01,4)
#define AEV_GID AEV_IOC(2,'E',0x02,8)
#define AEV_GREP AEV_IOC(2,'E',0x03,8)
#define AEV_SFF AEV_IOC(1,'E',0x80,AEV_FFSZ)
#define AEV_RMFF AEV_IOC(1,'E',0x81,4)
#define AEV_GEFFECTS AEV_IOC(2,'E',0x84,4)
#define AEV_GRAB AEV_IOC(1,'E',0x90,4)
#define AEV_REVOKE AEV_IOC(1,'E',0x91,4)
#define AEV_SCLOCKID AEV_IOC(1,'E',0xA0,4)
#define AEV_JSGVERSION AEV_IOC(2,'j',0x01,4)
#define AEV_JSGAXES AEV_IOC(2,'j',0x11,1)
#define AEV_JSGBUTTONS AEV_IOC(2,'j',0x12,1)
#define AEV_JSGAXMAP AEV_IOC(2,'j',0x32,64)
#define AEV_JSGBTNMAP AEV_IOC(2,'j',0x34,1024)
#define AEV_FF_RUMBLE 0x50
#define AEV_FF_GAIN 0x60
#define AEV_EV_FF 0x15
#endif
