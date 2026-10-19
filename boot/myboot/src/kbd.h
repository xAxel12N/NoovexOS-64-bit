#ifndef KBD_H
#define KBD_H
#include "types.h"
#define KEY_NONE  0
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_ENTER 0x102
int kbd_poll(void);
int kbd_getchar(void);
void kbd_flush(void);
#endif
