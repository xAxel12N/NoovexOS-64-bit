#ifndef NVX_APPS_DOOM_H
#define NVX_APPS_DOOM_H

#include "include/nvx/base.h"

extern unsigned int* DG_ScreenBuffer;
extern u8* g_doom_wad;
extern int g_doomx , g_doomy , g_doom_state;
extern int g_doom_mcap;
extern int doom_mdx , doom_mdy , doom_mbtn , doom_mbtn_prev;

int DG_GetMouse(int* dx,int* dy,int* buttons);
void DG_Init(void);
void DG_SetWindowTitle(const char* t);
unsigned int DG_GetTicksMs(void);
void DG_SleepMs(unsigned int ms);
int DG_GetKey(int* pressed, unsigned char* key);
void DG_DrawFrame(void);
void doom_draw(int i);

#endif
