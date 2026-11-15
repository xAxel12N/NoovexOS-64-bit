#ifndef NVX_DRIVERS_IOMMU_VTD_H
#define NVX_DRIVERS_IOMMU_VTD_H
#include "include/nvx/base.h"
#define VTD_MAX 8
typedef struct { u64 base; u32 gsts0, gsts1, pmen0, pmen1; u64 cap; u8 flags; u8 done; } vtd_unit;
extern vtd_unit g_vtd[VTD_MAX];
extern int g_vtd_n, g_vtd_present, g_vtd_changed, g_vtd_failed;
extern u8 g_vtd_dmar_flags;
int vtd_init(void);
#endif
