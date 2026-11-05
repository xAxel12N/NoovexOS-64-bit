#ifndef NVX_DRIVERS_GPIO_AMD_GPIO_H
#define NVX_DRIVERS_GPIO_AMD_GPIO_H

#include "include/nvx/base.h"

extern int g_amdg_ok, g_amdg_state, g_amdg_pin, g_amdg_pol;
extern u64 g_amdg_base;

void amdg_init(void);
int amdg_present(void);
u32 amdg_reg(int pin);
int amdg_tp_setup(u64 base,int pin,int active_low);
void amdg_sample(void);
void amdg_learn(int rep);
int amdg_ready(void);
int amdg_asserted(void);
void amdg_cmd(const char* a);

#endif
