#include "include/nvx/base.h"
#include "include/nvx/core/buildsig.h"
/* ===== NoovexOS build signature (patched post-link) ===== */
extern char __bss_end[];
__attribute__((aligned(64), used, section(".sig")))
volatile const struct nvx_g_buildsig_t g_buildsig = {
    {'N','V','X','6','4','S','I','G'},
    "9.82",
    0, 0,
    {0},
    "NoovexOS build",
    0,
    {0},
    (unsigned long long)__bss_end
};
