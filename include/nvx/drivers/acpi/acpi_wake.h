#ifndef NVX_DRIVERS_ACPI_ACPI_WAKE_H
#define NVX_DRIVERS_ACPI_ACPI_WAKE_H

#include "include/nvx/base.h"

struct aw_blk { u16 b0, b1; u8 l0, l1, base1; };
int aw_fadt_blk(const u8* f,u32 len,struct aw_blk* b);
int aw_gpe_reg(const struct aw_blk* b,u32 g,u16* sts,u16* en,u8* bit);
int acpi_wake_arm(int sx);
void acpi_wake_clear(void);
int acpi_wake_poll(void);
int acpi_wake_disarm(void);
int acpi_wake_kbd_ok(int sx);
const char* acpi_wake_status(void);

#endif
