#ifndef _NVX_SYSMACROS_H
#define _NVX_SYSMACROS_H
#define major(d) ((unsigned int)(((d)>>8)&0xfff))
#define minor(d) ((unsigned int)(((d)&0xff)|(((d)>>12)&0xfff00)))
#define makedev(ma,mi) ((((unsigned long long)(ma)&0xfff)<<8)|((unsigned long long)(mi)&0xff)|(((unsigned long long)(mi)&0xfff00)<<12))
#endif
