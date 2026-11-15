#include "include/nvx/base.h"
#include "include/nvx/core/buildsig.h"
#define NVX_SVN 1
extern char __bss_end[];
__attribute__((aligned(64), used, section(".sig")))
volatile const struct nvx_g_buildsig_t g_buildsig = {
    {'N','V','X','6','4','S','I','G'},
    "11.10",
    0, 0,
    {0},
    "NoovexOS build",
    0,
    {0},
    (unsigned long long)__bss_end,
    0, NVX_SVN
};
_Static_assert(sizeof(struct nvx_g_buildsig_t)==320,"buildsig size");
