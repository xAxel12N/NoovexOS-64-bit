#ifndef NVX_CORE_SOUND_H
#define NVX_CORE_SOUND_H

#include "include/nvx/base.h"

extern struct nvx_snd g_snd;
extern int g_snd_state , g_snd_vol;

int snd_init(void);
void snd_poll(void);
int snd_dev_write(const char* buf,int len);
int snd_dev_read(char* buf,int len);
int snd_rec_avail(void); const char* snd_rec_name(void); int snd_rec_start(void); void snd_rec_stop(void); void snd_rec_gain(int pct);
int snd_cap_avail(void); int snd_cap_read(u8* d,int len);
extern int g_cap_on, g_cap_level, g_cap_peak, g_mic_gain; extern u32 g_cap_over;
void audio_cmd(const char* a);

#endif
