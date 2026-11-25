#ifndef NVX_CORE_SLEEPSM_H
#define NVX_CORE_SLEEPSM_H
enum { SLP_IDLE=0, SLP_CHECK, SLP_SUSPEND, SLP_ASLEEP, SLP_RESUME, SLP_BLOCKED, SLP_FAILED };
enum { SLP_MODE_NONE=0, SLP_MODE_S2IDLE, SLP_MODE_S1, SLP_MODE_S3 };
#define SLP_MAXDEV 16
struct slp_dev { const char* name; int (*present)(void); const char* (*block)(void); int (*suspend)(void); int (*resume)(void); };
struct slp_sm { const struct slp_dev* dev[SLP_MAXDEV]; int n, state, done, fails, resfail, cycles; unsigned live; char msg[160]; };
int slp_add(struct slp_sm* s,const struct slp_dev* d);
int slp_enter(struct slp_sm* s);
int slp_leave(struct slp_sm* s);
int slp_pick(int enabled,int have_s3,int s3_ok,int have_lps0,int have_s1,int hwred);
int slp_req_due(unsigned long long now,unsigned long long wake_t,int lid_open);
const char* slp_mode_name(int m);
const char* slp_state_name(int st);
#endif
