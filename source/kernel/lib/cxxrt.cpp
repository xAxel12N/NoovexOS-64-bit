extern "C" {
#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/cxxrt.h"
}

typedef void (*cxx_ctor)(void);
extern "C" cxx_ctor __init_array_start[], __init_array_end[];

static int g_cxx_ctors = -1;

extern "C" int cxx_run_ctors(void){
    if(g_cxx_ctors >= 0) return g_cxx_ctors;
    int n = 0;
    for(cxx_ctor* p = __init_array_start; p < __init_array_end; ++p){
        if(*p && *p != (cxx_ctor)-1){ (*p)(); ++n; }
    }
    g_cxx_ctors = n;
    return n;
}

extern "C" int cxx_ctor_count(void){ return g_cxx_ctors; }

void* operator new(unsigned long n){ return kmalloc(n ? n : 1); }
void* operator new[](unsigned long n){ return kmalloc(n ? n : 1); }
void operator delete(void* p) noexcept { if(p) kfree(p); }
void operator delete[](void* p) noexcept { if(p) kfree(p); }
void operator delete(void* p, unsigned long) noexcept { if(p) kfree(p); }
void operator delete[](void* p, unsigned long) noexcept { if(p) kfree(p); }

extern "C" {
void* __dso_handle = 0;
int __cxa_atexit(void (*)(void*), void*, void*){ return 0; }
void __cxa_pure_virtual(void){ klog("c++: pure virtual function called"); }
}
