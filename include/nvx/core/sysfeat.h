#ifndef NVX_CORE_SYSFEAT_H
#define NVX_CORE_SYSFEAT_H
enum { SYSF_EXT4_RW=0, SYSF_MIDI, SYSF_INTEL_BLT, SYSF_AMD_FLIP, SYSF_VOLFILES, SYSF_SD_ADMA, SYSF_AMD_SDMA, SYSF_EXFAT, SYSF_VOLWRITE, SYSF_V4L2, SYSF_USBSER, SYSF_COUNT };
enum { SYSF_EXT4_RO=0, SYSF_EXT4_FLUSH=1 };
enum { SYSF_ST_OFF=0, SYSF_ST_ON, SYSF_ST_FAILED, SYSF_ST_UNSUPPORTED, SYSF_ST_NOHW };
int sysfeat_get(int f);
int sysfeat_set(int f,int v);
const char* sysfeat_text(int f);
const char* sysfeat_name(int f);
int sysfeat_max(int f);
const char* sysfeat_choice(int f,int v);
int sysfeat_state(int f);
int sysfeat_default(int f);
struct featguard { const char* name; const char* flag; int on, fails, maxfail, dead, trying; };
void fg_boot(struct featguard* g);
int fg_begin(struct featguard* g);
void fg_ok(struct featguard* g);
void fg_fail(struct featguard* g,const char* why);
void fg_enable(struct featguard* g,int on);
int sysfeat_begin(int f);
void sysfeat_ok(int f);
void sysfeat_fail(int f,const char* why);
#endif
