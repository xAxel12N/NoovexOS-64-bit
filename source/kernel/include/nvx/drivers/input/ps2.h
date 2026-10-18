#ifndef NVX_DRIVERS_INPUT_PS2_H
#define NVX_DRIVERS_INPUT_PS2_H

#include "include/nvx/base.h"

extern int kshift , kcaps , kext , kalt;
extern u8 mb[4];

void ps2_byte(void);

#endif
