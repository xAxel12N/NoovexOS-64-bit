#ifndef NVX_CORE_POWER_H
#define NVX_CORE_POWER_H

#include "include/nvx/base.h"

struct nvx_g_acpi_t { int ok; u64 rsdp; int rev; u32 smi_cmd; u8 acpi_en; u32 pm1a_evt, pm1b_evt, pm1a_cnt, pm1b_cnt; u8 pm1_evt_len;
    int have_s5, have_s1; u32 s5a, s5b, s1a, s1b; u32 flags; u8 rst_space; u64 rst_addr; u8 rst_val; int hwred; u8 slp_space; u64 slp_addr; };

extern struct nvx_g_acpi_t g_acpi;

u32 acpi_r32(const u8* p);
u64 acpi_r64(const u8* p);
u64 acpi_find_rsdp(void);
const u8* acpi_tbl_ok(u64 a);
void power_init(void);
void acpi_enable_mode(void);
void power_off(void);
void power_reboot(void);
int power_sleep(void);

#endif
