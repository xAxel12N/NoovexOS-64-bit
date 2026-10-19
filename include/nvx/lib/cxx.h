#ifndef NVX_LIB_CXX_H
#define NVX_LIB_CXX_H

#ifndef __cplusplus
#error "include/nvx/lib/cxx.h is for C++ units"
#endif

extern "C" {
#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/cxx/cxxrt.h"
}

inline void* operator new(unsigned long, void* p) noexcept { return p; }

namespace nvx {

template<typename T> class KBuf {
public:
    KBuf() : p_(0), n_(0) {}
    explicit KBuf(unsigned long n) : p_(0), n_(0) { alloc(n); }
    ~KBuf() { reset(); }
    KBuf(const KBuf&) = delete;
    KBuf& operator=(const KBuf&) = delete;
    bool alloc(unsigned long n) { reset(); if(!n) return false; p_ = (T*)kmalloc(n * sizeof(T)); n_ = p_ ? n : 0; return p_ != 0; }
    void reset() { if(p_) kfree(p_); p_ = 0; n_ = 0; }
    T* get() const { return p_; }
    unsigned long size() const { return n_; }
    explicit operator bool() const { return p_ != 0; }
    T& operator[](unsigned long i) const { return p_[i]; }
private:
    T* p_;
    unsigned long n_;
};

template<typename T> inline T min(T a, T b) { return a < b ? a : b; }
template<typename T> inline T max(T a, T b) { return a > b ? a : b; }
template<typename T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

class EdgeKeys {
public:
    EdgeKeys() { for(int i = 0; i < 128; ++i) prev_[i] = 0; }
    void sync(const volatile unsigned char* k) { for(int i = 0; i < 128; ++i) prev_[i] = k[i]; }
    bool pressed(const volatile unsigned char* k, int sc) { bool r = k[sc] && !prev_[sc]; prev_[sc] = k[sc]; return r; }
private:
    unsigned char prev_[128];
};

}

#endif
