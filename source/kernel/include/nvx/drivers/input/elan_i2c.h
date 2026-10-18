#ifndef NVX_DRIVERS_INPUT_ELAN_I2C_H
#define NVX_DRIVERS_INPUT_ELAN_I2C_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/input/i2c_hid.h"

int elan_acpi_match(const char* h);
int el_setup(tpdev* t);
void el_process(tpdev* t,const u8* b,int n);

#endif
