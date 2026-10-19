#ifndef _NVX_SYS_TIME_H
#define _NVX_SYS_TIME_H
#include <sys/types.h>
struct timeval { time_t tv_sec; long tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };
int gettimeofday(struct timeval*,void*);
#endif
