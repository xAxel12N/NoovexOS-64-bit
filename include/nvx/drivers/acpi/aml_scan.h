#ifndef NVX_DRIVERS_ACPI_AML_SCAN_H
#define NVX_DRIVERS_ACPI_AML_SCAN_H

#include "include/nvx/base.h"

#define AMLN_MAX 768
typedef struct { int tbl; u32 op, s, e; int parent; int dev; char path[72]; } amlnode;
typedef struct { u16 addr; u32 speed; char ctrl[72]; } aml_i2cres;
typedef struct { u16 pin; u8 level, active_low; char ctrl[72]; } aml_gpiores;
typedef struct { int node; char hid[12]; int is_hidi2c; aml_i2cres i2c[6]; int ni2c; aml_gpiores gpio; int has_gpio; u32 mem_base, mem_len; int has_mem; u64 adr; int has_adr; } amldev;
#define AMLD_MAX 160

extern amlnode g_amln[AMLN_MAX];
extern int g_amlt_n;
extern amldev g_amld[AMLD_MAX];
extern int g_amld_n;

void aml_scan_tables(void);
int aml_find_dev(const char* path);
void aml_load_from_acpi(void);

#endif
