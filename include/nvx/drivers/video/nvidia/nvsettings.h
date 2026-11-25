#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVSETTINGS_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVSETTINGS_H

enum{
    NVSET_BOOTSTART=0,
    NVSET_MODESET,
    NVSET_CURSOR,
    NVSET_EXTEND,
    NVSET_CARDMEM,
    NVSET_2D,
    NVSET_DPTRAIN,
    NVSET_AMPERE,
    NVSET_ADA,
    NVSET_SHADER8X,
    NVSET_COUNT
};

enum{
    NVSET_ST_OFF=0,
    NVSET_ST_WAITING,
    NVSET_ST_ACTIVE,
    NVSET_ST_FALLBACK,
    NVSET_ST_AUTO_OFF,
    NVSET_ST_NO_HW
};

#define NVSET_MAX_FAILS 3

int nvset_count(void);
const char* nvset_name(int feat);
const char* nvset_label(int feat);
int nvset_find(const char* name);
int nvset_get(int feat);
int nvset_set(int feat,int on);
int nvset_state(int feat);
int nvset_no_card(void);
const char* nvset_state_text(int feat);
const char* nvset_last_error(int feat);
int nvset_fails(int feat);
int nvset_auto_off(int feat);
void nvset_reset(int feat);

void nvset_load(void);
int nvset_allowed(int feat);
int nvset_try_begin(int feat);
void nvset_try_end(int feat,int ok,const char* err);
void nvset_try_cancel(int feat);
void nvset_note(int feat,int st,const char* err);
int nvset_cmd(const char* args,char* out,int max);

#endif
