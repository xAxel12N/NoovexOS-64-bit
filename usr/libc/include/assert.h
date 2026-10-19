#ifndef NVX_ASSERT_H
#define NVX_ASSERT_H
#include <nvx/_std.h>
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x)?(void)0:(fprintf(stderr,"%s:%d: %s: Assertion `%s' failed.\n",__FILE__,__LINE__,__func__,#x),abort()))
#endif
#endif
