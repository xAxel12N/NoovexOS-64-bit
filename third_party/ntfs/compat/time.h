#ifndef _NVX_TIME_H
#define _NVX_TIME_H
#include <stddef.h>
typedef long time_t; typedef long clock_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
time_t time(time_t*); struct tm* localtime(const time_t*); struct tm* gmtime(const time_t*); char* ctime(const time_t*); size_t strftime(char*,size_t,const char*,const struct tm*); time_t mktime(struct tm*);
#endif
