#ifndef NVX_INIT_SETUP_H
#define NVX_INIT_SETUP_H

#include "include/nvx/base.h"

extern const char *MON[12];

void setup_present(void);
int setup_input(void);
void login_present(void);
int login_input(void);
void setup_begin(void);

#endif
