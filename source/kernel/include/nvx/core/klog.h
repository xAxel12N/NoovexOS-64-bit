#ifndef NVX_CORE_KLOG_H
#define NVX_CORE_KLOG_H

#include "include/nvx/base.h"

extern void (*g_klog_hook)(const char*);

void klog_init(void);
void klog(const char* s);
void klog2(const char* a,const char* b);

#endif
