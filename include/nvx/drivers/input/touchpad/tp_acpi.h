#ifndef NVX_DRIVERS_INPUT_TP_ACPI_H
#define NVX_DRIVERS_INPUT_TP_ACPI_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"

typedef struct { int ok; u16 addr; u32 speed; char ctrl[72]; int has_gpio; u16 pin; u8 active_low; char gctrl[72]; } tpa_res;

extern int g_tpa_crs_ok, g_tpa_dsm_reg, g_tpa_ps0, g_tpa_pr0, g_tpa_timing, g_tpa_lpss;
extern u16 g_tpa_ss[3], g_tpa_fs[3];

int tpa_crs(const char* dev,tpa_res* r);
int tpa_dsm_hid(const char* dev);
void tpa_power_on(const char* dev);
int tpa_timing(const char* ctrl,u16 ss[3],u16 fs[3]);
void tpa_lpss_init(u64 base);
u64 tpa_map(u64 base);
int tpa_hid(const char* dev,char* out);

#endif
