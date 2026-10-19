#ifndef NVX_DRIVERS_INPUT_PS2_MOUSE_H
#define NVX_DRIVERS_INPUT_PS2_MOUSE_H

#include "include/nvx/base.h"

int pw_read (void);
int mcmd(u8 c);
void mouse_init(void);

#endif
