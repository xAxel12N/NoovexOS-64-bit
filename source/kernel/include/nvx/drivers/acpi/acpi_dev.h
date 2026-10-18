#ifndef NVX_DRIVERS_ACPI_ACPI_DEV_H
#define NVX_DRIVERS_ACPI_ACPI_DEV_H

#include "include/nvx/base.h"
#include "include/nvx/lib/js.h"
#include "include/nvx/drivers/acpi/aml.h"

struct nvx_g_aec_t { int ok, from_ecdt, glk, fails; u16 data, cmd; anode* dev; u32 gpe, reads, writes, queries, timeouts, events, trips; u8 lastq; u64 dead_until; };
struct nvx_g_acs_t { int up, tables, bad_tables, ec_reg, acpi_mode; u32 ms; u64 pwrbtn_t; u32 notifies, unhandled; char errs[64]; };
typedef struct { anode* dev; int present, mwh, have_info; u32 design, full, volt, warn, low, tech, cycles; u32 state, rate, remain, pvolt; char model[24], serial[24], type[12], oem[24]; } acbat;
struct nvx_g_bl_t { int ok, mode, nlv, cur, minlv; u8 lv[101]; anode* out; anode* bus; int bqc; u64 mmio; u32 pwm_kind, pwm_max; u32 sets; };

extern struct nvx_g_aec_t g_aec;
extern struct nvx_g_acs_t g_acs;
extern void (*g_acpi_pwrbtn_cb)(void);
extern u32 g_acpi_memblock;
extern acbat g_acb[2];
extern int g_acb_n;
extern anode* g_acad;
extern int g_ac_online;
extern int g_acb_pct , g_acb_charging , g_acb_mins;
extern struct nvx_g_bl_t g_bl;

int aec_read(u8 a,u8* v);
const u8* acpi_find_table(const char* sig,int nth);
void acpi_battery_update(int full);
u64 acpi_hi_map(u64 pa);
void bl_set(int pct);
void bl_step(int dir);
void acpi_drain_notify(void);
void acpi_poll(void);
void acpi_aml_init(void);
void acpi_power_button(void);

#endif
