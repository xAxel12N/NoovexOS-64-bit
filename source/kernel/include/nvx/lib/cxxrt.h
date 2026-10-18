#ifndef NVX_LIB_CXXRT_H
#define NVX_LIB_CXXRT_H

#ifdef __cplusplus
extern "C" {
#endif
int cxx_run_ctors(void);
int cxx_ctor_count(void);
#ifdef __cplusplus
}
#endif

#endif
